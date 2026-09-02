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

#include <folly/IntrusiveList.h>
#include <folly/Synchronized.h>
#include <folly/coro/Task.h>
#include <folly/coro/Timeout.h>
#include <folly/fibers/Baton.h>
#include <folly/futures/Future.h>

#include <deque>

namespace folly {
namespace fibers {

/**
 * Fiber-compatible semaphore. Will safely block fibers that wait when no
 * tokens are available and wake fibers when signalled.
 *
 * Fair. Waiters are awoken in FIFO order. (Note: whether the callers see FIFO
 * order depends on the executors, wrapping async types, and the existence of
 * happens-before relationships between async wait operations.)
 */
class Semaphore {
 public:
  /// Construct a semaphore holding the given number of tokens.
  ///
  /// @param tokenCount The initial and maximum number of tokens.
  explicit Semaphore(size_t tokenCount)
      : capacity_(tokenCount), tokens_(int64_t(capacity_)) {}

  /// Deleted copy constructor.
  ///
  /// @param other The semaphore to copy from.
  Semaphore(const Semaphore& other) = delete;

  /// Deleted move constructor.
  ///
  /// @param other The semaphore to move from.
  Semaphore(Semaphore&& other) = delete;

  /// Deleted copy assignment.
  ///
  /// @param other The semaphore to assign from.
  /// @return Reference to this semaphore.
  Semaphore& operator=(const Semaphore& other) = delete;

  /// Deleted move assignment.
  ///
  /// @param other The semaphore to move from.
  /// @return Reference to this semaphore.
  Semaphore& operator=(Semaphore&& other) = delete;

  /// Release a token in the semaphore. Signal the waiter if necessary.
  void signal();

  /// Wait for capacity in the semaphore.
  void wait();

  /// A queued waiter that is notified when the semaphore acquires capacity.
  struct Waiter {
    /// Construct an unqueued waiter.
    Waiter() noexcept {}

    /// The baton will be signalled when this waiter acquires the semaphore.
    Baton baton;

   private:
    friend Semaphore;
    folly::SafeIntrusiveListHook hook_;
  };

  /**
   * Try to wait on the semaphore.
   * Return true on success.
   * On failure, the passed waiter is enqueued, its baton will be posted once
   * semaphore has capacity. Caller is responsible to wait then signal.
   *
   * @param waiter The waiter enqueued when no token is immediately available.
   * @return true if a token was acquired immediately, false otherwise.
   */
  bool try_wait(Waiter& waiter);

  /**
   * If the semaphore has capacity, removes a token and returns true. Otherwise
   * returns false and leaves the semaphore unchanged.
   *
   * @return true if a token was acquired, false otherwise.
   */
  bool try_wait();

#if FOLLY_HAS_COROUTINES

  /// Wait for capacity in the semaphore.
  ///
  /// Note that this wait-operation can be cancelled by requesting cancellation
  /// on the awaiting coroutine's associated CancellationToken.
  /// If the operation is successfully cancelled then it will complete with
  /// an error of type folly::OperationCancelled.
  ///
  /// Note that requesting cancellation of the operation will only have an
  /// effect if the operation does not complete synchronously (ie. was not
  /// already in a signalled state).
  ///
  /// If the semaphore was already in a signalled state prior to awaiting the
  /// returned Task then the operation will complete successfully regardless
  /// of whether cancellation was requested.
  ///
  /// @return A task that completes once a token has been acquired.
  coro::Task<void> co_wait();

  /// Wait for capacity in the semaphore with a timeout.
  ///
  /// Same as co_wait() but with a timeout.
  ///
  /// The timeout just requests cancellation after a timer expires. It has the
  /// same effect as requesting cancellation.
  ///
  /// If cancellation or timeout happen after the semaphore was already in a
  /// signalled state, no exception will be thrown, and the method will just
  /// return as if no cancellation or timeout happened.
  ///
  /// @param timeout The maximum duration to wait for a token.
  /// @return A task that completes once a token has been acquired or the
  /// timeout elapses.
  template <typename Duration>
  coro::Task<void> co_try_wait_for(Duration timeout) {
    return folly::coro::timeoutNoDiscard(co_wait(), timeout);
  }

#endif

  /// Wait for capacity in the semaphore.
  ///
  /// @return A future that completes once a token has been acquired.
  SemiFuture<Unit> future_wait();

  /// Return the total number of tokens the semaphore was created with.
  ///
  /// @return The semaphore capacity.
  size_t getCapacity() const;

  /// Return the number of tokens currently available.
  ///
  /// @return The number of unclaimed tokens.
  size_t getAvailableTokens() const;

 private:
  bool waitSlow(Waiter& waiter);
  bool signalSlow();

  size_t capacity_;
  // Atomic counter
  std::atomic<int64_t> tokens_;

  using WaiterList = folly::SafeIntrusiveList<Waiter, &Waiter::hook_>;

  folly::Synchronized<WaiterList> waitList_;
};

} // namespace fibers
} // namespace folly
