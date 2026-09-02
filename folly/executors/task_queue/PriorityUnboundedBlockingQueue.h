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

#include <folly/ConstexprMath.h>
#include <folly/Executor.h>
#include <folly/concurrency/PriorityUnboundedQueueSet.h>
#include <folly/executors/task_queue/BlockingQueue.h>
#include <folly/lang/Exception.h>
#include <folly/synchronization/LifoSem.h>

namespace folly {

/// A blocking queue with a fixed number of priority levels.
///
/// Items are stored in an unbounded multi-producer multi-consumer queue per
/// priority level, and consumers block on a semaphore until an item is
/// available.
///
/// \tparam T The type of the elements stored in the queue.
/// \tparam Semaphore The semaphore type used to block consumers.
template <class T, class Semaphore = folly::LifoSem>
class PriorityUnboundedBlockingQueue : public BlockingQueue<T> {
 public:
  /// Construct a queue with the given number of priority levels.
  ///
  /// To use folly::Executor::*_PRI, for numPriorities == 2 MID_PRI and HI_PRI
  /// are treated at the same priority level.
  ///
  /// \param numPriorities The number of distinct priority levels.
  /// \param semaphoreOptions Options forwarded to the semaphore.
  explicit PriorityUnboundedBlockingQueue(
      uint8_t numPriorities,
      const typename Semaphore::Options& semaphoreOptions = {})
      : sem_(semaphoreOptions), queue_(numPriorities) {}

  /// Return the number of priority levels supported by the queue.
  ///
  /// \returns The number of priority levels.
  uint8_t getNumPriorities() override { return queue_.priorities(); }

  /// Add an item to the queue at medium priority.
  ///
  /// \param item The item to enqueue.
  /// \returns The result of the enqueue operation.
  BlockingQueueAddResult add(T&& item) override {
    return addWithPriority(std::move(item), folly::Executor::MID_PRI);
  }

  /// Add an item to the queue at the given priority.
  ///
  /// \param item The item to enqueue.
  /// \param priority The priority level for the item.
  /// \returns The result of the enqueue operation.
  BlockingQueueAddResult addWithPriority(T&& item, int8_t priority) override {
    queue_.at_priority(translatePriority(priority)).enqueue(std::move(item));
    return sem_.post();
  }

  /// Remove and return an item, blocking until one is available.
  ///
  /// \returns The dequeued item.
  T take() override {
    sem_.wait();
    return dequeue();
  }

  /// Remove and return an item if one is immediately available.
  ///
  /// \returns The dequeued item, or none if the queue is empty.
  folly::Optional<T> try_take() {
    if (!sem_.try_wait()) {
      return none;
    }
    return dequeue();
  }

  /// Remove and return an item, waiting up to the given duration.
  ///
  /// \param time The maximum time to wait for an item.
  /// \returns The dequeued item, or none if the timeout elapsed.
  folly::Optional<T> try_take_for(std::chrono::milliseconds time) override {
    if (!sem_.try_wait_for(time)) {
      return none;
    }
    return dequeue();
  }

  /// Return an approximate count of items in the queue.
  ///
  /// \returns An estimate of the number of queued items.
  size_t size() override { return sem_.valueGuess(); }

 private:
  size_t translatePriority(int8_t const priority) {
    size_t const priorities = queue_.priorities();
    assert(priorities <= 255);
    int8_t const hi = (priorities + 1) / 2 - 1;
    int8_t const lo = hi - (priorities - 1);
    return hi - constexpr_clamp(priority, lo, hi);
  }

  T dequeue() {
    // must follow a successful sem wait
    if (auto obj = queue_.try_dequeue()) {
      return std::move(*obj);
    }
    terminate_with<std::logic_error>("bug in task queue");
  }

  Semaphore sem_;
  PriorityUMPMCQueueSet<T, /* MayBlock = */ true> queue_;
};

} // namespace folly
