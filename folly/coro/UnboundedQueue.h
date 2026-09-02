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

#include <folly/concurrency/UnboundedQueue.h>
#include <folly/coro/Coroutine.h>
#include <folly/coro/Task.h>
#include <folly/fibers/Semaphore.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

/// Wrapper around folly::UnboundedQueue that supports async dequeue.
template <
    typename T,
    bool SingleProducer = false,
    bool SingleConsumer = false,
    bool MayBlock = false,
    size_t LgSegmentSize = 8>
class UnboundedQueue {
 public:
  /// Enqueues a value onto the queue.
  ///
  /// @param val the value to enqueue
  template <typename U = T>
  void enqueue(U&& val) {
    queue_.enqueue(std::forward<U>(val));
    sem_.signal();
  }

  /// Dequeues a value from the queue, suspending until one is available.
  ///
  /// Note that this operation can be safely cancelled by requesting cancellation
  /// on the awaiting coroutine's associated CancellationToken.
  /// If the operation is successfully cancelled then it will complete with
  /// an error of type `folly::OperationCancelled`.
  /// WARNING: It is not safe to wrap this with folly::coro::timeout(). Wrap with
  /// folly::coro::timeoutNoDiscard(), or use co_try_dequeue_for() instead.
  ///
  /// @returns a Task yielding the dequeued value
  folly::coro::Task<T> dequeue() {
    folly::Try<void> result = co_await folly::coro::co_awaitTry(sem_.co_wait());
    if (result.hasException()) {
      co_yield co_error(std::move(result).exception());
    }

    co_return queue_.dequeue();
  }

  /// Tries to dequeue a value from the queue within a timeout.
  ///
  /// The operation will either successfully dequeue an item from the queue, or
  /// else be cancelled and complete with an error of type
  /// `folly::OperationCancelled`.
  ///
  /// @param timeout the maximum duration to wait for a value
  /// @returns a Task yielding the dequeued value, or an error on timeout
  template <typename Duration>
  folly::coro::Task<T> co_try_dequeue_for(Duration timeout) {
    folly::Try<void> result =
        co_await folly::coro::co_awaitTry(sem_.co_try_wait_for(timeout));
    if (result.hasException()) {
      co_yield co_error(std::move(result).exception());
    }

    co_return queue_.dequeue();
  }

  /// Dequeues a value into an output reference, suspending until available.
  ///
  /// @param out the reference to receive the dequeued value
  /// @returns a Task that completes once a value has been dequeued
  folly::coro::Task<void> dequeue(T& out) {
    co_await sem_.co_wait();
    queue_.dequeue(out);
  }

  /// Tries to dequeue a value without blocking.
  ///
  /// @returns the dequeued value, or `folly::none` if the queue is empty
  folly::Optional<T> try_dequeue() {
    return sem_.try_wait() ? queue_.try_dequeue() : folly::none;
  }

  /// Tries to dequeue a value into an output reference without blocking.
  ///
  /// @param out the reference to receive the dequeued value
  /// @returns true if a value was dequeued, false if the queue is empty
  bool try_dequeue(T& out) {
    return sem_.try_wait() ? queue_.try_dequeue(out) : false;
  }

  /// Reports whether the queue is empty.
  ///
  /// @returns true if the queue has no elements
  bool empty() const { return queue_.empty(); }

  /// Returns a pointer to the next element without dequeuing it.
  ///
  /// @returns a pointer to the front element, or null if the queue is empty
  const T* try_peek() noexcept { return queue_.try_peek(); }

  /// Returns the number of elements in the queue.
  ///
  /// @returns the number of queued elements
  size_t size() const { return queue_.size(); }

 private:
  folly::UnboundedQueue< //
      T,
      SingleProducer,
      SingleConsumer,
      MayBlock,
      LgSegmentSize>
      queue_;
  folly::fibers::Semaphore sem_{0};
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
