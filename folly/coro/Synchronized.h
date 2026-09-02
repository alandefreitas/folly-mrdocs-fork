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

#include <functional>
#include <mutex>
#include <utility>

#include <folly/Utility.h>
#include <folly/coro/SharedLock.h>
#include <folly/coro/SharedMutex.h>
#include <folly/coro/Task.h>
#include <folly/coro/Traits.h>

namespace folly::coro {

namespace detail {

template <typename CoroMutexType>
struct SynchronizedMutexTraits;

template <>
struct SynchronizedMutexTraits<SharedMutexFair> {
  using CoroMutex = SharedMutexFair;
  using ReadLock = SharedLock<CoroMutex>;
  using WriteLock = std::unique_lock<CoroMutex>;

  static inline auto co_readLock(CoroMutex& mutex) {
    return mutex.co_scoped_lock_shared();
  }

  static inline ReadLock tryReadLock(CoroMutex& mutex) noexcept(
      noexcept(ReadLock(mutex, std::try_to_lock))) {
    return ReadLock(mutex, std::try_to_lock);
  }

  static inline auto co_writeLock(CoroMutex& mutex) {
    return mutex.co_scoped_lock();
  }

  static inline auto tryWriteLock(CoroMutex& mutex) noexcept(
      noexcept(WriteLock(mutex, std::try_to_lock))) {
    return WriteLock(mutex, std::try_to_lock);
  }

  static inline void unlock(ReadLock& lock) noexcept(noexcept(lock.unlock())) {
    lock.unlock();
  }

  static inline void unlock(WriteLock& lock) noexcept(noexcept(lock.unlock())) {
    lock.unlock();
  }

  static inline auto ownsLock(const ReadLock& lock) noexcept(
      noexcept(lock.owns_lock())) {
    return lock.owns_lock();
  }

  static inline auto ownsLock(const WriteLock& lock) noexcept(
      noexcept(lock.owns_lock())) {
    return lock.owns_lock();
  }
};

} // namespace detail

/**
 * This class is an adaptation of the folly::Synchronized class but is designed
 * to work with coro-compatible mutexes like coro::SharedMutexFair instead.
 *
 * In practice what this means is
 * that we can co_await gaining the read/write lock rather than blocking whilst
 * acquiring it.
 *
 * The API is not a complete clone of everything that folly::Synchronized
 * supports but is instead the minimum of what we need. Ultimately this classes
 * main job is to abstract away gaining the locks.
 */
template <
    typename Inner,
    typename CoroMutexType = SharedMutexFair,
    typename CoroMutexTraits = detail::SynchronizedMutexTraits<CoroMutexType>>
class Synchronized : public NonCopyableNonMovable {
 public:
  /// The traits describing how to lock and unlock the coro-compatible mutex.
  using Traits = CoroMutexTraits;
  /// The coro-compatible mutex type guarding the object.
  using CoroMutex = typename Traits::CoroMutex;
  /// The lock type used for shared (read) access.
  using ReadLock = typename Traits::ReadLock;
  /// The lock type used for exclusive (write) access.
  using WriteLock = typename Traits::WriteLock;

  /// Default-constructs the guarded object.
  Synchronized() noexcept(noexcept(CoroMutex{}) && noexcept(Inner{})) = default;

  /// Constructs the guarded object by copying from `rhs`.
  ///
  /// @param rhs The value to copy into the guarded object.
  explicit Synchronized(const Inner& rhs) noexcept(noexcept(Inner(rhs)))
      : inner_(rhs) {}

  /// Constructs the guarded object by moving from `rhs`.
  ///
  /// @param rhs The value to move into the guarded object.
  explicit Synchronized(Inner&& rhs) noexcept(noexcept(Inner(std::move(rhs))))
      : inner_(std::move(rhs)) {}

  /// Constructs the guarded object in place from the given arguments.
  ///
  /// @param tag An in-place construction tag (unused).
  /// @param args The arguments forwarded to the guarded object's constructor.
  template <typename... Args>
  explicit Synchronized(std::in_place_t tag, Args&&... args)
      : inner_(std::forward<Args>(args)...) {}

