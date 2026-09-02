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

#include <folly/CancellationToken.h>
#include <folly/coro/Baton.h>
#include <folly/coro/Coroutine.h>
#include <folly/coro/CurrentExecutor.h>
#include <folly/coro/Invoke.h>
#include <folly/coro/Task.h>
#include <folly/coro/Traits.h>
#include <folly/futures/Future.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

/// Converts the given SemiAwaitable to a Task (without starting it).
struct ToTaskFn {
  /// Wrap a SemiAwaitable in a Task without starting it.
  ///
  /// \param a The SemiAwaitable to wrap.
  /// \returns A Task that awaits `a` when started.
  template <typename SemiAwaitable>
  Task<semi_await_result_t<SemiAwaitable>> operator()(SemiAwaitable a) const {
    co_return co_await std::move(a);
  }
  /// Wrap a referenced SemiAwaitable in a Task without starting it.
  ///
  /// \param a A reference wrapper around the SemiAwaitable to wrap.
  /// \returns A Task that awaits the referenced awaitable when started.
  template <typename SemiAwaitable>
  Task<semi_await_result_t<SemiAwaitable>> operator()(
      std::reference_wrapper<SemiAwaitable> a) const {
    co_return co_await a.get();
  }
  /// Wrap a Future<Unit> in a Task without starting it.
  ///
  /// \param a The future to wrap.
  /// \returns A Task that awaits the future when started.
  Task<void> operator()(folly::Future<Unit> a) const {
    co_yield co_result(co_await co_awaitTry(std::move(a)));
  }
  /// Wrap a SemiFuture<Unit> in a Task without starting it.
  ///
  /// \param a The semi-future to wrap.
  /// \returns A Task that awaits the semi-future when started.
  Task<void> operator()(folly::SemiFuture<Unit> a) const {
    co_yield co_result(co_await co_awaitTry(std::move(a)));
  }
};
/// Callable object that converts a SemiAwaitable to a Task without starting it.
inline constexpr ToTaskFn toTask{};

/// Converts a Future to a Task that cancels the future on cancellation.
///
/// \param f The Future to await.
/// \returns A Task yielding the future's result, interrupted on cancellation.
template <typename V>
Task<drop_unit_t<V>> toTaskInterruptOnCancel(folly::Future<V> f) {
  bool cancelled{false};
  Baton baton;
  Try<V> result;
  f.setCallback_(
      [&result, &baton](Executor::KeepAlive<>&&, Try<V>&& t) {
        result = std::move(t);
        baton.post();
      },
      // No user logic runs in the callback, we can avoid the cost of switching
      // the context.
      /* context */ nullptr);

  {
    CancellationCallback cancelCallback(
        co_await co_current_cancellation_token, [&]() noexcept {
          cancelled = true;
          f.cancel();
        });
    co_await baton;
  }
  if (cancelled) {
    co_yield co_stopped_may_throw;
  }
  co_yield co_result(std::move(result));
}

/// Converts a SemiFuture to a Task that cancels the future on cancellation.
///
/// \param f The SemiFuture to await.
/// \returns A Task yielding the future's result, interrupted on cancellation.
template <typename V>
Task<drop_unit_t<V>> toTaskInterruptOnCancel(folly::SemiFuture<V> f) {
  auto ex = co_await co_current_executor;
  co_await co_nothrow(toTaskInterruptOnCancel(std::move(f).via(ex)));
}

/// Converts the given SemiAwaitable to a SemiFuture (without starting it).
///
/// \param a The SemiAwaitable to convert.
/// \returns A SemiFuture for the result of the awaitable.
template <typename SemiAwaitable>
folly::SemiFuture<
    lift_unit_t<semi_await_result_t<remove_reference_wrapper_t<SemiAwaitable>>>>
toSemiFuture(SemiAwaitable&& a) {
  return toTask(std::forward<SemiAwaitable>(a)).semi();
}

/// Converts the given SemiAwaitable to a Future, starting it on the Executor.
///
/// \param a The SemiAwaitable to convert and start.
/// \param ex The executor on which to start and resume the work.
/// \returns A Future for the result of the awaitable.
template <typename SemiAwaitable>
folly::Future<
    lift_unit_t<semi_await_result_t<remove_reference_wrapper_t<SemiAwaitable>>>>
toFuture(SemiAwaitable&& a, Executor::KeepAlive<> ex) {
  auto excopy = ex;
  return co_withExecutor(
             std::move(excopy), toTask(std::forward<SemiAwaitable>(a)))
      .start()
      .via(std::move(ex));
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
