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

#include <algorithm>
#include <mutex>
#include <queue>
#include <span>

#include <glog/logging.h>

#include <folly/DefaultKeepAliveExecutor.h>
#include <folly/Memory.h>
#include <folly/SharedMutex.h>
#include <folly/container/span.h>
#include <folly/executors/GlobalThreadPoolList.h>
#include <folly/executors/task_queue/LifoSemMPMCQueue.h>
#include <folly/executors/thread_factory/NamedThreadFactory.h>
#include <folly/io/async/Request.h>
#include <folly/portability/GFlags.h>
#include <folly/synchronization/AtomicStruct.h>
#include <folly/synchronization/Baton.h>
#include <folly/synchronization/RelaxedAtomic.h>

namespace folly {

/** Base class for implementing threadpool based executors.
 *
 * Dynamic thread behavior:
 *
 * ThreadPoolExecutors may vary their actual running number of threads
 * between minThreads_ and maxThreads_, tracked by activeThreads_.
 * The actual implementation of joining an idle thread is left to the
 * ThreadPoolExecutors' subclass (typically by LifoSem try_take_for
 * timing out).  Idle threads should be removed from threadList_, and
 * threadsToJoin incremented, and activeThreads_ decremented.
 *
 * On task add(), if an executor can guarantee there is an active
 * thread that will handle the task, then nothing needs to be done.
 * If not, then ensureActiveThreads() should be called to possibly
 * start another pool thread, up to maxThreads_.
 *
 * ensureJoined() is called on add(), such that we can join idle
 * threads that were destroyed (which can't be joined from
 * themselves).
 *
 * Thread pool stats accounting:
 *
 * Derived classes must register instances to keep stats on all thread
 * pools by calling registerThreadPoolExecutor(this) on constructions
 * and deregisterThreadPoolExecutor(this) on destruction.
 *
 * Registration must be done wherever getPendingTaskCountImpl is implemented
 * and getPendingTaskCountImpl should be marked 'final' to avoid data races.
 */
class ThreadPoolExecutor : public DefaultKeepAliveExecutor {
 public:
  /// Constructs the executor with thread limits and a thread factory.
  ///
  /// \param maxThreads Maximum number of threads the pool may run.
  /// \param minThreads Minimum number of threads the pool keeps.
  /// \param threadFactory Factory used to create pool threads.
  explicit ThreadPoolExecutor(
      size_t maxThreads,
      size_t minThreads,
      std::shared_ptr<ThreadFactory> threadFactory);

  /// Destroys the executor, stopping and joining its threads.
  ~ThreadPoolExecutor() override;

  /// Enqueues a function to be executed by the pool.
  ///
  /// \param func The function to execute.
  void add(Func func) override = 0;
  /**
   * If func doesn't get started within expiration time after its enqueued,
   * expireCallback will be run
   *
   * @param func  Main function to be executed
   * @param expiration Maximum time to wait for func to start execution
   * @param expireCallback If expiration limit is reached, execute this callback
   */
  virtual void add(
      Func func, std::chrono::milliseconds expiration, Func expireCallback);

  /// Sets the factory used to create pool threads.
  ///
  /// \param threadFactory The thread factory to use.
  void setThreadFactory(std::shared_ptr<ThreadFactory> threadFactory) {
    CHECK(numThreads() == 0);
    threadFactory_ = std::move(threadFactory);
  }

  /// Returns the factory used to create pool threads.
  ///
  /// \returns The current thread factory.
  std::shared_ptr<ThreadFactory> getThreadFactory() const {
    return threadFactory_;
  }

  /// Returns the configured number of threads in the pool.
  ///
  /// \returns The configured thread count.
  size_t numThreads() const;

  /// Sets the configured number of threads in the pool.
  ///
  /// \param numThreads The desired thread count.
  void setNumThreads(size_t numThreads);

  /// Returns the actual number of active threads, which can differ from
  /// numThreads() due to ThreadPoolExecutor's dynamic behavior.
  ///
  /// \returns The number of currently active threads.
  size_t numActiveThreads() const;

