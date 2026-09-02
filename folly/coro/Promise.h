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

#include <utility>

#include <folly/CancellationToken.h>
#include <folly/Try.h>
#include <folly/coro/Baton.h>
#include <folly/coro/Coroutine.h>
#include <folly/futures/Promise.h>
#include <folly/lang/SafeAlias-fwd.h>
#include <folly/synchronization/RelaxedAtomic.h>

#if FOLLY_HAS_COROUTINES

namespace folly::coro {
template <typename T>
class Promise;
template <typename T>
class Future;

// Creates promise and associated unfulfilled future
template <typename T>
std::pair<Promise<T>, Future<T>> makePromiseContract();

// Creates fulfilled future
template <typename T>
Future<remove_cvref_t<T>> makeFuture(T&&);
template <typename T>
Future<T> makeFuture(exception_wrapper&&);
Future<void> makeFuture();

namespace detail {
template <typename T>
struct PromiseState {
  PromiseState() = default;

  Try<T> result;
  // Must be exchanged to true before setting result
  folly::relaxed_atomic<bool> fulfilled{false};
  // Must be posted after setting result
  coro::Baton ready;
};
} // namespace detail

/// The write side of a promise/future contract used to fulfill a Future.
template <typename T>
class Promise {
 public:
  /**
   * Construct an empty Promise.
   *
   * This object is not valid use until you initialize it with move assignment.
   */
  Promise() = default;

  /// Move-constructs from another promise, taking over its shared state.
  /// @param other The promise to move from.
  Promise(Promise&& other) noexcept
      : ct_(std::move(other.ct_)),
        state_(std::exchange(other.state_, nullptr)) {}
  /// Move-assigns from another promise, breaking this one if unfulfilled.
  /// @param other The promise to move from.
  /// @returns A reference to this promise.
  Promise& operator=(Promise&& other) noexcept {
    if (this != &other && state_ && !state_->fulfilled) {
      setException(BrokenPromise{tag<T>});
    }
    ct_ = std::move(other.ct_);
    state_ = std::exchange(other.state_, nullptr);
    return *this;
  }
  /// Copy construction is disabled; a Promise is move-only.
  Promise(const Promise& other) = delete;
  /// Copy assignment is disabled; a Promise is move-only.
  Promise& operator=(const Promise& other) = delete;

  /// Destroys the promise, breaking it if it was never fulfilled.
  ~Promise() {
    if (state_ && !state_->fulfilled) {
      setException(BrokenPromise{tag<T>});
    }
  }

  /// Returns whether the promise refers to a valid shared state.
  /// @returns True if the promise is valid.
  bool valid() const noexcept { return state_; }

  /// Returns whether the promise has already been fulfilled.
  /// @returns True if a result has been set.
  bool isFulfilled() const noexcept { return state_ && state_->fulfilled; }

  /// Fulfills the promise with a value.
  /// @param args The arguments used to construct the stored value.
  template <typename... Args>
  void setValue(Args&&... args) {
    trySetValue(std::forward<Args>(args)...);
  }

  /// Fulfills the promise with an exception.
  /// @param args The arguments used to construct the stored exception.
  template <typename... Args>
  void setException(Args&&... args) {
    trySetException(std::forward<Args>(args)...);
  }

  /// Fulfills the promise with a Try holding a value or exception.
  /// @param result The Try holding the value or exception to store.
  void setResult(Try<T>&& result) { trySetResult(std::move(result)); }

  /**
   * Fulfills the promise with a value if not already fulfilled.
   * @param args The arguments used to construct the stored value.
   * @returns Whether the fulfillment took place.
   */
  template <typename... Args>
  bool trySetValue(Args&&... args) {
    DCHECK(state_);
    if (state_->fulfilled.exchange(true)) {
      return false;
    }
    if constexpr (std::is_void_v<T>) {
      static_assert(sizeof...(Args) == 0);
    } else {
      state_->result.emplace(std::forward<Args>(args)...);
    }
    state_->ready.post();
    return true;
  }

  /**
   * Fulfills the promise with an exception if not already fulfilled.
   * @param args The arguments used to construct the stored exception.
   * @returns Whether the fulfillment took place.
   */
  template <typename... Args>
  bool trySetException(Args&&... args) {
    DCHECK(state_);
    if (state_->fulfilled.exchange(true)) {
      return false;
    }
    state_->result.emplaceException(std::forward<Args>(args)...);
    state_->ready.post();
    return true;
  }