  /**
   * A RAII wrapper around a pointer to the underlying object together with
   * a lock on the underlying mutex.
   *
   * If acquired with a try-lock style method, you must check the boolean
   * value of the locked pointer before dereferencing it.
   */
  template <typename ValueType, typename LockType>
  class GenericLockedPtr : public MoveOnly {
   public:
    /// Move-constructs from another locked pointer, taking over its lock.
    ///
    /// @param other The locked pointer to move from.
    GenericLockedPtr(GenericLockedPtr&& other) noexcept(
        noexcept(LockType(std::move(other.lock_))))
        : lock_(std::move(other.lock_)),
          ptr_(std::exchange(other.ptr_, nullptr)) {}

    /// Move-assigns from another locked pointer, releasing any current lock.
    ///
    /// @param other The locked pointer to move from.
    /// @returns A reference to this locked pointer.
    GenericLockedPtr& operator=(GenericLockedPtr&& other) noexcept(
        noexcept(lock_ = std::move(other.lock_))) {
      if (this != &other) {
        lock_ = std::move(other.lock_);
        ptr_ = std::exchange(other.ptr_, nullptr);
      }
      return *this;
    }

    /// Returns a pointer to the guarded object.
    ///
    /// @returns A pointer to the underlying object.
    ValueType* operator->() const noexcept {
      DCHECK_NE(ptr_, nullptr);
      return ptr_;
    }

    /// Returns a reference to the guarded object.
    ///
    /// @returns A reference to the underlying object.
    ValueType& operator*() const noexcept {
      DCHECK_NE(ptr_, nullptr);
      return *ptr_;
    }

    /// Releases the lock and invalidates this pointer.
    void unlock() {
      DCHECK_NE(ptr_, nullptr);
      ptr_ = nullptr;
      Traits::unlock(lock_);
    }

    /// Returns whether this pointer currently owns the lock.
    ///
    /// @returns `true` if the underlying lock is held, otherwise `false`.
    explicit operator bool() const noexcept { return Traits::ownsLock(lock_); }

   private:
    friend class Synchronized;
    explicit GenericLockedPtr(LockType&& lock, ValueType* ptr)
        : lock_(std::move(lock)), ptr_(ptr) {}

    LockType lock_;
    ValueType* ptr_ = nullptr;
  };

  /// A locked pointer granting read-only access to the guarded object.
  using ReadLockedPtr = GenericLockedPtr<const Inner, ReadLock>;
  /// A locked pointer granting read-write access to the guarded object.
  using WriteLockedPtr = GenericLockedPtr<Inner, WriteLock>;

  /// Asynchronously acquires the write lock.
  ///
  /// @returns A task producing a write-locked pointer to the guarded object.
  Task<WriteLockedPtr> wLock() {
    auto lock = co_await Traits::co_writeLock(mutex_);
    co_return WriteLockedPtr{std::move(lock), &inner_};
  }

  /// Asynchronously acquires the read lock.
  ///
  /// @returns A task producing a read-locked pointer to the guarded object.
  Task<ReadLockedPtr> rLock() const {
    auto lock = co_await Traits::co_readLock(mutex_);
    co_return ReadLockedPtr{std::move(lock), &inner_};
  }

  /// Attempts to acquire the read lock without blocking.
  ///
  /// @returns A read-locked pointer that is engaged only if the lock was
  /// acquired.
  ReadLockedPtr tryRLock() const {
    auto lock = Traits::tryReadLock(mutex_);
    auto* ptr = Traits::ownsLock(lock) ? &inner_ : nullptr;
    return ReadLockedPtr{std::move(lock), ptr};
  }

  /// Attempts to acquire the write lock without blocking.
  ///
  /// @returns A write-locked pointer that is engaged only if the lock was
  /// acquired.
  WriteLockedPtr tryWLock() {
    auto lock = WriteLock{mutex_, std::try_to_lock};
    auto* ptr = Traits::ownsLock(lock) ? &inner_ : nullptr;
    return WriteLockedPtr{std::move(lock), ptr};
  }

  /// The result of invoking `FuncT` with a read-locked pointer.
  template <typename FuncT>
  using rlock_result_t = invoke_result_t<FuncT, ReadLockedPtr>;

  /// The result of invoking `FuncT` with a write-locked pointer.
  template <typename FuncT>
  using wlock_result_t = invoke_result_t<FuncT, WriteLockedPtr>;

