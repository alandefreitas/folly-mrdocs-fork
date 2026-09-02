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

#include <folly/Try.h>
#include <folly/result/result.h>

/// `result<T>` <-> `Try<T>` conversions to aid in migrating legacy `Try` code.
/// See `design_notes.md` for reasons to migrate away from `Try`.
///
/// Perfect interconversion is not always possible:
///   - `Try` does not support reference types, but `result` does.
///   - `Try` has 3 states.  Value & error interconvert transparently.  For the
///     empty state, `try_to_result` has a 2nd arg to set the policy.

#if FOLLY_HAS_RESULT

/// The Folly library.
namespace folly {

/// Converts a `result<T>` to a `Try<T>`.
///
/// NB: If `T` is a reference type, this will fail with a `Try` static assert.
///
/// \param r The result to convert.
/// \returns A `Try<T>` mirroring `r`'s value or error state.
template <typename T>
[[nodiscard]] Try<T> result_to_try(result<T> r) noexcept(
    std::is_nothrow_move_constructible_v<T>) {
  if (r.has_value()) {
    if constexpr (std::is_void_v<T>) {
      return Try<void>{};
    } else {
      return Try<T>{std::move(r).value_or_throw()};
    }
  } else {
    return Try<T>{
        std::move(r).error_or_stopped().get_legacy_error_or_cancellation_slow(
            detail::result_private_t{})};
  }
}

/// Empty-`Try` policy type that maps the empty state to an error `result`.
inline constexpr struct empty_try_as_error_t {
  /// Produces a `UsingUninitializedTry` error `result` for an empty `Try`.
  ///
  /// \returns An error-state `result<T>`.
  template <typename T>
  result<T> on_empty_try() const {
    return {error_or_stopped{UsingUninitializedTry{}}};
  }
} /** The empty-`Try`-as-error policy value. */ empty_try_as_error;

/// Empty-`Try` policy that produces a `result` by invoking a callable.
template <typename Fn>
class empty_try_with {
 private:
  Fn fn_;

 public:
  /// Constructs the policy from a callable.
  ///
  /// \param fn Callable invoked to produce the empty-state `result`.
  explicit empty_try_with(Fn fn) : fn_(std::move(fn)) {}
  /// Produces the `result` for an empty `Try` by invoking the callable.
  ///
  /// \returns The `result<T>` returned by the stored callable.
  template <typename T>
  result<T> on_empty_try() && {
    return {std::move(fn_)()};
  }
};

/// If `t` is empty, defaults to returning a `UsingUninitializedTry` result.
/// Pick your own default via `empty_try_with{[]() { return result<T>{...}; }`.
///
/// \param t The `Try` to convert.
/// \param if_empty Policy that produces a `result<T>` for the empty state.
/// \returns A `result<T>` mirroring `t`, using `if_empty` for the empty state.
template <typename T, typename IfEmpty>
result<T> try_to_result(Try<T> t, IfEmpty if_empty) noexcept(
    std::is_nothrow_move_constructible_v<T> &&
    noexcept(std::move(if_empty).template on_empty_try<T>())) {
  if (t.hasValue()) {
    if constexpr (std::is_void_v<T>) {
      return result<void>{};
    } else {
      return {std::move(t).value()};
    }
  } else if (t.hasException()) {
    return {error_or_stopped::make_legacy_error_or_cancellation_slow(
        detail::result_private_t{}, std::move(t).exception())};
  } else {
    // NEVER use `private_rich_exception_ptr_sigil` here, that is reserved for
    // a potential `Try`-in-terms-of-`result` implementation, and would make
    // the returned `result` behave in unexpected ways.
    return std::move(if_empty).template on_empty_try<T>();
  }
}
/// Converts a `Try<T>` to a `result<T>`, treating empty as an error.
///
/// \param t The `Try` to convert.
/// \returns A `result<T>` mirroring `t`'s value or error state.
template <typename T>
result<T> try_to_result(Try<T> t) noexcept(
    std::is_nothrow_move_constructible_v<T>) {
  return try_to_result(std::move(t), empty_try_as_error);
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
