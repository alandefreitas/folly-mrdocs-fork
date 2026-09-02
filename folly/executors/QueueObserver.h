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

#include <stdint.h>
#include <sys/types.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <folly/Function.h>
#include <folly/Portability.h>
#include <folly/Synchronized.h>
#include <folly/portability/SysTypes.h>

namespace folly {

/// Per-request context associated with an enqueued item.
class RequestContext;

/**
 * WorkerProvider is a simple interface that can be used
 * to collect information about worker threads that are pulling work
 * from a given queue.
 */
class WorkerProvider {
 public:
  /// Destroys the worker provider.
  virtual ~WorkerProvider() {}

  /**
   * Abstract type returned by the collectThreadIds() method.
   * Implementations of the WorkerProvider interface need to define this class.
   * The intent is to return a guard along with a list of worker IDs which can
   * be removed on destruction of this object.
   */
  class KeepAlive {
   public:
    /// Destroys the keep-alive guard.
    virtual ~KeepAlive() = 0;
  };

  /// Aggregate returned by collectThreadIds() with a keep-alive guard and ids.
  struct IdsWithKeepAlive {
    /// Guard whose destruction releases the reported thread ids.
    std::unique_ptr<KeepAlive> guard;
    /// The captured OS thread ids.
    std::vector<pid_t> threadIds;
  };

  /// Captures the thread ids of all threads consuming from a given queue.
  ///
  /// The returned aggregate holds the OS thread ids and a keep-alive guard
  /// that the caller holds while the ids remain valid.
  ///
  /// \returns The captured thread ids and their keep-alive guard.
  virtual IdsWithKeepAlive collectThreadIds() = 0;
};

/**
 * Interface for executors that provide a WorkerProvider to collect thread ids.
 */
class GetThreadIdCollector {
 public:
  /// Destroys the collector interface.
  virtual ~GetThreadIdCollector() = default;

  /// Returns the WorkerProvider used to collect thread ids.
  ///
  /// \returns The worker provider, or nullptr if none is available.
  virtual WorkerProvider* getThreadIdCollector() = 0;
};

/// WorkerProvider backed by an explicitly maintained set of thread ids.
class ThreadIdWorkerProvider : public WorkerProvider {
 public:
  /// Captures the current worker thread ids together with a keep-alive guard.
  ///
  /// \returns The collected thread ids and their keep-alive guard.
  IdsWithKeepAlive collectThreadIds() override final;

  /// Adds a worker thread id to the tracked set.
  ///
  /// \param tid The OS thread id to add.
  void addTid(pid_t tid);

  /// Removes a worker thread id from the tracked set.
  ///
  /// Will block until all KeepAlives have been destroyed, if any exist.
  ///
  /// \param tid The OS thread id to remove.
  void removeTid(pid_t tid);

 private:
  Synchronized<std::unordered_set<pid_t>> osThreadIds_;
  mutable SharedMutex threadsExitMutex_;
};

/// Observes enqueue and dequeue events on a queue.
class QueueObserver {
 public:
  /// Destroys the observer.
  virtual ~QueueObserver() {}

  /// Records that a request has been enqueued.
  ///
  /// \param context The request context being enqueued.
  /// \returns A token identifying the enqueued request.
  virtual intptr_t onEnqueued(const RequestContext* context) = 0;

  /// Records that a previously enqueued request has been dequeued.
  ///
  /// \param token The token returned by onEnqueued for this request.
  virtual void onDequeued(intptr_t token) = 0;
};

/// Factory that creates QueueObserver instances for a queue.
class QueueObserverFactory {
 public:
  /// Destroys the factory.
  virtual ~QueueObserverFactory() {}

  /// Creates a QueueObserver for the given priority.
  ///
  /// \param pri The priority the observer will monitor.
  /// \returns A new QueueObserver for the given priority.
  virtual std::unique_ptr<QueueObserver> create(int8_t pri) = 0;

  /// Builds a factory for the given context and priority count.
  ///
  /// \param context Identifier for the queue being observed.
  /// \param numPriorities Number of priorities the queue supports.
  /// \param workerProvider Optional provider of worker thread ids.
  /// \returns A new QueueObserverFactory, or nullptr if unavailable.
  static std::unique_ptr<QueueObserverFactory> make(
      const std::string& context,
      size_t numPriorities,
      WorkerProvider* workerProvider = nullptr);
};

/// Function type that builds a QueueObserverFactory.
using MakeQueueObserverFactory = std::unique_ptr<QueueObserverFactory>(
    const std::string& context, size_t numPriorities, WorkerProvider* workerProvider);
#if FOLLY_HAVE_WEAK_SYMBOLS
/// Weak hook used to build a QueueObserverFactory when provided at link time.
///
/// \implementationdefined
FOLLY_ATTR_WEAK MakeQueueObserverFactory make_queue_observer_factory;
#else
/// Weak hook used to build a QueueObserverFactory when provided at link time.
constexpr MakeQueueObserverFactory* make_queue_observer_factory = nullptr;
#endif

/// Aggregates the worker thread ids per queue name along with their keep-alives.
struct QueueInfo {
  /// Maps each queue name to the set of OS thread ids serving it.
  std::unordered_map<std::string, std::unordered_set<pid_t>> namesAndTids;
  /// Keep-alive guards that keep the reported thread ids valid.
  std::vector<std::unique_ptr<WorkerProvider::KeepAlive>> keepAlives;
};

/// Callable that returns information about lagging queues.
using LaggingQueueInfoFunc = folly::Function<QueueInfo()>;
} // namespace folly
