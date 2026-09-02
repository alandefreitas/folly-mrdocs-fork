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

#include <folly/Executor.h>
#include <folly/MPMCQueue.h>
#include <folly/Range.h>
#include <folly/executors/task_queue/BlockingQueue.h>
#include <folly/synchronization/LifoSem.h>

namespace folly {

/// A blocking queue with priority levels backed by per-priority MPMC queues
/// and a LIFO semaphore.
template <
    class T,
    QueueBehaviorIfFull kBehavior = QueueBehaviorIfFull::THROW,
    class Semaphore = folly::LifoSem>
class PriorityLifoSemMPMCQueue : public BlockingQueue<T> {
 public:
  /// Constructs the queue with a uniform capacity for each priority.
  ///
  /// Note A: The queue pre-allocates all memory for max_capacity.
  /// Note B: To use folly::Executor::*_PRI, for numPriorities == 2
  ///         MID_PRI and HI_PRI are treated at the same priority level.
  ///
  /// \param numPriorities The number of priority levels.
  /// \param max_capacity The capacity of each priority queue.
  /// \param semaphoreOptions Options for the underlying semaphore.
  PriorityLifoSemMPMCQueue(
      uint8_t numPriorities,
      size_t max_capacity,
      const typename Semaphore::Options& semaphoreOptions = {})
      : sem_(semaphoreOptions) {
    CHECK_GT(numPriorities, 0) << "Number of priorities should be positive";
    queues_.reserve(numPriorities);
    for (int8_t i = 0; i < numPriorities; i++) {
      queues_.emplace_back(max_capacity);
    }
  }

  /// Constructs the queue with a per-priority capacity.
  ///
  /// \param capacities The capacity of each priority queue; its size sets the
  /// number of priorities.
  /// \param semaphoreOptions Options for the underlying semaphore.
  PriorityLifoSemMPMCQueue(
      folly::Range<const size_t*> capacities,
      const typename Semaphore::Options& semaphoreOptions = {})
      : sem_(semaphoreOptions) {
    CHECK_GT(capacities.size(), 0) << "Number of priorities should be positive";
    CHECK_LT(capacities.size(), 256) << "At most 255 priorities supported";

    queues_.reserve(capacities.size());
    for (auto capacity : capacities) {
      queues_.emplace_back(capacity);
    }
  }

  /// Returns the number of priority levels supported by the queue.
  ///
  /// \returns The number of priority levels.
  uint8_t getNumPriorities() override { return queues_.size(); }

  /// Adds an item to the queue at medium priority.
  ///
  /// \param item The item to add.
  /// \returns The result of the add operation.
  BlockingQueueAddResult add(T&& item) override {
    return addWithPriority(std::move(item), folly::Executor::MID_PRI);
  }

  /// Adds an item to the queue at the given priority.
  ///
  /// \param item The item to add.
  /// \param priority The priority level for the item.
  /// \returns The result of the add operation.
  BlockingQueueAddResult addWithPriority(T&& item, int8_t priority) override {
    int mid = getNumPriorities() / 2;
    size_t queue = priority < 0
        ? std::max(0, mid + priority)
        : std::min(getNumPriorities() - 1, mid + priority);
    CHECK_LT(queue, queues_.size());
    switch (kBehavior) { // static
      case QueueBehaviorIfFull::THROW:
        if (!queues_[queue].writeIfNotFull(std::move(item))) {
          throw QueueFullException("LifoSemMPMCQueue full, can't add item");
        }
        break;
      case QueueBehaviorIfFull::BLOCK:
        queues_[queue].blockingWrite(std::move(item));
        break;
    }
    return sem_.post();
  }

  /// Removes an item, blocking until one is available.
  ///
  /// \returns The removed item.
  T take() override {
    sem_.wait();
    T item;
    while (true) {
      if (nonBlockingTake(item)) {
        return item;
      }
    }
  }

  /// Removes an item, waiting up to the given timeout for one to be available.
  ///
  /// \param time The maximum time to wait for an item.
  /// \returns The removed item, or folly::none if the timeout elapsed.
  folly::Optional<T> try_take_for(std::chrono::milliseconds time) override {
    if (!sem_.try_wait_for(time)) {
      return folly::none;
    }
    T item;
    while (true) {
      if (nonBlockingTake(item)) {
        return item;
      }
    }
  }

  /// Attempts to remove an item without blocking, scanning highest priority
  /// first.
  ///
  /// \param item Set to the removed item on success.
  /// \returns True if an item was removed.
  bool nonBlockingTake(T& item) {
    for (auto it = queues_.rbegin(); it != queues_.rend(); it++) {
      if (it->readIfNotEmpty(item)) {
        return true;
      }
    }
    return false;
  }

  /// Returns an approximate count of items in the queue.
  ///
  /// \returns The estimated number of items across all priority queues.
  size_t size() override { return sem_.valueGuess(); }

 private:
  Semaphore sem_;
  std::vector<folly::MPMCQueue<T>> queues_;
};

} // namespace folly
