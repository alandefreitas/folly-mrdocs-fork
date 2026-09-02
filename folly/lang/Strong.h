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

#include <iosfwd>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

namespace folly {

/// `strong<T, Tag>` - prevent accidental mixing of semantically different uses
/// of the same underlying type. Known as "newtype" in some languages. C++20.
/// Default-construction value-initializes `T`, even if it's a primitive type.
///
/// The tag type should be the name of your derived type. This guarantees that
/// different strong `T`s are not interchangeable.
///
/// The `std::hash`, `fmt::format`, and `operator<<(ostream&)` implementations
/// rely on this convention, so if you see failures in those, check your tag.
/// You can customize the output per-type, see `CustomOutput` in the test.
///
/// It is often OK to allow implicit construction from the underlying type.
/// We strongly recommend using concepts to limit undesired conversions:
///
///   struct UserId : public strong<uint64_t, UserId> {
///     using strong<uint64_t, UserId>::strong;
///     // Prohibit risky signed-unsigned & float-int conversions.
///     // Callers will want to suffix integer literals with `u`.
///     /* implicit */ UserId(std::unsigned_integral auto n) : strong{n} {}
///   };
///
///   UserId uid{123u}; // OK, explicit construction
///   UserId uid = 123u; // OK, implicit conversion
///   UserId uid{123}; // Warning: narrowing conversion
///   UserId uid = 123; // Error: no viable conversion
///
/// For ease of use, even without an implicit constructor, your strong type
/// will be comparable with the underlying type.
template <typename T, typename /* your derived class */>
class strong {
 private:
  static_assert(!std::is_reference_v<T>);
  T value_{};

 public:
  /// The wrapped underlying type `T`.
  using underlying_type = T;

  /// Default-construct, value-initializing `T` even if it is trivial.
  constexpr strong() = default; // note: value-initializes `T`, even if trivial

  /// Construct explicitly by forwarding args to the underlying `T` ctor.
  ///
  /// In particular, this will construct `strong` from `T`.
  ///
  /// @param head The first argument forwarded to `T`'s constructor.
  /// @param tail The remaining arguments forwarded to `T`'s constructor.
  template <typename Head, typename... Tail>
    requires(
        std::is_constructible_v<T, Head, Tail...> &&
        // Avoid touching the standard ctors
        !std::derived_from<std::remove_cvref_t<Head>, strong>)
  explicit constexpr strong(Head&& head, Tail&&... tail)
      : value_{std::forward<Head>(head), std::forward<Tail>(tail)...} {}

  /// Equality-compare two `strong` values; available iff `T` is comparable.
  ///
  /// @param lhs The left-hand `strong` value.
  /// @param rhs The right-hand `strong` value.
  /// @return `true` if the wrapped values are equal.
  friend bool operator==(const strong& lhs, const strong& rhs) = default;
  /// Three-way-compare two `strong` values; available iff `T` is comparable.
  ///
  /// @param lhs The left-hand `strong` value.
  /// @param rhs The right-hand `strong` value.
  /// @return The result of comparing the wrapped values.
  friend auto operator<=>(const strong& lhs, const strong& rhs) = default;

  /// Equality-compare a `strong` against a value of the underlying type.
  ///
  /// For ease of use, `strong` is comparable with `underlying_type`, even if
  /// your derived class does not add an implicit constructor. Accept only `T`
  /// to prevent implicit conversions from other types (`strong<int>` vs
  /// `double`).
  ///
  /// @param st The strong value.
  /// @param underlying A value of the underlying type `T`.
  /// @return `true` if the wrapped value equals `underlying`.
  template <typename U>
    requires std::same_as<std::remove_cvref_t<U>, T>
  friend constexpr bool operator==(const strong& st, U&& underlying) {
    return st.value_ == underlying;
  }
  /// Three-way-compare a `strong` against a value of the underlying type.
  ///
  /// @param st The strong value.
  /// @param underlying A value of the underlying type `T`.
  /// @return The result of comparing the wrapped value against `underlying`.
  template <typename U>
    requires std::same_as<std::remove_cvref_t<U>, T>
  friend constexpr auto operator<=>(const strong& st, U&& underlying) {
    return st.value_ <=> underlying;
  }

