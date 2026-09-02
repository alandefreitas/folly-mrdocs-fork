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

#include <type_traits>
#include <utility>

#include <glog/logging.h>

#include <folly/Traits.h>
#include <folly/Unit.h>
#include <folly/functional/Invoke.h>
#include <folly/lang/CustomizationPoint.h>

namespace folly {
namespace fibers {
/// Async/await-style API for running fiber tasks.
namespace async {

template <typename T>
class Async;

namespace detail {
/**
 * Define in source to avoid including FiberManager header and keep this file
 * cheap to include
 */
bool onFiber();
} // namespace detail

/// Callable type that awaits an Async wrapper via tag_invoke.
struct await_fn {
  /// Awaits `async` and returns its result.
  ///
  /// @param async The Async wrapper to await.
  /// @returns The value produced by awaiting `async`.
  template <typename T>
  auto operator()(Async<T>&& async) const
      noexcept(is_nothrow_tag_invocable<await_fn, Async<T>&&>::value)
          -> tag_invoke_result_t<await_fn, Async<T>&&> {
    return tag_invoke(*this, static_cast<Async<T>&&>(async));
  }
};
/// Customization point object that retrieves the result from an Async wrapper.
///
/// A function calling await must return an Async wrapper itself for the wrapper
/// to serve its intended purpose (the best way to enforce this is static
/// analysis).
///
/// \implementationdefined
FOLLY_DEFINE_CPO(await_fn, await_async)
#if !defined(_MSC_VER)
static constexpr auto& await = await_async;
#endif

/**
 * Asynchronous fiber result wrapper
 *
 * Syntactic sugar to indicate that can be used as the return type of a
 * function, indicating that a fiber can be preempted within that function.
 * Wraps the eagerly executed result of the function and must be 'await'ed to
 * retrieve the result.
 *
 * Since fibers context switches are implicit, it can be difficult to tell if a
 * function does I/O. In large codebases, it can also be difficult to tell if a
 * given function is running on fibers or not. The Async<> return type makes
 * I/O explicit and provides a good way to identify code paths running on fiber.
 *
 * Async must be combined with static analysis (eg. lints) that forces a
 * function that calls 'await' to also return an Async wrapped result.
 *
 * Runtime Consideration:
 * - The wrapper is currently 0 cost (in optimized builds), and this will
 *   remain a guarentee
 * - It provides protection (in debug builds) against running Async-annotated
 *   code on main context.
 * - It does NOT provide protection against doing I/O in non Async-annotated
 *   code, both asynchronously (on fiber) or blocking (main context).
 * - It does NOT provide protection from fiber's stack overflow.
 */
template <typename T>
class [[nodiscard]] Async {
 public:
  /// The value type wrapped by this Async.
  using inner_type = T;

  /// General use constructor that forwards `val` to the wrapped value.
  ///
  /// @param val Arguments used to construct the wrapped value.
  template <typename... Us>
  /* implicit */ Async(Us&&... val) : val_(std::forward<Us>(val)...) {}

  /// Move constructor to allow eager-return of async without using await.
  ///
  /// @param async The wrapper whose value is moved into this one.
  template <typename U>
  /* implicit */ Async(Async<U>&& async) noexcept
      : val_(static_cast<U&&>(async.val_)) {}

  /// Copy construction is disabled.
  ///
  /// @param other The wrapper that would be copied.
  Async(const Async& other) = delete;

  /// Move-constructs the wrapper from `other`.
  ///
  /// @param other The wrapper to move from.
  Async(Async&& other) = default;

  /// Copy assignment is disabled.
  ///
  /// @param other The wrapper that would be copied.
  /// @returns A reference to this wrapper.
  Async& operator=(const Async& other) = delete;

  /// Move assignment is disabled.
  ///
  /// @param other The wrapper that would be moved.
  /// @returns A reference to this wrapper.
  Async& operator=(Async&& other) = delete;

  /// Awaits the wrapper and returns its stored value.
  ///
  /// @param awaiter The await customization-point tag (unused).
  /// @param async The Async wrapper being awaited.
  /// @returns The value stored in `async`.
  friend T&& tag_invoke(await_fn awaiter, Async&& async) noexcept {
    DCHECK(detail::onFiber());
    return static_cast<T&&>(async.val_);
  }

 private:
  T val_;

  template <typename U>
  friend class Async;
};

/// Async wrapper specialization for functions that produce no value.
template <>
class [[nodiscard]] Async<void> {
 public:
  using inner_type = void;

  /* implicit */ Async() {}
  /* implicit */ Async(Unit) {}
  /* implicit */ Async(Async<Unit>&&) {}

  Async(const Async&) = delete;
  Async(Async&& other) = default;
  Async& operator=(const Async&) = delete;
  Async operator=(Async&&) = delete;

  /// Awaits the wrapper, checking that the caller runs on a fiber.
  ///
  /// @param awaiter The await customization-point tag (unused).
  /// @param async The Async wrapper being awaited (unused).
  friend void tag_invoke(await_fn awaiter, Async&& async) noexcept {
    DCHECK(detail::onFiber());
  }
};

/**
 * Deduction guide to make it easier to construct and return Async objects.
 * The guide doesn't permit constructing and returning by reference.
 */
template <typename T>
explicit Async(T) -> Async<T>;

/**
 * A utility to start annotating at top of stack (eg. the task which is added to
 * fiber manager) A function must not return an Async wrapper if it uses
 * `init_await` instead of `await` (again, enforce via static analysis)
 *
 * @param async The Async wrapper to await.
 * @returns The value extracted from `async`.
 */
template <typename T>
T&& init_await(Async<T>&& async) {
  return await_async(std::move(async));
}

/// Starts annotating at the top of the stack for an Async wrapper with no
/// value.
///
/// @param async The Async<void> wrapper to await.
inline void init_await(Async<void>&& async) {
  await_async(std::move(async));
}

/// True when `T` is an instantiation of the Async wrapper.
template <typename T>
constexpr bool is_async_v = folly::is_instantiation_of_v<Async, T>;

/// Trait that extracts the inner value type wrapped by an Async type.
template <typename T>
struct async_inner_type;

template <typename T>
struct async_inner_type<Async<T>> {
  using type = T;
};

/// Inner value type wrapped by the Async type `T`.
template <typename T>
using async_inner_type_t = typename async_inner_type<T>::type;

/// Maps the Async result of invoking `F` with `Args...` to its inner value
/// type.
template <typename F, typename... Args>
using async_invocable_inner_type =
    async_inner_type<std::invoke_result_t<F, Args...>>;

/// Inner value type produced by invoking `F` with `Args...` and unwrapping the
/// resulting Async.
template <typename F, typename... Args>
using async_invocable_inner_type_t =
    typename async_invocable_inner_type<F, Args...>::type;

} // namespace async
} // namespace fibers
} // namespace folly
