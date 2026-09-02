/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <glog/logging.h>

#include <folly/Portability.h>
#include <folly/fibers/FiberManager.h>
#include <folly/functional/Invoke.h>
#include <folly/futures/Future.h>
#include <folly/observer/detail/Core.h>
#include <folly/observer/detail/GraphCycleDetector.h>
#include <folly/synchronization/SanitizeThread.h>

/// The Folly library.
namespace folly {
/// Implementation details.
namespace observer_detail {

/**
 * ObserverManager is a singleton which controls the re-computation of all
 * Observers. Such re-computation always happens on the thread pool owned by
 * ObserverManager.
 *
 * ObserverManager has global current version. All existing Observers
 * may have their version be less (yet to be updated) or equal (up to date)
 * to the global current version.
 *
 * ObserverManager::CurrentQueue contains all of the Observers which need to be
 * updated to the global current version. Those updates are peformed on the
 * ObserverManager's thread pool, until the queue is empty. If some Observer is
 * updated, all of its dependents are added to ObserverManager::CurrentQueue
 * to be updated.
 *
 * If some leaf Observer (i.e. created from Observable) is updated, then current
 * version of the ObserverManager should be bumped. All such updated leaf
 * Observers are added to the ObserverManager::NextQueue.
 *
 * *Only* when ObserverManager::CurrentQueue is empty, the global current
 * version is bumped and all updates from the ObserverManager::NextQueue are
 * performed. If leaf Observer gets updated more then once before being picked
 * from the ObserverManager::NextQueue, then only the last update is processed.
 */
class ObserverManager {
 public:
  /// Returns the global current version.
  ///
  /// \returns The current version of the ObserverManager.
  static size_t getVersion() { return getInstance().version_; }

  /// Checks whether the caller runs on the ObserverManager thread.
  ///
  /// \returns True if called from the manager thread.
  static bool inManagerThread() { return inManagerThread_; }

  /// Ensures the ObserverManager singleton is initialized.
  static void vivify() { getUpdatesManager(); }

  /// Schedules a Core to be refreshed to at least the given version.
  ///
  /// \param core The Core to refresh.
  /// \param minVersion The minimum version the Core should reach.
  static void scheduleRefresh(Core::Ptr core, size_t minVersion) {
    if (core->getVersion() >= minVersion) {
      return;
    }

    auto& instance = getInstance();

    std::shared_lock rh(instance.versionMutex_);

    instance.scheduleCurrent(
        [coreWeak = folly::to_weak_ptr(std::move(core)),
         &instance,
         rh_2 = std::move(rh)]() {
          if (auto coreShared = coreWeak.lock()) {
            coreShared->refresh(instance.version_);
          }
        });
  }

  /// Schedules a leaf Core update that bumps the global version.
  ///
  /// \param coreFunc Functor returning the Core to update.
  static void scheduleRefreshNewVersion(Function<Core::Ptr()> coreFunc) {
    getInstance().scheduleNext(std::move(coreFunc));
  }

  /// Performs the initial refresh of a newly created Core.
  ///
  /// \param core The Core to initialize.
  static void initCore(Core::Ptr core) {
    DCHECK(core->getVersion() == 0);

    auto& instance = getInstance();

    folly::fibers::runInMainContext([&] {
      auto inManagerThread = std::exchange(inManagerThread_, true);
      SCOPE_EXIT {
        inManagerThread_ = inManagerThread;
      };

      std::shared_lock rh(instance.versionMutex_);

      core->refresh(instance.version_);
    });
  }

  /// Blocks until all pending updates have been processed.
  static void waitForAllUpdates() {
    tryWaitForAllUpdatesImpl([=](auto& m) { return std::unique_lock(m); });
  }
  /// Waits for all pending updates without blocking.
  ///
  /// \returns True if all updates were processed.
  static bool tryWaitForAllUpdates() {
    return tryWaitForAllUpdatesImpl([=](auto& m) {
      return std::unique_lock(m, std::try_to_lock);
    });
  }
  /// Waits for all pending updates up to a timeout.
  ///
  /// \param timeout Maximum time to wait.
  /// \returns True if all updates were processed before the timeout.
  template <typename Rep, typename Period>
  static bool tryWaitForAllUpdatesFor(
      std::chrono::duration<Rep, Period> timeout) {
    return tryWaitForAllUpdatesImpl([=](auto& m) {
      return std::unique_lock(m, timeout);
    });
  }
  /// Waits for all pending updates until a deadline.
  ///
  /// \param deadline Point in time to stop waiting.
  /// \returns True if all updates were processed before the deadline.
  template <typename Clock, typename Duration>
  static bool tryWaitForAllUpdatesUntil(
      std::chrono::time_point<Clock, Duration> deadline) {
    return tryWaitForAllUpdatesImpl([=](auto& m) {
      return std::unique_lock(m, deadline);
    });
  }