  /**
   * stop() is best effort - there is no guarantee that unexecuted tasks won't
   * be executed before it returns. Specifically, IOThreadPoolExecutor's stop()
   * behaves like join().
   */
  virtual void stop();

  /// Stops the executor and joins all threads, waiting for tasks to finish.
  virtual void join();

  /**
   * Execute f against all ThreadPoolExecutors, primarily for retrieving and
   * exporting stats.
   *
   * \param f The function invoked with each ThreadPoolExecutor.
   */
  static void withAll(FunctionRef<void(ThreadPoolExecutor&)> f);

  /// Snapshot of thread pool statistics.
  struct PoolStats {
    /// Constructs a zero-initialized stats snapshot.
    PoolStats()
        : threadCount(0),
          idleThreadCount(0),
          activeThreadCount(0),
          pendingTaskCount(0),
          totalTaskCount(0),
          processedTaskCount(0),
          maxIdleTime(0) {}
    /// Total, idle, and active thread counts.
    size_t threadCount, idleThreadCount, activeThreadCount;
    /// Pending, total, and processed task counts.
    uint64_t pendingTaskCount, totalTaskCount, processedTaskCount;
    /// Maximum idle time observed across threads.
    std::chrono::nanoseconds maxIdleTime;
  };

  /// Returns a snapshot of the pool's statistics.
  ///
  /// \returns The current pool statistics.
  PoolStats getPoolStats() const;

  /// Returns the number of tasks waiting to be executed.
  ///
  /// \returns The pending task count.
  size_t getPendingTaskCount() const;

  /// Returns the name of the thread pool.
  ///
  /// \returns The pool name.
  const std::string& getName() const;

  /**
   * Return the cumulative CPU time used by all threads in the pool, including
   * those that are no longer alive. Requires system support for per-thread CPU
   * clocks. If not available, the function returns 0. This operation can be
   * expensive.
   *
   * \returns The cumulative CPU time used by all pool threads, or 0 if
   * per-thread CPU clocks are unavailable.
   */
  std::chrono::nanoseconds getUsedCpuTime() const {
    std::shared_lock r{threadListLock_};
    return threadList_.getUsedCpuTime();
  }

  /**
   * Base class for threads created with ThreadPoolExecutor.
   * Some subclasses have methods that operate on these
   * handles.
   */
  class ThreadHandle {
   public:
    /// Destroys the thread handle.
    virtual ~ThreadHandle() = default;
  };

  /**
   * Observer interface for thread start/stop.
   * Provides hooks so actions can be taken when
   * threads are created
   */
  class Observer {
   public:
    /// Destroys the observer.
    virtual ~Observer() = default;

    /// Called when a thread has started.
    ///
    /// \param handle Handle of the thread that started.
    virtual void threadStarted(ThreadHandle* handle) noexcept {}

    /// Called when a thread has stopped.
    ///
    /// \param handle Handle of the thread that stopped.
    virtual void threadStopped(ThreadHandle* handle) noexcept {}

    /// Called for a thread that started before this observer was added.
    ///
    /// \param h Handle of the previously started thread.
    virtual void threadPreviouslyStarted(ThreadHandle* h) noexcept {
      threadStarted(h);
    }

    /// Called for a thread that has not yet stopped when this observer is
    /// removed.
    ///
    /// \param h Handle of the thread that has not yet stopped.
    virtual void threadNotYetStopped(ThreadHandle* h) noexcept {
      threadStopped(h);
    }
  };

  /// Registers an observer for thread start/stop events.
  ///
  /// \param observer The observer to add.
  virtual void addObserver(std::shared_ptr<Observer> observer);

  /// Removes a previously registered observer.
  ///
  /// \param observer The observer to remove.
  virtual void removeObserver(std::shared_ptr<Observer> observer);

  /// Task metadata recorded when a task is enqueued.
  struct TaskInfo {
    /// Scheduling priority of the task.
    int8_t priority;
    /// Identifier of the request context associated with the task.
    uint64_t requestId = 0;
    /// Time when the task was enqueued.
    std::chrono::steady_clock::time_point enqueueTime;
    /// Unique identifier of the task.
    uint64_t taskId;
  };