  /**
   * Fulfills the promise with a Try if not already fulfilled.
   * @param result The Try holding the value or exception to store.
   * @returns Whether the fulfillment took place.
   */
  bool trySetResult(Try<T>&& result) {
    DCHECK(state_);
    if (state_->fulfilled.exchange(true)) {
      return false;
    }
    state_->result = std::move(result);
    state_->ready.post();
    return true;
  }

  /**
   * Fulfills the promise with a value/Try returned from calling func if not
   * already fulfilled.
   *
   * If either the call to func or the result's constructor completes with an
   * exception then the exception is caught and stored as the result.
   *
   * @param func The callable that produces the value or Try to store.
   * @returns Whether the fulfillment took place.
   */
  template <typename Func>
  bool trySetWith(Func&& func) {
    DCHECK(state_);
    if (state_->fulfilled.exchange(true)) {
      return false;
    }
    try {
      state_->result = Try<T>(std::forward<Func>(func)());
    } catch (...) {
      state_->result.emplaceException(current_exception());
    }
    state_->ready.post();
    return true;
  }

  /**
   * Fulfills the promise with an exception returned from calling func if not
   * already fulfilled.
   *
   * If either the call to func or the result's constructor completes with an
   * exception then the exception is caught and stored as the result.
   *
   * @param func The callable that produces the exception to store.
   * @returns Whether the fulfillment took place.
   */
  template <typename Func>
  bool trySetExceptionWith(Func&& func) {
    DCHECK(state_);
    if (state_->fulfilled.exchange(true)) {
      return false;
    }
    try {
      state_->result.emplaceException(std::forward<Func>(func)());
    } catch (...) {
      state_->result.emplaceException(current_exception());
    }
    state_->ready.post();
    return true;
  }

  /// Returns the cancellation token associated with this promise.
  /// @returns The cancellation token observed by the promise.
  const CancellationToken& getCancellationToken() const { return ct_; }

 private:
  Promise(CancellationToken ct, detail::PromiseState<T>& state)
      : ct_(std::move(ct)), state_(&state) {}

  CancellationToken ct_;
  detail::PromiseState<T>* state_{nullptr};

  friend std::pair<Promise<T>, Future<T>> makePromiseContract<T>();
};

/// The awaitable read side of a promise/future contract.
template <typename T>
class Future {
 public:
  /**
   * Construct an empty Future.
   *
   * This object is not valid use until you initialize it with move assignment.
   */
  Future() = default;

  /// Move-constructs from another future, taking over its shared state.
  /// @param other The future to move from.
  Future(Future&& other) noexcept
      : cs_(std::move(other.cs_)),
        state_(std::exchange(other.state_, nullptr)),
        ct_(std::move(other.ct_)),
        hasCancelTokenOverride_(
            std::exchange(other.hasCancelTokenOverride_, false)) {}

  /// Move-assigns from another future, taking over its shared state.
  /// @param other The future to move from.
  /// @returns A reference to this future.
  Future& operator=(Future&& other) noexcept {
    if (this != &other) {
      cs_ = std::move(other.cs_);
      state_ = std::exchange(other.state_, nullptr);
      ct_ = std::move(other.ct_);
      hasCancelTokenOverride_ =
          std::exchange(other.hasCancelTokenOverride_, false);
    }
    return *this;
  }

  /// Copy construction is disabled; a Future is move-only.
  Future(const Future& other) = delete;
  /// Copy assignment is disabled; a Future is move-only.
  Future& operator=(const Future& other) = delete;

  /// Awaitable that waits for the future's result when the future is co_awaited.
  class WaitOperation : private Baton::WaitOperation {
   public:
    /// Constructs a wait operation bound to the given future.
    /// @param future The future to await.
    explicit WaitOperation(Future& future) noexcept
        : Baton::WaitOperation(future.state_->ready),
          future_(future),
          cb_(std::move(future.ct_), [&] { future_.cancel(); }) {}

    /// Reports whether the future is already ready without suspending.
    using Baton::WaitOperation::await_ready;
    /// Suspends the awaiting coroutine until the future becomes ready.
    using Baton::WaitOperation::await_suspend;

