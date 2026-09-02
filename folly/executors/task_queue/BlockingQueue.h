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
#include <exception>
#include <stdexcept>

#include <glog/logging.h>

#include <folly/CPortability.h>
#include <folly/Optional.h>

namespace folly {

/// Behavior of a queue when an item is added while the queue is full.
///
/// Some queue implementations (for example, LifoSemMPMCQueue or
/// PriorityLifoSemMPMCQueue) support both blocking (BLOCK) and
/// non-blocking (THROW) behaviors.
enum class QueueBehaviorIfFull {
  THROW, ///< Throw QueueFullException instead of blocking.
  BLOCK, ///< Block the caller until space is available.
};

/// Exception thrown when adding to a full queue that uses THROW behavior.
class FOLLY_EXPORT QueueFullException : public std::runtime_error {
  using std::runtime_error::runtime_error; // Inherit constructors.
};

/// Result of adding an item to a BlockingQueue.
struct BlockingQueueAddResult {
  /// Construct a result.
  ///
  /// \param reused Whether an existing thread was reused for the added item.
  BlockingQueueAddResult(bool reused = false) : reusedThread(reused) {}

  /// True if an existing thread was reused to work on the added item.
  bool reusedThread;
};

/// Abstract interface for a queue that can block callers until items are
/// available.
template <class T>
class BlockingQueue {
 public:
  /// Destroy the queue.
  virtual ~BlockingQueue() = default;

  /// Adds item to the queue.
  ///
  /// Returns true if an existing thread was able to work on it (used
  /// for dynamically sizing thread pools), false otherwise.  Return false
  /// if this feature is not supported.
  ///
  /// \param item Item to add to the queue.
  /// \returns Result reporting whether an existing thread picked up the item.
  virtual BlockingQueueAddResult add(T&& item) = 0;

  /// Adds item to the queue with the given priority.
  ///
  /// \param item Item to add to the queue.
  /// \param priority Priority to enqueue the item with.
  /// \returns Result reporting whether an existing thread picked up the item.
  virtual BlockingQueueAddResult addWithPriority(
      T&& item, int8_t priority) {
    return add(std::move(item));
  }

  /// Returns the number of priority levels supported by the queue.
  ///
  /// \returns Number of supported priority levels.
  virtual uint8_t getNumPriorities() { return 1; }

  /// Removes and returns an item from the queue, blocking until one is
  /// available.
  ///
  /// \returns The dequeued item.
  virtual T take() = 0;

  /// Removes and returns an item from the queue, waiting up to the given time.
  ///
  /// \param time Maximum duration to wait for an item.
  /// \returns The dequeued item, or empty if the wait timed out.
  virtual folly::Optional<T> try_take_for(std::chrono::milliseconds time) = 0;

  /// Returns the number of items currently in the queue.
  ///
  /// \returns Current queue size.
  virtual size_t size() = 0;
};

} // namespace folly
