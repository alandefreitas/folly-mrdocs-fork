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

namespace folly {

/// In functional programming, the degenerate case is often called "unit". In
/// C++, "void" is often the best analogue. However, because of the syntactic
/// special-casing required for void, it is frequently a liability for template
/// metaprogramming. So, instead of writing specializations to handle cases like
/// SomeContainer<void>, a library author may instead rule that out and simply
/// have library users use SomeContainer<Unit>. Contained values may be ignored.
/// Much easier.
///
/// "void" is the type that admits of no values at all. It is not possible to
/// construct a value of this type.
/// "unit" is the type that admits of precisely one unique value. It is
/// possible to construct a value of this type, but it is always the same value
/// every time, so it is uninteresting.
struct Unit {
  /// Compare two Unit values for equality.
  ///
  /// \param other The other Unit value to compare against.
  /// \returns Always `true`, since all Unit values are equal.
  constexpr bool operator==(const Unit& other) const { return true; }
  /// Compare two Unit values for inequality.
  ///
  /// \param other The other Unit value to compare against.
  /// \returns Always `false`, since all Unit values are equal.
  constexpr bool operator!=(const Unit& other) const { return false; }
};

/// The single value of type Unit.
constexpr Unit unit{};

/// Maps a type to itself, and maps `void` to Unit.
///
/// Use this to replace `void` with a real type in template metaprogramming.
template <typename T>
struct lift_unit {
  /// The mapped type: `T` itself.
  using type = T;
};
template <>
struct lift_unit<void> {
  /// The mapped type: Unit, standing in for `void`.
  using type = Unit;
};
/// Alias for the type produced by lift_unit.
template <typename T>
using lift_unit_t = typename lift_unit<T>::type;

/// Maps a type to itself, and maps Unit back to `void`.
///
/// This is the inverse of lift_unit.
template <typename T>
struct drop_unit {
  /// The mapped type: `T` itself.
  using type = T;
};
template <>
struct drop_unit<Unit> {
  /// The mapped type: `void`, recovered from Unit.
  using type = void;
};
/// Alias for the type produced by drop_unit.
template <typename T>
using drop_unit_t = typename drop_unit<T>::type;

} // namespace folly
