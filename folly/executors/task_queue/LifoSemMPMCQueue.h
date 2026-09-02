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

#include <folly/MPMCQueue.h>
#include <folly/executors/task_queue/BlockingQueue.h>
#include <folly/synchronization/LifoSem.h>

namespace folly {

/// A blocking multi-producer, multi-consumer queue backed by a LIFO semaphore.
///
/// Wraps an `MPMCQueue` and a semaphore so consumers can block until an item
/// is available. The behavior when the queue is full is selected by
/// `kBehavior`.
///
/// \tparam T Type of the queued items.
/// \tparam kBehavior What to do when adding to a full queue: throw or block.
/// \tparam Semaphore Semaphore type used to signal available items.
template <
    class T,
    QueueBehaviorIfFull kBehavior = QueueBehaviorIfFull::THROW,
    class Semaphore = folly::LifoSem>
class LifoSemMPMCQueue : public BlockingQueue<T> {
 public:
  /// Constructs the queue, pre-allocating all memory for `max_capacity`.
  ///
  /// \param max_capacity Maximum number of items the queue can hold.
  /// \param semaphoreOptions Options forwarded to the semaphore.
  explicit LifoSemMPMCQueue(
      size_t max_capacity,
      const typename Semaphore::Options& semaphoreOptions = {})
      : sem_(semaphoreOptions), queue_(max_capacity) {}

  /// Adds an item to the queue and signals a waiting consumer.
  ///
  /// \param item The item to move into the queue.
  /// \returns The result of posting to the semaphore.
  BlockingQueueAddResult add(T&& item) override {
    switch (kBehavior) { // static
      case QueueBehaviorIfFull::THROW:
        if (!queue_.writeIfNotFull(std::move(item))) {
          throw QueueFullException("LifoSemMPMCQueue full, can't add item");
        }
        break;
      case QueueBehaviorIfFull::BLOCK:
        queue_.blockingWrite(std::move(item));
        break;
    }
    return sem_.post();
  }

  /// Blocks until an item is available, then removes and returns it.
  ///
  /// \returns The item taken from the queue.
  T take() override {
    sem_.wait();
    T item;
    while (!queue_.readIfNotEmpty(item)) {
    }
    return item;
  }

  /// Waits up to `time` for an item, then removes and returns it if available.
  ///
  /// \param time Maximum duration to wait for an item.
  /// \returns The item if one became available, or `folly::none` on timeout.
  folly::Optional<T> try_take_for(std::chrono::milliseconds time) override {
    if (!sem_.try_wait_for(time)) {
      return folly::none;
    }
    T item;
    while (!queue_.readIfNotEmpty(item)) {
    }
    return item;
  }

  /// Returns the maximum number of items the queue can hold.
  ///
  /// \returns The queue capacity.
  size_t capacity() { return queue_.capacity(); }

  /// Returns an approximate count of items currently in the queue.
  ///
  /// \returns An estimate of the number of queued items.
  size_t size() override { return sem_.valueGuess(); }

 private:
  Semaphore sem_;
  folly::MPMCQueue<T> queue_;
};

} // namespace folly