  /// Task metadata recorded when a task is dequeued.
  struct DequeuedTaskInfo : TaskInfo {
    std::chrono::nanoseconds waitTime{0}; ///< Dequeue time minus enqueueTime.
  };

  /// Task metadata recorded after a task finishes processing.
  struct ProcessedTaskInfo : DequeuedTaskInfo {
    /// Whether the task expired before it ran.
    bool expired = false;
    /// Time spent executing the task.
    std::chrono::nanoseconds runTime{0};
  };

  /// Interface for observing task lifecycle events.
  class TaskObserver {
   public:
    /// Destroys the task observer.
    virtual ~TaskObserver() = default;

    /// Called when a task is enqueued.
    ///
    /// \param info Metadata for the enqueued task.
    virtual void taskEnqueued(const TaskInfo& info) noexcept {}

    /// Called when a task is dequeued.
    ///
    /// \param info Metadata for the dequeued task.
    virtual void taskDequeued(const DequeuedTaskInfo& info) noexcept {}

    /// Called when a task has been processed.
    ///
    /// \param info Metadata for the processed task.
    virtual void taskProcessed(const ProcessedTaskInfo& info) noexcept {}

   private:
    friend class ThreadPoolExecutor;

    TaskObserver* next_ = nullptr;
  };

  /// Adds a task observer.
  ///
  /// For performance reasons, TaskObservers can be added but not removed. All
  /// added observers will be destroyed on executor destruction.
  ///
  /// \param taskObserver The task observer to add.
  void addTaskObserver(std::unique_ptr<TaskObserver> taskObserver);

  // TODO(ott): Migrate call sites to the TaskObserver interface.
  /// Alias for ProcessedTaskInfo used by the legacy stats callback.
  using TaskStats = ProcessedTaskInfo;
  /// Callback invoked with per-task stats.
  using TaskStatsCallback = std::function<void(const TaskStats&)>;

  /// Subscribes a callback to receive per-task stats.
  ///
  /// \param cb The callback invoked with each task's stats.
  [[deprecated("Use addTaskObserver()")]] void subscribeToTaskStats(
      TaskStatsCallback cb);

  /// Sets the idle timeout after which a dynamic thread may be reaped.
  ///
  /// \param timeout The idle timeout before a thread may stop.
  void setThreadDeathTimeout(std::chrono::milliseconds timeout) {
    threadTimeout_ = timeout;
  }

 protected:
  /// Starts n new threads.
  ///
  /// Prerequisite: threadListLock_ writelocked.
  ///
  /// \param n The number of threads to add.
  void addThreads(size_t n);

  /// Tries to start one new thread.
  ///
  /// Prerequisite: threadListLock_ writelocked.
  ///
  /// \returns `true` if a thread was added.
  bool tryAddOneThread() noexcept;

  /// Removes n threads from the pool.
  ///
  /// Prerequisite: threadListLock_ writelocked.
  ///
  /// \param n The number of threads to remove.
  /// \param isJoin Whether the removal is part of a join.
  void removeThreads(size_t n, bool isJoin);

  /// Handle and bookkeeping for a single pool worker thread.
  struct //
      alignas(folly::cacheline_align_v) //
      alignas(folly::AtomicStruct<std::chrono::steady_clock::time_point>) //
      Thread : public ThreadHandle {
    /// Constructs a thread handle with a fresh id and idle state.
    explicit Thread()
        : id(nextId++),
          handle(),
          idle(true),
          lastActiveTime(std::chrono::steady_clock::now()) {}

    /// Destroys the thread handle.
    ~Thread() override = default;

    /// Returns the CPU time consumed by this thread.
    ///
    /// \returns The CPU time used by this thread.
    std::chrono::nanoseconds usedCpuTime() const;

    /// Source of monotonically increasing thread ids.
    static std::atomic<uint64_t> nextId;
    /// Unique identifier for this thread.
    uint64_t id;

    /// Number of tasks processed by this worker.  Reset to zero when
    /// the thread stops.
    folly::relaxed_atomic<uint64_t> processedTasks;

    /// Underlying std::thread handle.
    std::thread handle;
    /// Whether this thread is currently idle.
    std::atomic<bool> idle;
    /// Time this thread was last active.
    folly::AtomicStruct<std::chrono::steady_clock::time_point> lastActiveTime;
    /// Signaled once the thread is alive.
    folly::Baton<> initBaton;
    /// Signaled when the thread may enter its work loop.
    folly::Baton<> readyBaton;
    /// Whether the thread was cancelled before becoming ready.
    bool cancelledBeforeReady{false};
  };

