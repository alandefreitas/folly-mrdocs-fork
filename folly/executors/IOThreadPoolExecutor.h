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

#include <folly/Portability.h>
#include <folly/executors/IOExecutor.h>
#include <folly/executors/QueueObserver.h>
#include <folly/executors/ThreadPoolExecutor.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/synchronization/RelaxedAtomic.h>

/// Declares the gflag controlling the default max reads per loop iteration.
///
/// \implementationdefined
FOLLY_GFLAGS_DECLARE_int32(folly_iothreadpoolexecutor_max_read_at_once);

namespace folly {

FOLLY_PUSH_WARNING
// Suppress "IOThreadPoolExecutor inherits DefaultKeepAliveExecutor
// keepAliveAcquire/keepAliveRelease via dominance"
FOLLY_MSVC_DISABLE_WARNING(4250)

/// Base interface for executors that run tasks on IO threads with EventBases.
class IOThreadPoolExecutorBase
    : public ThreadPoolExecutor,
      public IOExecutor,
      public GetThreadIdCollector {
 public:
  /// Inherits the ThreadPoolExecutor constructors.
  using ThreadPoolExecutor::ThreadPoolExecutor;

  /// Destroys the executor base.
  ~IOThreadPoolExecutorBase() override = default;

  /// Returns an EventBase to schedule IO work on.
  ///
  /// \returns An EventBase owned by the executor.
  folly::EventBase* getEventBase() override = 0;

  /// Returns keep-alive handles to every EventBase in the executor.
  ///
  /// \returns Keep-alive handles to each thread's EventBase.
  virtual std::vector<folly::Executor::KeepAlive<folly::EventBase>>
  getAllEventBases() = 0;

  /// Returns the EventBaseManager used by this executor.
  ///
  /// \returns The EventBaseManager for this executor.
  virtual folly::EventBaseManager* getEventBaseManager() = 0;

  /// Observer that is notified when EventBases are registered.
  class IOObserver : public Observer {
   public:
    /// Called when an EventBase is registered with the executor.
    ///
    /// \param eventBase The EventBase being registered.
    virtual void registerEventBase(EventBase& eventBase) noexcept {}

    /// Called when an EventBase is unregistered from the executor.
    ///
    /// \param eventBase The EventBase being unregistered.
    virtual void unregisterEventBase(EventBase& eventBase) noexcept {}
  };
};

/**
 * A Thread Pool for IO bound tasks
 *
 * @note Uses event_fd for notification, and waking an epoll loop.
 * There is one queue (NotificationQueue specifically) per thread/epoll.
 * If the thread is already running and not waiting on epoll,
 * we don't make any additional syscalls to wake up the loop,
 * just put the new task in the queue.
 * If any thread has been waiting for more than a few seconds,
 * its stack is madvised away. Currently however tasks are scheduled round
 * robin on the queues, so unless there is no work going on,
 * this isn't very effective.
 * Since there is one queue per thread, there is hardly any contention
 * on the queues - so a simple spinlock around an std::deque is used for
 * the tasks. There is no max queue size.
 * By default, there is one thread per core - it usually doesn't make sense to
 * have more IO threads than this, assuming they don't block.
 *
 * @note ::getEventBase() will return an EventBase you can schedule IO work on
 * directly, chosen round-robin.
 *
 * @note N.B. For this thread pool, stop() behaves like join() because
 * outstanding tasks belong to the event base and will be executed upon its
 * destruction.
 */
class IOThreadPoolExecutor : public IOThreadPoolExecutorBase {
 public:
  /// Configuration options for an IOThreadPoolExecutor.
  struct Options {
    /// Constructs options with default values.
    Options()
        : waitForAll(false),
          enableThreadIdCollection(false),
          maxReadAtOnce(
              FLAGS_folly_iothreadpoolexecutor_max_read_at_once < 0
                  ? decltype(maxReadAtOnce){}
                  : decltype(maxReadAtOnce){
                        FLAGS_folly_iothreadpoolexecutor_max_read_at_once}) {}

    /// Sets whether shutdown waits for all outstanding work.
    ///
    /// \param b True to wait for all outstanding work on shutdown.
    /// \returns A reference to this Options instance.
    Options& setWaitForAll(bool b) {
      this->waitForAll = b;
      return *this;
    }

    /// Sets whether worker thread id collection is enabled.
    ///
    /// \param b True to enable thread id collection.
    /// \returns A reference to this Options instance.
    Options& setEnableThreadIdCollection(bool b) {
      this->enableThreadIdCollection = b;
      return *this;
    }

    /// Sets the maximum number of reads to process at once per loop.
    ///
    /// \param w The maximum number of reads per loop iteration.
    /// \returns A reference to this Options instance.
    Options& setMaxReadAtOnce(uint32_t w) {
      this->maxReadAtOnce = w;
      return *this;
    }

