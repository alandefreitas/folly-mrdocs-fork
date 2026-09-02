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

#include <chrono>
#include <limits>

#include <folly/Portability.h>
#include <folly/concurrency/UnboundedQueue.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/executors/QueueObserver.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/io/async/EventBasePoller.h>
#include <folly/synchronization/Baton.h>
#include <folly/synchronization/RelaxedAtomic.h>
#include <folly/synchronization/ThrottledLifoSem.h>
#include <folly/synchronization/WaitOptions.h>

namespace folly {

/**
 * NOTE: This is highly experimental. Do not use.
 *
 * A pool of EventBases scheduled over a pool of threads.
 *
 * Intended as a drop-in replacement for folly::IOThreadPoolExecutor, but with a
 * substantially different design: EventBases are not pinned to threads, so it
 * is possible to have more EventBases than threads. EventBases that have ready
 * events can be scheduled on any of the threads in the pool, with the
 * scheduling governed by ThrottledLifoSem.
 *
 * This allows to batch the loops of multiple EventBases on a single thread as
 * long as each runs for a short enough time, reducing the number of wake-ups
 * and allowing for better load balancing across handlers. For example, we can
 * create a large number of EventBases processed by a smaller number of threads
 * and distribute the handlers.
 *
 * The number of EventBases is set at construction time and cannot be changed
 * later. The number of threads can be changed dynamically, but setting it to 0
 * is not supported (otherwise no thread would be left to drive the EventBases)
 * and it is not useful to run more threads than EventBases, so that is not
 * supported either: attempting to set the number of threads to 0 or to a value
 * greater than numEventBases() (either in construction or using
 * setNumThreads()) will throw std::invalid_argument).
 */
class MuxIOThreadPoolExecutor : public IOThreadPoolExecutorBase {
 public:
  /// Configuration options for the executor.
  struct Options {
    /// Constructs options with default values.
    Options() {}

    /// Sets whether per-thread id collection is enabled.
    ///
    /// \param b Whether to enable thread id collection.
    /// \returns A reference to these options.
    Options& setEnableThreadIdCollection(bool b) {
      enableThreadIdCollection = b;
      return *this;
    }

    /// Sets the number of EventBases in the pool.
    ///
    /// \param num The number of EventBases to create.
    /// \returns A reference to these options.
    Options& setNumEventBases(size_t num) {
      numEventBases = num;
      return *this;
    }

    /// Sets the interval at which idle threads wake up to check for work.
    ///
    /// \param w The wake-up interval.
    /// \returns A reference to these options.
    Options& setWakeUpInterval(std::chrono::nanoseconds w) {
      wakeUpInterval = w;
      return *this;
    }

    /// Sets the maximum time an idle thread spins before going to sleep.
    ///
    /// \param s The maximum idle spin duration.
    /// \returns A reference to these options.
    Options& setIdleSpinMax(std::chrono::nanoseconds s) {
      idleSpinMax = s;
      return *this;
    }

    /// Whether per-thread id collection is enabled.
    bool enableThreadIdCollection{false};
    /// Number of EventBases. If 0, it is set to the number of threads.
    size_t numEventBases{0};
    /// Interval at which idle threads wake up to check for work.
    std::chrono::nanoseconds wakeUpInterval{std::chrono::microseconds{100}};
    /// Max spin for an idle thread waiting for work before going to sleep.
    std::chrono::nanoseconds idleSpinMax = std::chrono::microseconds{10};
  };

  /// Constructs the executor.
  ///
  /// \param numThreads The number of threads driving the EventBases.
  /// \param options Configuration options for the executor.
  /// \param threadFactory Factory used to create the worker threads.
  /// \param ebm The EventBaseManager to use.
  explicit MuxIOThreadPoolExecutor(
      size_t numThreads,
      Options options = {},
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("MuxIOTPEx"),
      folly::EventBaseManager* ebm = folly::EventBaseManager::get());

  /// Destroys the executor, stopping and joining its threads.
  ~MuxIOThreadPoolExecutor() override;

  /// Returns the number of EventBases in the pool.
  ///
  /// \returns The number of EventBases.
  size_t numEventBases() const { return numEventBases_; }

  /// Schedules a task to run on the pool.
  ///
  /// \param func The task to run.
  void add(Func func) override;

  /// Schedules a task to run on the pool with an expiration.
  ///
  /// \param func The task to run.
  /// \param expiration The duration after which the task expires.
  /// \param expireCallback Called if the task expires before it runs.
  void add(
      Func func,
      std::chrono::milliseconds expiration,
      Func expireCallback = nullptr) override;

  /// Returns one of the EventBases in the pool.
  ///
  /// \returns A pointer to an EventBase.
  folly::EventBase* getEventBase() override;

  /// Returns all the EventBase instances.
  ///
  /// \returns Keep-alive handles for every EventBase in the pool.
  std::vector<folly::Executor::KeepAlive<folly::EventBase>> getAllEventBases()
      override;

  /// Returns the EventBaseManager used by the executor.
  ///
  /// \returns A pointer to the EventBaseManager.
  folly::EventBaseManager* getEventBaseManager() override;

  /// Returns the worker id collector.
  ///
  /// Returns nullptr unless explicitly enabled through the constructor.
  ///
  /// \returns A pointer to the WorkerProvider, or nullptr.
  folly::WorkerProvider* getThreadIdCollector() override {
    return threadIdCollector_.get();
  }

  /// Registers an observer for executor events.
  ///
  /// \param o The observer to add.
  void addObserver(std::shared_ptr<Observer> o) override;

  /// Unregisters a previously registered observer.
  ///
  /// \param o The observer to remove.
  void removeObserver(std::shared_ptr<Observer> o) override;

  /// Stops the executor without waiting for its threads to finish.
  void stop() override;

  /// Stops the executor and waits for its threads to finish.
  void join() override;

 private:
  using EventBasePoller = folly::detail::EventBasePoller;

  struct EvbState;

  struct alignas(Thread) IOThread : public Thread {
    EvbState* curEvbState; // Only accessed inside the worker thread.
  };

  void maybeUnregisterEventBases(Observer* o);

  void validateNumThreads(size_t numThreads) override;
  ThreadPtr makeThread() override;
  EvbState& pickEvbState();
  void threadRun(ThreadPtr thread) override;
  void stopThreads(size_t n) override;
  size_t getPendingTaskCountImpl() const override final;

  const Options options_;
  const size_t numEventBases_;
  folly::EventBaseManager* eventBaseManager_;

  std::unique_ptr<EventBasePoller::FdGroup> fdGroup_;
  std::vector<std::unique_ptr<EvbState>> evbStates_;
  std::vector<Executor::KeepAlive<EventBase>> keepAlives_;

  relaxed_atomic<size_t> nextEvb_{0};
  folly::ThreadLocal<std::shared_ptr<IOThread>> thisThread_;
  std::unique_ptr<ThreadIdWorkerProvider> threadIdCollector_;
  std::atomic<size_t> pendingTasks_{0};

  USPMCQueue<EventBasePoller::Handle*, /* MayBlock */ false> readyQueue_;
  folly::ThrottledLifoSem readyQueueSem_;
};

} // namespace folly