  /// Records the dependencies accessed while an Observer is recomputed.
  class DependencyRecorder {
   public:
    /// Set of cores an Observer depends on.
    using DependencySet = std::unordered_set<Core::Ptr>;
    /// Dependencies collected for a single Core.
    struct Dependencies {
      /// Constructs a dependency set bound to a Core.
      ///
      /// \param core_ The Core whose dependencies are recorded.
      explicit Dependencies(const Core& core_) : core(core_) {}

      /// The cores this Core depends on.
      DependencySet dependencies;
      /// The Core these dependencies belong to.
      const Core& core;
    };

    /// Starts recording dependencies for the given Core.
    ///
    /// \param core The Core being recomputed.
    explicit DependencyRecorder(const Core& core) : dependencies_(core) {
      DCHECK(inManagerThread());

      previousDepedencies_ = currentDependencies_;
      currentDependencies_ = &dependencies_;
    }

    /// Checks whether dependency recording is currently active.
    ///
    /// \returns True if a recorder is active on this thread.
    static bool isActive() { return currentDependencies_; }

    /// Invokes a function with dependency recording temporarily disabled.
    ///
    /// \param f Function to invoke.
    /// \returns The result of invoking f.
    template <typename F>
    static invoke_result_t<F> withDependencyRecordingDisabled(F f) {
      auto* const dependencies = std::exchange(currentDependencies_, nullptr);
      SCOPE_EXIT {
        currentDependencies_ = dependencies;
      };

      return f();
    }

    /// Records that the current Observer depends on another Core.
    ///
    /// \param dependency The Core that was accessed.
    static void markDependency(Core::Ptr dependency) {
      DCHECK(inManagerThread());
      DCHECK(currentDependencies_);

      currentDependencies_->dependencies.insert(std::move(dependency));
    }

    /// Records a refresh-time dependency edge for cycle detection.
    ///
    /// \param core The Core being depended upon.
    static void markRefreshDependency(const Core& core) {
      if (!kIsDebug) {
        return;
      }
      if (!currentDependencies_) {
        return;
      }

      getInstance().cycleDetector_.withLock([&](CycleDetector& cycleDetector) {
        bool hasCycle =
            !cycleDetector.addEdge(&currentDependencies_->core, &core);
        if (hasCycle) {
          LOG(FATAL) << "Observer cycle detected.";
        }
      });
    }

    /// Removes a refresh-time dependency edge for cycle detection.
    ///
    /// \param core The Core no longer depended upon.
    static void unmarkRefreshDependency(const Core& core) {
      if (!kIsDebug) {
        return;
      }
      if (!currentDependencies_) {
        return;
      }

      getInstance().cycleDetector_.withLock([&](CycleDetector& cycleDetector) {
        cycleDetector.removeEdge(&currentDependencies_->core, &core);
      });
    }

    /// Stops recording and returns the collected dependencies.
    ///
    /// \returns The set of dependencies recorded.
    DependencySet release() {
      DCHECK(currentDependencies_ == &dependencies_);
      std::swap(currentDependencies_, previousDepedencies_);
      previousDepedencies_ = nullptr;

      return std::move(dependencies_.dependencies);
    }

    /// Destroys the recorder, releasing it if still active.
    ~DependencyRecorder() {
      if (currentDependencies_ == &dependencies_) {
        release();
      }
    }

   private:
    Dependencies dependencies_;
    Dependencies* previousDepedencies_;

    static thread_local Dependencies* currentDependencies_;
  };

 private:
  using TryWaitForAllUpdatesImplOp = FunctionRef<
      std::unique_lock<SharedMutexReadPriority>(SharedMutexReadPriority&)>;
  ObserverManager() {}

  static bool tryWaitForAllUpdatesImpl(TryWaitForAllUpdatesImplOp op);

  void scheduleCurrent(Function<void()>);
  void scheduleNext(Function<Core::Ptr()>);

  class UpdatesManager {
   public:
    UpdatesManager();
    bool tryWaitForAllUpdatesImpl(TryWaitForAllUpdatesImplOp op);

   private:
    class CurrentQueueProcessor;
    class NextQueueProcessor;

    std::unique_ptr<CurrentQueueProcessor> currentQueueProcessor_;
    std::unique_ptr<NextQueueProcessor> nextQueueProcessor_;
  };
  struct Singleton;

  static ObserverManager& getInstance();
  static std::shared_ptr<UpdatesManager> getUpdatesManager();
  static thread_local bool inManagerThread_;

  /**
   * Version mutex is used to make sure all updates are processed from the
   * CurrentQueue, before bumping the version and moving to the NextQueue.
   *
   * To achieve this every task added to CurrentQueue holds a reader lock.
   * NextQueue grabs a writer lock before bumping the version, so it can only
   * happen if CurrentQueue is empty (notice that we use read-priority shared
   * mutex).
   */
  mutable SharedMutexReadPriority versionMutex_;
  std::atomic<size_t> version_{1};

  using CycleDetector = GraphCycleDetector<const Core*>;
  folly::Synchronized<CycleDetector, std::mutex> cycleDetector_;
};
} // namespace observer_detail
} // namespace folly