  /// Acquires the read lock and invokes `func` with a read-locked pointer.
  ///
  /// @param func A callable invoked with a read-locked pointer while the lock
  /// is held.
  /// @returns A task producing the result of `func`.
  template <typename FuncT, typename ReturnT = rlock_result_t<FuncT>>
  typename std::enable_if<!is_semi_awaitable_v<ReturnT>, Task<ReturnT>>::type
  withRLock(FuncT func) const {
    auto lock = co_await Traits::co_readLock(mutex_);
    co_return func(ReadLockedPtr{std::move(lock), &inner_});
  }

  /// Acquires the read lock and awaits the awaitable that `func` returns.
  ///
  /// @param func A callable invoked with a read-locked pointer whose result
  /// is awaited while the lock is held.
  /// @returns A task producing the awaited result of `func`.
  template <typename FuncT, typename ReturnT = rlock_result_t<FuncT>>
  typename std::enable_if<
      is_semi_awaitable_v<ReturnT>,
      Task<semi_await_result_t<ReturnT>>>::type
  withRLock(FuncT func) const {
    auto lock = co_await Traits::co_readLock(mutex_);
    co_return co_await func(ReadLockedPtr{std::move(lock), &inner_});
  }

  /// Acquires the write lock and invokes `func` with a write-locked pointer.
  ///
  /// @param func A callable invoked with a write-locked pointer while the
  /// lock is held.
  /// @returns A task producing the result of `func`.
  template <typename FuncT, typename ReturnT = wlock_result_t<FuncT>>
  typename std::enable_if<!is_semi_awaitable_v<ReturnT>, Task<ReturnT>>::type
  withWLock(FuncT func) {
    auto lock = co_await Traits::co_writeLock(mutex_);
    co_return func(WriteLockedPtr{std::move(lock), &inner_});
  }

  /// Acquires the write lock and awaits the awaitable that `func` returns.
  ///
  /// @param func A callable invoked with a write-locked pointer whose result
  /// is awaited while the lock is held.
  /// @returns A task producing the awaited result of `func`.
  template <typename FuncT, typename ReturnT = wlock_result_t<FuncT>>
  typename std::enable_if<
      is_semi_awaitable_v<ReturnT>,
      Task<semi_await_result_t<ReturnT>>>::type
  withWLock(FuncT func) {
    auto lock = co_await Traits::co_writeLock(mutex_);
    co_return co_await func(WriteLockedPtr{std::move(lock), &inner_});
  }

  /**
   * Temporarlily locks both objects and swaps their underlying data.
   *
   * Mimics the behaviour of folly::Synchronized in that we return early if you
   * try to swap with itself and gains locks in ascending memory order to
   * prevent deadlocks.
   *
   * @param rhs The other synchronized object to swap data with.
   * @returns A task that completes once both objects have been swapped.
   */
  Task<void> swap(Synchronized& rhs) {
    if (this == &rhs) {
      co_return;
    }

    // Can't compare pointers for inequality with operator> because it's
    // unspecified behavior unless they share provenance, see:
    // - https://en.wikipedia.org/wiki/Unspecified_behavior,
    // - https://en.cppreference.com/w/cpp/language/operator_comparison.
    if (std::greater<>()(this, &rhs)) {
      co_return co_await rhs.swap(*this);
    }

    auto guard1 = co_await wLock();
    auto guard2 = co_await rhs.wLock();

    using std::swap;
    swap(inner_, rhs.inner_);

    co_return;
  }

  /// Returns a copy of the guarded object made while holding the read lock.
  ///
  /// @returns A task producing a copy of the underlying object.
  Task<Inner> copy() const {
    auto lock = co_await Traits::co_readLock(mutex_);
    Inner res = folly::copy(inner_);
    co_return res;
  }

  /// Locks the object and swaps its underlying data with `newInner`.
  ///
  /// @param newInner The value to swap with the guarded data.
  /// @returns A task that completes once the swap has been performed.
  Task<void> swap(Inner& newInner) {
    auto lock = co_await Traits::co_writeLock(mutex_);

    using std::swap;
    swap(inner_, newInner);
  }

 private:
  mutable CoroMutex mutex_;
  Inner inner_;
};

} // namespace folly::coro