    /// Resumes the awaiting coroutine and returns the future's value.
    /// @returns The future's value, or throws if it holds an exception.
    T await_resume() {
      if constexpr (!std::is_void_v<T>) {
        return std::move(future_.state_->result.value());
      } else {
        future_.state_->result.throwIfFailed();
      }
    }

    /// Resumes the awaiting coroutine and returns the result as a Try.
    /// @returns The future's result wrapped in a Try, including any exception.
    folly::Try<T> await_resume_try() {
      return std::move(future_.state_->result);
    }

   private:
    Future& future_;
    CancellationCallback cb_;
  };

  /// Suspends the current coroutine until the future is ready.
  /// @returns An awaitable that resumes with the future's result.
  [[nodiscard]] WaitOperation operator co_await() && noexcept {
    return WaitOperation{*this};
  }

  /// Returns whether the future refers to a valid shared state.
  /// @returns True if the future is valid.
  bool valid() const noexcept { return state_ != nullptr; }

  /// Returns whether the future refers to a valid shared state.
  /// @returns True if the future is valid.
  explicit operator bool() const noexcept { return valid(); }

  /// Returns whether the associated promise has been fulfilled.
  /// @returns True if a result is ready to be consumed.
  bool isReady() const noexcept { return state_->ready.ready(); }

  /// Attaches a cancellation token to the future so awaiting it observes cancellation.
  /// @param ct The cancellation token to associate with the future.
  /// @param future The future to attach the token to.
  /// @returns The future with the cancellation token applied.
  friend Future co_withCancellation(
      folly::CancellationToken ct, Future&& future) noexcept {
    if (!std::exchange(future.hasCancelTokenOverride_, true)) {
      future.ct_ = std::move(ct);
    }
    return std::move(future);
  }

  /// The safe-alias category of this Future, derived from its value type.
  template <safe_alias Default>
  using folly_private_safe_alias_t = safe_alias_of<T, Default>;

 private:
  Future(CancellationSource cs, detail::PromiseState<T>& state)
      : cs_(std::move(cs)), state_(&state) {}

  void cancel() {
    if (!state_->fulfilled.exchange(true)) {
      cs_.requestCancellation();
      state_->result.emplaceException(OperationCancelled{});
      state_->ready.post();
    }
  }

  CancellationSource cs_;
  detail::PromiseState<T>* state_{nullptr};
  // The token inherited when the future is awaited
  CancellationToken ct_;
  bool hasCancelTokenOverride_{false};

  friend std::pair<Promise<T>, Future<T>> makePromiseContract<T>();
};

/**
 * makePromiseContract can help you migrating your non-coroutine code base to
 * coroutine. If your code already uses Future/SemiFuture, you don't need this
 * tool. A common use case is with async callback functions. In the example, we
 * can pass a callback function into the legacy code sleepAndNotify and
 * sleepAndNotify sets the promise on completion. Consider to use detachOnCancel
 * with this makePromiseContract to handle long running (longer than your
 * timeout) tasks that don't handle cancellation properly.
 *
 * @returns A pair holding the promise and its associated unfulfilled future.
 *
 * \refcode folly/docs/examples/folly/coro/Promise.cpp
 */
template <typename T>
std::pair<Promise<T>, Future<T>> makePromiseContract() {
  auto [cs, data] = CancellationSource::create(
      folly::detail::WithDataTag<detail::PromiseState<T>>{});
  return {
      Promise<T>{cs.getToken(), std::get<0>(*data)},
      Future<T>{std::move(cs), std::get<0>(*data)}};
}

/// Creates a future already fulfilled with the given value.
/// @param t The value to store in the future.
/// @returns A ready future holding the value.
template <typename T>
Future<remove_cvref_t<T>> makeFuture(T&& t) {
  auto [promise, future] = makePromiseContract<remove_cvref_t<T>>();
  promise.setValue(std::forward<T>(t));
  return std::move(future);
}
/// Creates a future already fulfilled with the given exception.
/// @param ex The exception to store in the future.
/// @returns A ready future holding the exception.
template <typename T>
Future<T> makeFuture(exception_wrapper&& ex) {
  auto [promise, future] = makePromiseContract<T>();
  promise.setException(std::move(ex));
  return std::move(future);
}
/// Creates a ready future that holds no value.
/// @returns A ready future of void.
inline Future<void> makeFuture() {
  auto [promise, future] = makePromiseContract<void>();
  promise.setValue();
  return std::move(future);
}

} // namespace folly::coro

#endif
