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

#include <folly/functional/Invoke.h>
#include <folly/futures/Future.h>

namespace folly {

/// Executor wrapper that adds future-returning task submission.
///
/// \tparam ExecutorImpl The underlying executor type to wrap.
template <typename ExecutorImpl>
class FutureExecutor : public ExecutorImpl {
 public:
  /// Constructs the wrapper, forwarding all arguments to the wrapped executor.
  ///
  /// \tparam Args Types of the arguments forwarded to `ExecutorImpl`.
  /// \param args Arguments forwarded to the wrapped executor's constructor.
  template <typename... Args>
  explicit FutureExecutor(Args&&... args)
      : ExecutorImpl(std::forward<Args>(args)...) {}

  /// Adds a function that returns a `Future<T>` and returns its result future.
  ///
  /// Given a function func that returns a Future<T>, adds that function to the
  /// contained Executor and returns a Future<T> which will be fulfilled with
  /// func's result once it has been executed.
  ///
  /// For example: auto f = futureExecutor.addFuture([](){
  ///                return doAsyncWorkAndReturnAFuture();
  ///              });
  ///
  /// \tparam F The callable type, which must return a future.
  /// \param func The function to execute.
  /// \returns A future fulfilled with the result of `func`.
  template <typename F>
  typename std::enable_if<
      folly::isFuture<invoke_result_t<F>>::value,
      invoke_result_t<F>>::type
  addFuture(F func) {
    using T = typename invoke_result_t<F>::value_type;
    folly::Promise<T> promise;
    auto future = promise.getFuture();
    ExecutorImpl::add([promise = std::move(promise),
                       func = std::move(func)]() mutable {
      func().then([promise = std::move(promise)](folly::Try<T>&& t) mutable {
        promise.setTry(std::move(t));
      });
    });
    return future;
  }

  /// Adds a function that returns a non-future value and returns its result
  /// future.
  ///
  /// Similar to addFuture above, but takes a func that returns some non-Future
  /// type T.
  ///
  /// For example: auto f = futureExecutor.addFuture([]() {
  ///                return 42;
  ///              });
  ///
  /// \tparam F The callable type, which must return a non-future value.
  /// \param func The function to execute.
  /// \returns A future fulfilled with the result of `func`.
  template <typename F>
  typename std::enable_if<
      !folly::isFuture<invoke_result_t<F>>::value,
      folly::Future<folly::lift_unit_t<invoke_result_t<F>>>>::type
  addFuture(F func) {
    using T = folly::lift_unit_t<invoke_result_t<F>>;
    folly::Promise<T> promise;
    auto future = promise.getFuture();
    ExecutorImpl::add(
        [promise = std::move(promise), func = std::move(func)]() mutable {
          promise.setWith(std::move(func));
        });
    return future;
  }
};

} // namespace folly
