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
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

#include <glog/logging.h>
#include <folly/Likely.h>
#include <folly/lang/Bits.h>

/// Test helper granting access to DynamicRingQueue internals.
template <typename T>
class DynamicRingQueueTestHelper;

namespace folly {

/**
 * A ring buffer queue backed by a power-of-two sized array that grows
 * on overflow. Not thread-safe.
 */
template <typename T>
class DynamicRingQueue {
 public:
  /// The maximum capacity the queue can grow to.
  static constexpr uint32_t kMaxCapacity = 1u << 31;

  /// Constructs an empty queue with no allocated buffer.
  DynamicRingQueue() = default;

  /// Constructs a queue with room for at least the given capacity.
  ///
  /// \param capacity The requested initial capacity.
  explicit DynamicRingQueue(uint32_t capacity) {
    if (capacity > kMaxCapacity) {
      throw std::length_error("DynamicRingQueue capacity exceeds maximum");
    }
    mask_ = folly::nextPowTwo(capacity) - 1;
    buf_ = std::make_unique<T[]>(mask_ + 1);
  }

  /// Destroys the queue and releases its buffer.
  ~DynamicRingQueue() = default;

  /// Copy construction is disabled.
  ///
  /// \param other The queue that would be copied.
  DynamicRingQueue(const DynamicRingQueue& other) = delete;
  /// Copy assignment is disabled.
  ///
  /// \param other The queue that would be copied.
  /// \returns A reference to this queue.
  DynamicRingQueue& operator=(const DynamicRingQueue& other) = delete;

  /// Move-constructs the queue, leaving the source empty.
  ///
  /// \param other The queue to move from.
  DynamicRingQueue(DynamicRingQueue&& other) noexcept
      : mask_(std::exchange(other.mask_, ~0u)),
        buf_(std::move(other.buf_)),
        head_(std::exchange(other.head_, 0)),
        tail_(std::exchange(other.tail_, 0)) {}

  /// Move-assigns the queue, leaving the source empty.
  ///
  /// \param other The queue to move from.
  /// \returns A reference to this queue.
  DynamicRingQueue& operator=(DynamicRingQueue&& other) noexcept {
    mask_ = std::exchange(other.mask_, ~0u);
    buf_ = std::move(other.buf_);
    head_ = std::exchange(other.head_, 0);
    tail_ = std::exchange(other.tail_, 0);
    return *this;
  }

  /// Appends an element to the back of the queue, growing if full.
  ///
  /// \param val The value to append.
  void push(T val) {
    if (FOLLY_UNLIKELY(size() >= capacity())) {
      grow();
    }
    buf_[tail_++ & mask_] = val;
  }

  /// Removes and returns the element at the front of the queue.
  ///
  /// \returns The popped element.
  T pop() {
    DCHECK(!empty());
    return buf_[head_++ & mask_];
  }

  /// Returns the number of elements currently stored in the queue.
  ///
  /// \returns The element count.
  uint32_t size() const { return tail_ - head_; }
  /// Returns the number of elements the queue can hold without growing.
  ///
  /// \returns The current capacity.
  uint32_t capacity() const { return mask_ + 1; }
  /// Returns the maximum capacity the queue can grow to.
  ///
  /// \returns The maximum capacity.
  uint32_t max_size() const noexcept { return kMaxCapacity; }
  /// Returns whether the queue holds no elements.
  ///
  /// \returns `true` if the queue is empty.
  bool empty() const { return head_ == tail_; }

 private:
  void grow() {
    uint32_t oldCap = capacity();
    if (oldCap >= kMaxCapacity) {
      throw std::length_error("DynamicRingQueue capacity exceeds maximum");
    }
    uint32_t newCap = oldCap == 0 ? 1 : oldCap * 2;
    auto newBuf = std::make_unique<T[]>(newCap);

    uint32_t n = size();
    uint32_t head = head_ & mask_;
    uint32_t headLen = std::min(n, oldCap - head);
    auto* src = buf_.get();
    auto* dst = newBuf.get();
    std::copy(src + head, src + head + headLen, dst);
    std::copy(src, src + (n - headLen), dst + headLen);

    buf_ = std::move(newBuf);
    mask_ = newCap - 1;
    head_ = 0;
    tail_ = n;
  }

  template <typename U>
  friend class ::DynamicRingQueueTestHelper;

  uint32_t mask_{~0u};
  std::unique_ptr<T[]> buf_{nullptr};
  uint32_t head_{0};
  uint32_t tail_{0};
};

} // namespace folly