  /// Shared pointer to a pool Thread.
  using ThreadPtr = std::shared_ptr<Thread>;

  /// Performs post-construction setup for newly created threads.
  ///
  /// Prerequisite: threadListLock_ writelocked.
  ///
  /// \param newThreads The threads that were just constructed.
  void afterConstructThreads(std::span<const ThreadPtr> newThreads) noexcept;

  /// A unit of work enqueued in the executor along with its metadata.
  struct Task {
    /// Expiration policy pairing a timeout with a callback.
    struct Expiration {
      /// Maximum time to wait before the task expires.
      std::chrono::milliseconds expiration;
      /// Callback run if the task expires before executing.
      Func expireCallback;
    };

    /// Constructs a task from a function and its scheduling metadata.
    ///
    /// \param func The callable to execute.
    /// \param context The request context captured at enqueue time.
    /// \param expiration Maximum time to wait before the task expires.
    /// \param expireCallback Callback run if the task expires.
    /// \param pri The scheduling priority of the task.
    Task(
        Func&& func,
        std::shared_ptr<folly::RequestContext> context,
        std::chrono::milliseconds expiration,
        Func&& expireCallback,
        int8_t pri = 0);

    /// Returns the scheduling priority of the task.
    ///
    /// \returns The task priority.
    int8_t priority() const { return priority_; }

    /// The callable to execute.
    Func func_;
    /// Time when the task was enqueued.
    std::chrono::steady_clock::time_point enqueueTime_;
    /// Request context captured when the task was enqueued.
    std::shared_ptr<folly::RequestContext> context_;
    /// Optional expiration policy for this task.
    std::unique_ptr<Expiration> expiration_;

   private:
    friend class ThreadPoolExecutor;

    int8_t priority_;
    uint64_t taskId_;
  };

  /// Fills task info metadata from a task.
  ///
  /// \param task The task to read metadata from.
  /// \param info The task info to populate.
  static void fillTaskInfo(const Task& task, TaskInfo& info);

  /// Notifies observers that a task has been enqueued.
  ///
  /// \param task The task that was enqueued.
  void registerTaskEnqueue(const Task& task);

  /// Invokes a function for each registered task observer.
  ///
  /// \tparam F The callable type invoked with each observer.
  /// \param f The function invoked with each task observer.
  template <class F>
  void forEachTaskObserver(F&& f) const {
    auto* taskObserver = taskObservers_.load(std::memory_order_acquire);
    while (taskObserver != nullptr) {
      f(*taskObserver);
      taskObserver = taskObserver->next_;
    }
  }

  /// Runs a task on the given thread.
  ///
  /// \param thread The thread that runs the task.
  /// \param task The task to run.
  void runTask(const ThreadPtr& thread, Task&& task);

  /// Validates a requested thread count, throwing on invalid values.
  ///
  /// \param numThreads The requested thread count to validate.
  virtual void validateNumThreads(size_t numThreads) {}

  /// The function bound to pool threads.
  ///
  /// It must call thread->initBaton.post() once alive, then
  /// thread->readyBaton.wait() followed by a check of
  /// thread->cancelledBeforeReady before entering the work loop.
  ///
  /// \param thread The thread this function runs on.
  virtual void threadRun(ThreadPtr thread) = 0;

