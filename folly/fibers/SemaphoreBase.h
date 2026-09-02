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
#include <folly/fibers/Baton.h>
#include <folly/futures/Future.h>
#if FOLLY_HAS_COROUTINES
#include <folly/coro/Task.h>
#endif

#include <deque>

namespace folly {
namespace fibers {

/// Fiber-compatible semaphore base. Will safely block fibers that wait when no
/// tokens are available and wake fibers when signalled.
class SemaphoreBase {
 public:
  /// Constructs the semaphore with the given number of tokens.
  /// @param tokenCount Initial token capacity of the semaphore.
  explicit SemaphoreBase(size_t tokenCount)
      : capacity_(tokenCount), tokens_(int64_t(capacity_)) {}

  /// Deleted copy constructor.
  /// @param other The semaphore that would be copied.
  SemaphoreBase(const SemaphoreBase& other) = delete;

  /// Deleted move constructor.
  /// @param other The semaphore that would be moved.
  SemaphoreBase(SemaphoreBase&& other) = delete;

  /// Deleted copy assignment operator.
  /// @param other The semaphore that would be copied.
  /// @returns A reference to this semaphore.
  SemaphoreBase& operator=(const SemaphoreBase& other) = delete;

  /// Deleted move assignment operator.
  /// @param other The semaphore that would be moved.
  /// @returns A reference to this semaphore.
  SemaphoreBase& operator=(SemaphoreBase&& other) = delete;

  /// A queued waiter blocked until the semaphore has capacity.
  struct Waiter {
    /// Constructs a waiter requesting the given number of tokens.
    /// @param tokens Number of tokens this waiter requests.
    explicit Waiter(int64_t tokens = 1) noexcept : tokens_{tokens} {}

    /// The baton will be signalled when this waiter acquires the semaphore.
    Baton baton;

   private:
    friend SemaphoreBase;
    folly::SafeIntrusiveListHook hook_;
    int64_t tokens_;
  };

  /// Returns the total number of tokens the semaphore can hold.
  /// @returns The semaphore capacity.
  size_t getCapacity() const;

  /// Returns the number of tokens currently available.
  /// @returns The count of available tokens.
  size_t getAvailableTokens() const;

 protected:
  /// Wait for request capacity in the semaphore.
  /// @param tokens Number of tokens to acquire.
  void wait_common(int64_t tokens);

  /**
   * Try to wait on the semaphore.
   * Return true on success.
   * On failure, the passed waiter is enqueued, its baton will be posted once
   * semaphore has capacity. Caller is responsible to wait then signal.
   *
   * @param waiter The waiter to enqueue on failure.
   * @param tokens Number of tokens to acquire.
   * @returns `true` on success, `false` if the waiter was enqueued.
   */
  bool try_wait_common(Waiter& waiter, int64_t tokens);

  /**
   * If the semaphore has capacity, removes a token and returns true. Otherwise
   * returns false and leaves the semaphore unchanged.
   *
   * @param tokens Number of tokens to acquire.
   * @returns `true` if the tokens were acquired, `false` otherwise.
   */
  bool try_wait_common(int64_t tokens);

#if FOLLY_HAS_COROUTINES

  /**
   * Wait for request capacity in the semaphore.
   *
   * Note that this wait-operation can be cancelled by requesting cancellation
   * on the awaiting coroutine's associated CancellationToken.
   * If the operation is successfully cancelled then it will complete with
   * an error of type folly::OperationCancelled.
   *
   * Note that requesting cancellation of the operation will only have an
   * effect if the operation does not complete synchronously (ie. was not
   * already in a signalled state).
   *
   * If the semaphore was already in a signalled state prior to awaiting the
   * returned Task then the operation will complete successfully regardless
   * of whether cancellation was requested.
   *
   * @param tokens Number of tokens to acquire.
   * @returns A coroutine task that completes once the tokens are acquired.
   */
  coro::Task<void> co_wait_common(int64_t tokens);

#endif

  /// Wait for request capacity in the semaphore, returning a future.
  /// @param tokens Number of tokens to acquire.
  /// @returns A future that completes once the tokens are acquired.
  SemiFuture<Unit> future_wait_common(int64_t tokens);

  /// Slow path that enqueues the waiter when tokens are unavailable.
  /// @param waiter The waiter to enqueue while waiting for capacity.
  /// @param tokens Number of tokens the waiter requests.
  /// @returns `true` if the tokens were acquired without waiting.
  bool waitSlow(Waiter& waiter, int64_t tokens);

  /// Slow path that wakes queued waiters when tokens become available.
  /// @param tokens Number of tokens being returned to the semaphore.
  /// @returns `true` if a waiter was signalled.
  bool signalSlow(int64_t tokens);

  size_t capacity_; ///< Total number of tokens the semaphore can hold.
  std::atomic<int64_t> tokens_; ///< Atomic counter of currently available tokens.

  /// List of waiters queued for capacity.
  using WaiterList = folly::SafeIntrusiveList<Waiter, &Waiter::hook_>;

  folly::Synchronized<WaiterList> waitList_; ///< Synchronized queue of pending waiters.
};

} // namespace fibers
} // namespace folly