  /// Explicitly convert to a mutable lvalue reference to the underlying value.
  ///
  /// @return A mutable lvalue reference to the underlying value.
  explicit constexpr operator T&() & { return value_; }
  /// Explicitly convert to a const lvalue reference to the underlying value.
  ///
  /// @return A const lvalue reference to the underlying value.
  explicit constexpr operator T const&() const& { return value_; }
  /// Explicitly convert to an rvalue reference to the underlying value.
  ///
  /// @return An rvalue reference to the underlying value.
  explicit constexpr operator T&&() && { return std::move(value_); }
  /// Explicitly convert to a const rvalue reference to the underlying value.
  ///
  /// @return A const rvalue reference to the underlying value.
  explicit constexpr operator T const&&() const&& { return std::move(value_); }

  /// Access the underlying value as a mutable lvalue reference.
  ///
  /// @return A mutable lvalue reference to the underlying value.
  constexpr T& value() & { return value_; }
  /// Access the underlying value as a const lvalue reference.
  ///
  /// @return A const lvalue reference to the underlying value.
  constexpr const T& value() const& { return value_; }
  /// Access the underlying value as an rvalue reference.
  ///
  /// @return An rvalue reference to the underlying value.
  constexpr T&& value() && { return std::move(value_); }
  /// Access the underlying value as a const rvalue reference.
  ///
  /// @return A const rvalue reference to the underlying value.
  constexpr const T&& value() const&& { return std::move(value_); }
};

/// `strong_derefable<T, Tag>` - a strong type with pointer-like dereference
/// operators. Inherits all functionality from `strong` and adds `operator*` and
/// `operator->` for convenient access to the underlying value. Usage:
///
///   struct X : strong_derefable<T, X> { /* as above */ };
template <typename T, typename Tag>
class strong_derefable : public strong<T, Tag> {
 public:
  using strong<T, Tag>::strong;

  /// Dereference to a const lvalue reference to the underlying value.
  ///
  /// @return A const lvalue reference to the underlying value.
  constexpr const T& operator*() const& { return this->value(); }
  /// Dereference to a mutable lvalue reference to the underlying value.
  ///
  /// @return A mutable lvalue reference to the underlying value.
  constexpr T& operator*() & { return this->value(); }
  /// Dereference to a const rvalue reference to the underlying value.
  ///
  /// @return A const rvalue reference to the underlying value.
  constexpr const T&& operator*() const&& { return std::move(*this).value(); }
  /// Dereference to an rvalue reference to the underlying value.
  ///
  /// @return An rvalue reference to the underlying value.
  constexpr T&& operator*() && { return std::move(*this).value(); }

  /// Member-access the underlying value through a const pointer.
  ///
  /// @return A const pointer to the underlying value.
  constexpr const T* operator->() const { return &this->value(); }
  /// Member-access the underlying value through a mutable pointer.
  ///
  /// @return A mutable pointer to the underlying value.
  constexpr T* operator->() { return &this->value(); }
};

/// Stream a strong type; available iff the underlying type is streamable.
///
/// @param os The output stream.
/// @param st The strong value to write.
/// @return The stream `os`, for chaining.
template <typename T, typename Tag>
auto operator<<(std::ostream& os, const strong<T, Tag>& st)
    -> decltype(os << st.value()) {
  return os << st.value();
}

/// Satisfied when `T` derives from `strong` instantiated with its own tag.
template <typename T>
concept is_strong =
    std::derived_from<T, strong<typename T::underlying_type, T>>;

} // namespace folly

/// Hash support so strong types can be keys in unordered containers.
///
/// A strong type is hashable iff the underlying type is.
template <typename T>
  requires(
      folly::is_strong<T> &&
      // Required for `folly::is_hashable_v` to be correct.
      requires(typename T::underlying_type underlying) {
        {
          std::hash<typename T::underlying_type>{}(underlying)
        } -> std::convertible_to<std::size_t>;
      })
struct std::hash<T> {
  /// Hash a strong value by hashing its underlying value.
  ///
  /// @param st The strong value to hash.
  /// @return The hash of the wrapped underlying value.
  size_t operator()(const T& st) const {
    return std::hash<typename T::underlying_type>{}(st.value());
  }
};

/// Format support; strong types are formattable iff the underlying type is.
template <typename T>
  requires(
      folly::is_strong<T> &&
      fmt::is_formattable<typename T::underlying_type>::value)
struct fmt::formatter<T> : fmt::formatter<typename T::underlying_type> {
  /// Format a strong value by formatting its underlying value.
  ///
  /// @param st The strong value to format.
  /// @param ctx The formatting context.
  /// @return The output iterator past the formatted output.
  auto format(const T& st, auto& ctx) const {
    return fmt::formatter<typename T::underlying_type>::format(st.value(), ctx);
  }
};