  /// Stops n threads, moving their ThreadPtrs to the stoppedThreads_ queue
  /// and removing them from threadList_, either synchronously or
  /// asynchronously.
  ///
  /// Prerequisite: threadListLock_ writelocked.
  ///
  /// \param n The number of threads to stop.
  virtual void stopThreads(size_t n) = 0;

  /// Joins n stopped threads and removes them from the waiting queue.
  ///
  /// Should not hold a lock because joining a thread may invoke cleanup
  /// operations that require a lock on ThreadPoolExecutor.
  ///
  /// \param n The number of stopped threads to join.
  void joinStoppedThreads(size_t n) noexcept;

  /// Stops and joins all threads to implement shutdown.
  ///
  /// \param isJoin Whether the shutdown is a join.
  void stopAndJoinAllThreads(bool isJoin);

  /// Creates a suitable Thread struct.
  ///
  /// \returns A new thread handle.
  virtual ThreadPtr makeThread() { return std::make_shared<Thread>(); }

  /// Registers a thread pool executor with the global list.
  ///
  /// \param tpe The executor to register.
  static void registerThreadPoolExecutor(ThreadPoolExecutor* tpe);

  /// Deregisters a thread pool executor from the global list.
  ///
  /// \param tpe The executor to deregister.
  static void deregisterThreadPoolExecutor(ThreadPoolExecutor* tpe);

  /// Destroys all task observers.
  ///
  /// Must be called from derived class destructors after stop(), while the
  /// derived class vtable is still valid. This ensures observer cleanup
  /// (e.g., fb303 callback unregistration) happens before the vtable reverts
  /// to the base class.
  void destroyTaskObservers();

  /// Returns the number of pending tasks.
  ///
  /// Prerequisite: threadListLock_ readlocked or writelocked.
  ///
  /// \returns The number of pending tasks.
  virtual size_t getPendingTaskCountImpl() const = 0;

  /// Handles registration of a thread with an observer.
  ///
  /// Called with threadListLock_ readlocked or writelocked.
  ///
  /// \param handle The thread being registered.
  /// \param observer The observer to notify.
  virtual void handleObserverRegisterThread(
      ThreadHandle* handle, Observer& observer) noexcept {}

  /// Handles unregistration of a thread with an observer.
  ///
  /// Called with threadListLock_ readlocked or writelocked.
  ///
  /// \param handle The thread being unregistered.
  /// \param observer The observer to notify.
  virtual void handleObserverUnregisterThread(
      ThreadHandle* handle, Observer& observer) noexcept {}

  /// Ordered container of pool threads keyed by thread id.
  class ThreadList {
   public:
    /// Adds a thread to the list.
    ///
    /// \param state The thread to add.
    void add(const ThreadPtr& state) {
      auto it = std::lower_bound(vec_.begin(), vec_.end(), state, Compare{});
      vec_.insert(it, state);
    }

    /// Removes a thread from the list.
    ///
    /// \param state The thread to remove.
    void remove(const ThreadPtr& state) {
      auto itPair =
          std::equal_range(vec_.begin(), vec_.end(), state, Compare{});
      CHECK(itPair.first != vec_.end());
      CHECK(std::next(itPair.first) == itPair.second);
      vec_.erase(itPair.first);
      pastCpuUsed_ += state->usedCpuTime();
    }

    /// Returns whether the list contains the given thread.
    ///
    /// \param ts The thread to look for.
    /// \returns `true` if the thread is present.
    bool contains(const ThreadPtr& ts) const {
      return std::binary_search(vec_.cbegin(), vec_.cend(), ts, Compare{});
    }

    /// Returns the underlying vector of threads.
    ///
    /// \returns The threads in the list.
    const std::vector<ThreadPtr>& get() const { return vec_; }

    /// Returns the CPU time used by live and past threads in the list.
    ///
    /// \returns The accumulated CPU time.
    std::chrono::nanoseconds getUsedCpuTime() const {
      auto acc{pastCpuUsed_};
      for (const auto& thread : vec_) {
        acc += thread->usedCpuTime();
      }
      return acc;
    }

