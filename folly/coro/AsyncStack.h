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

#include <folly/Executor.h>
#include <folly/coro/Coroutine.h>
#include <folly/coro/WithAsyncStack.h>
#include <folly/tracing/AsyncStack.h>

#include <utility>
#include <vector>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

/// Awaitable that yields the current async stack trace.
class AsyncStackTraceAwaitable {
  class Awaiter {
   public:
    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    bool await_suspend(coroutine_handle<Promise> h) noexcept {
      initialFrame_ = &h.promise().getAsyncFrame();
      return false;
    }

    FOLLY_NOINLINE std::vector<std::uintptr_t> await_resume() {
      static constexpr size_t maxFrames = 100;
      std::array<std::uintptr_t, maxFrames> result;

      result[0] =
          reinterpret_cast<std::uintptr_t>(FOLLY_ASYNC_STACK_RETURN_ADDRESS());
      auto numFrames = getAsyncStackTraceFromInitialFrame(
          initialFrame_, result.data() + 1, maxFrames - 1);

      return std::vector<std::uintptr_t>(
          std::make_move_iterator(result.begin()),
          std::make_move_iterator(result.begin()) + numFrames + 1);
    }

   private:
    folly::AsyncStackFrame* initialFrame_;
  };

 public:
  /// Returns this awaitable unchanged, ignoring the executor.
  ///
  /// @param executor the executor to schedule on (unused)
  /// @returns a copy of this awaitable
  AsyncStackTraceAwaitable viaIfAsync(
      const folly::Executor::KeepAlive<>& executor) const noexcept {
    return {};
  }

  /// Produces the awaiter for this awaitable.
  ///
  /// @returns an awaiter that yields the current async stack trace
  Awaiter operator co_await() const noexcept { return {}; }

  /// Forwards the awaitable for `co_withAsyncStack` support.
  ///
  /// @param cpo the customization-point tag (unused)
  /// @param awaitable the awaitable to forward
  /// @returns the forwarded awaitable
  friend AsyncStackTraceAwaitable tag_invoke(
      cpo_t<co_withAsyncStack> cpo, AsyncStackTraceAwaitable awaitable) noexcept {
    return awaitable;
  }
};

/// Awaitable yielding the current async stack trace when co_awaited.
inline constexpr AsyncStackTraceAwaitable co_current_async_stack_trace = {};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