    /// Whether shutdown waits for all outstanding work.
    bool waitForAll;
    /// Whether worker thread id collection is enabled.
    bool enableThreadIdCollection;
    /// Maximum number of reads to process at once per loop, if set.
    std::optional<uint32_t> maxReadAtOnce;
  };

  /// Constructs an executor with a fixed number of IO threads.
  ///
  /// \param numThreads Number of IO threads to run.
  /// \param threadFactory Factory used to create the threads.
  /// \param ebm EventBaseManager used to manage the threads' EventBases.
  /// \param options Additional executor options.
  explicit IOThreadPoolExecutor(
      size_t numThreads,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("IOThreadPool"),
      folly::EventBaseManager* ebm = folly::EventBaseManager::get(),
      Options options = Options());

  /// Constructs an executor with a variable number of IO threads.
  ///
  /// \param maxThreads Maximum number of IO threads to run.
  /// \param minThreads Minimum number of IO threads to keep running.
  /// \param threadFactory Factory used to create the threads.
  /// \param ebm EventBaseManager used to manage the threads' EventBases.
  /// \param options Additional executor options.
  IOThreadPoolExecutor(
      size_t maxThreads,
      size_t minThreads,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("IOThreadPool"),
      folly::EventBaseManager* ebm = folly::EventBaseManager::get(),
      Options options = Options());

  /// Stops the executor and joins all IO threads.
  ~IOThreadPoolExecutor() override;

  /// Schedules a task to run on one of the IO threads.
  ///
  /// \param func The task to run.
  void add(Func func) override;

  /// Schedules a task with an expiration and optional expiration callback.
  ///
  /// \param func The task to run.
  /// \param expiration Time after which the task is considered expired.
  /// \param expireCallback Callback invoked if the task expires before running.
  void add(
      Func func,
      std::chrono::milliseconds expiration,
      Func expireCallback = nullptr) override;

  /// Returns an EventBase to schedule IO work on, chosen round-robin.
  ///
  /// \returns An EventBase owned by one of the pool threads.
  folly::EventBase* getEventBase() override;

  /// Ensures that the maximum number of active threads is running and returns
  /// the EventBase associated with each thread.
  ///
  /// \returns Keep-alive handles to each thread's EventBase.
  std::vector<folly::Executor::KeepAlive<folly::EventBase>> getAllEventBases()
      override;

  /// Returns the EventBase associated with a given thread handle.
  ///
  /// \param h The thread handle to query.
  /// \returns The EventBase owned by the given thread.
  static folly::EventBase* getEventBase(ThreadPoolExecutor::ThreadHandle* h);

  /// Returns the EventBaseManager used by this executor.
  ///
  /// \returns The EventBaseManager for this executor.
  folly::EventBaseManager* getEventBaseManager() override;

  /// Returns the worker provider used to collect thread ids.
  ///
  /// Returns nullptr unless explicitly enabled through the constructor.
  ///
  /// \returns The thread id collector, or nullptr if disabled.
  folly::WorkerProvider* getThreadIdCollector() override {
    return threadIdCollector_.get();
  }

 protected:
  /// A pool thread that owns an EventBase and tracks its pending work.
  struct alignas(Thread) IOThread : public Thread {
    /// Whether the thread's event loop should keep running.
    std::atomic<bool> shouldRun{true};
    /// Number of tasks pending on this thread.
    std::atomic<size_t> pendingTasks{0};
    /// The EventBase driven by this thread.
    folly::EventBase* eventBase{nullptr};
    /// Guards shutdown of this thread's EventBase.
    std::mutex eventBaseShutdownMutex_;
  };

  /// Notifies the observer that a thread has been registered.
  ///
  /// \param h Handle for the thread being registered.
  /// \param observer The observer to notify.
  void handleObserverRegisterThread(
      ThreadHandle* h, Observer& observer) noexcept override;

  /// Notifies the observer that a thread has been unregistered.
  ///
  /// \param h Handle for the thread being unregistered.
  /// \param observer The observer to notify.
  void handleObserverUnregisterThread(
      ThreadHandle* h, Observer& observer) noexcept override;

 private:
  ThreadPtr makeThread() override;
  std::shared_ptr<IOThread> pickThread();
  void threadRun(ThreadPtr thread) override;
  void stopThreads(size_t n) override;
  size_t getPendingTaskCountImpl() const override final;
  const bool isWaitForAll_; // whether to wait till event base loop exits
  relaxed_atomic<size_t> nextThread_;
  folly::ThreadLocal<std::shared_ptr<IOThread>> thisThread_;
  folly::EventBaseManager* eventBaseManager_;
  std::unique_ptr<ThreadIdWorkerProvider> threadIdCollector_;
  const std::optional<uint32_t> maxReadAtOnce_;
};

FOLLY_POP_WARNING

} // namespace folly
