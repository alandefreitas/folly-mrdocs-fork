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

#include <cstddef>
#include <type_traits>
#include <utility>

#include <folly/CppAttributes.h>
#include <folly/Utility.h>
#include <folly/lang/Align.h>

namespace folly {

/// A wrapper that stores a value of type T with at least the given alignment.
template <typename T, std::size_t Align>
class aligned {
  static_assert(!(Align & (Align - 1)), "alignment not a power of two");
  static_assert(alignof(T) <= Align, "alignment too small");

 public:
  /// The alignment, in bytes, applied to the stored value.
  using alignment = index_constant<Align>;
  /// The type of the stored value.
  using value_type = T;

  /// Default-constructs the stored value.
  aligned() = default;
  /// Copy-constructs from another aligned wrapper.
  ///
  /// @param other The wrapper to copy from.
  aligned(aligned const& other) = default;
  /// Move-constructs from another aligned wrapper.
  ///
  /// @param other The wrapper to move from.
  aligned(aligned&& other) = default;
  /// Constructs the stored value as a copy of the given value.
  ///
  /// @param value The value to copy.
  template <typename S = T>
    requires std::is_copy_constructible<S>::value
  aligned(T const& value) noexcept(std::is_nothrow_copy_constructible<T>::value)
      : value_(value) {}
  /// Constructs the stored value by moving the given value.
  ///
  /// @param value The value to move.
  template <typename S = T>
    requires std::is_move_constructible<S>::value
  aligned(T&& value) noexcept(std::is_nothrow_move_constructible<T>::value)
      : value_(static_cast<T&&>(value)) {}
  /// Constructs the stored value in place from the given arguments.
  ///
  /// @param inPlace Tag selecting in-place construction.
  /// @param a Arguments forwarded to the constructor of the stored value.
  template <typename... A>
    requires std::is_constructible<T, A...>::value
  explicit aligned(std::in_place_t inPlace, A&&... a) noexcept(
      std::is_nothrow_constructible<T, A...>::value)
      : value_(static_cast<A&&>(a)...) {}

  /// Copy-assigns from another aligned wrapper.
  ///
  /// @param other The wrapper to copy from.
  /// @return A reference to this wrapper.
  aligned& operator=(aligned const& other) = default;
  /// Move-assigns from another aligned wrapper.
  ///
  /// @param other The wrapper to move from.
  /// @return A reference to this wrapper.
  aligned& operator=(aligned&& other) = default;
  /// Assigns a copy of the given value to the stored value.
  ///
  /// @param value The value to copy.
  /// @return A reference to this wrapper.
  template <typename S = T>
    requires std::is_copy_assignable<S>::value
  aligned& operator=(T const& value) noexcept(
      std::is_nothrow_copy_assignable<T>::value) {
    value_ = value;
    return *this;
  }
  /// Move-assigns the given value to the stored value.
  ///
  /// @param value The value to move.
  /// @return A reference to this wrapper.
  template <typename S = T>
    requires std::is_move_assignable<S>::value
  aligned& operator=(T&& value) noexcept(
      std::is_nothrow_move_assignable<T>::value) {
    value_ = std::move(value);
    return *this;
  }

  /// Returns a pointer to the stored value.
  ///
  /// @return A pointer to the stored value.
  T* get() noexcept [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] { return &value_; }
  /// Returns a pointer to the stored value.
  ///
  /// @return A pointer to the stored value.
  T const* get() const noexcept [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] { return &value_; }
  /// Returns a pointer to the stored value.
  ///
  /// @return A pointer to the stored value.
  T* operator->() noexcept [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] { return &value_; }
  /// Returns a pointer to the stored value.
  ///
  /// @return A pointer to the stored value.
  T const* operator->() const noexcept [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] { return &value_; }
  /// Returns a reference to the stored value.
  ///
  /// @return A reference to the stored value.
  T& operator*() noexcept [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] { return value_; }
  /// Returns a reference to the stored value.
  ///
  /// @return A reference to the stored value.
  T const& operator*() const noexcept [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] { return value_; }

 private:
  alignas(Align) T value_;
};

/// An aligned wrapper for T using at least cache-line alignment.
template <typename T>
using cacheline_aligned = aligned<
    T,
    (cacheline_align_v < alignof(T) ? alignof(T) : cacheline_align_v)>;

} // namespace folly