   private:
    struct Compare {
      bool operator()(const ThreadPtr& ts1, const ThreadPtr& ts2) const {
        return ts1->id < ts2->id;
      }
    };
    std::vector<ThreadPtr> vec_;
    // cpu time used by threads that are no longer alive
    std::chrono::nanoseconds pastCpuUsed_{0};
  };

  /// Blocking queue holding threads that have stopped and await joining.
  class StoppedThreadQueue : public BlockingQueue<ThreadPtr> {
   public:
    /// Adds a stopped thread to the queue.
    ///
    /// \param item The stopped thread to enqueue.
    /// \returns The result of the enqueue operation.
    BlockingQueueAddResult add(ThreadPtr&& item) override;

    /// Removes and returns a stopped thread, blocking until one is available.
    ///
    /// \returns The dequeued thread.
    ThreadPtr take() override;

    /// Returns the number of stopped threads in the queue.
    ///
    /// \returns The queue size.
    size_t size() override;

    /// Tries to remove a stopped thread, waiting up to the given timeout.
    ///
    /// \param timeout Maximum time to wait for a thread.
    /// \returns The dequeued thread, or empty if the timeout elapsed.
    folly::Optional<ThreadPtr> try_take_for(
        std::chrono::milliseconds timeout) override;

   private:
    folly::LifoSem sem_;
    std::mutex mutex_;
    std::queue<ThreadPtr> queue_;
  };

  /// Factory used to create pool threads.
  std::shared_ptr<ThreadFactory> threadFactory_;

  /// List of live pool threads.
  ThreadList threadList_;
  /// Guards access to threadList_.
  mutable SharedMutex threadListLock_;
  /// Queue of stopped threads awaiting join.
  StoppedThreadQueue stoppedThreads_;
  std::atomic<bool> isJoin_{false}; ///< Whether the current downsizing is a join.

  /// Registered thread start/stop observers.
  std::vector<std::shared_ptr<Observer>> observers_;
  /// Hook registering this executor with the global thread pool list.
  folly::ThreadPoolListHook threadPoolHook_;

  // Dynamic thread sizing functions and variables
  /// Ensures the number of active threads is raised toward the maximum.
  void ensureMaxActiveThreads();

  /// Starts additional pool threads if needed to handle pending work.
  void ensureActiveThreads() noexcept;

  /// Joins idle threads that were destroyed and still need joining.
  void ensureJoined() noexcept;

  /// Returns whether the pool is at its minimum active thread count.
  ///
  /// \returns `true` if active threads are at the configured minimum.
  bool minActive();

  /// Tries to time out and stop one idle thread.
  ///
  /// \returns `true` if a thread was timed out.
  bool tryTimeoutThread();

  // These are only modified while holding threadListLock_, but
  // are read without holding the lock.
  /// Maximum number of threads in the pool.
  std::atomic<size_t> maxThreads_{0};
  /// Minimum number of threads in the pool.
  std::atomic<size_t> minThreads_{0};
  /// Current number of active threads.
  std::atomic<size_t> activeThreads_{0};
  /// Whether idle threads are allowed to time out and stop.
  std::atomic<bool> threadsCanTimeout_{true};

  /// Number of idle threads pending join.
  std::atomic<size_t> threadsToJoin_{0};
  /// Idle timeout after which a dynamic thread may be reaped.
  std::atomic<std::chrono::milliseconds> threadTimeout_;

  /// Number of tasks processed by stopped or joined threads.  Updated
  /// when a thread stops, which preceeds joining.  Requires holding
  /// the threadListLock_.
  uint64_t stoppedThreadProcessedTasks_{0};

  /// Joins the keep-alive token the first time it is called.
  ///
  /// \returns `true` if this call performed the join, `false` if the
  /// keep-alive was already joined.
  bool joinKeepAliveOnce() {
    if (!std::exchange(keepAliveJoined_, true)) {
      joinKeepAlive();
      return true;
    }
    return false;
  }

  /// Whether the keep-alive token has already been joined.
  bool keepAliveJoined_{false};

 private:
  std::atomic<TaskObserver*> taskObservers_{nullptr};
};

} // namespace folly
