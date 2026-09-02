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

#include <folly/Poly.h>

/// The Folly library.
namespace folly {
/// Interfaces and helpers for the `Poly` type-erasure utility.
namespace poly {
/**
 * A `Poly` interface for types that are equality comparable.
 */
struct IEqualityComparable : PolyExtends<> {
  /// Tests two values for equality.
  ///
  /// \param _this The left-hand value.
  /// \param that The right-hand value.
  /// \returns `true` if the values are equal.
  template <class T>
  static auto isEqual_(T const& _this, T const& that)
      -> decltype(std::declval<bool (&)(bool)>()(_this == that)) {
    return _this == that;
  }

  /// The set of member functions this interface requires.
  template <class T>
  using Members = FOLLY_POLY_MEMBERS(&isEqual_<T>);
};

/**
 * A `Poly` interface for types that are strictly orderable.
 */
struct IStrictlyOrderable : PolyExtends<> {
  /// Tests whether one value orders before another.
  ///
  /// \param _this The left-hand value.
  /// \param that The right-hand value.
  /// \returns `true` if `_this` is less than `that`.
  template <class T>
  static auto isLess_(T const& _this, T const& that)
      -> decltype(std::declval<bool (&)(bool)>()(_this < that)) {
    return _this < that;
  }

  /// The set of member functions this interface requires.
  template <class T>
  using Members = FOLLY_POLY_MEMBERS(&isLess_<T>);
};

} // namespace poly

/// \cond
namespace detail {
template <class I1, class I2>
using Comparable = std::conjunction<
    std::is_same<std::decay_t<I1>, std::decay_t<I2>>,
    std::is_base_of<poly::IEqualityComparable, std::decay_t<I1>>>;

template <class I1, class I2>
using Orderable = std::conjunction<
    std::is_same<std::decay_t<I1>, std::decay_t<I2>>,
    std::is_base_of<poly::IStrictlyOrderable, std::decay_t<I1>>>;
} // namespace detail
/// \endcond

template <
    class I1,
    class I2,
    std::enable_if_t<detail::Comparable<I1, I2>::value, int> = 0>
/// Tests two `Poly` objects for equality.
///
/// \param _this The left-hand operand.
/// \param that The right-hand operand.
/// \returns `true` if both objects are empty or hold equal values.
bool operator==(Poly<I1> const& _this, Poly<I2> const& that) {
  if (poly_empty(_this) != poly_empty(that)) {
    return false;
  } else if (poly_empty(_this)) {
    return true;
  } else if (poly_type(_this) != poly_type(that)) {
    throw BadPolyCast();
  }
  return ::folly::poly_call<0, poly::IEqualityComparable>(_this, that);
}

template <
    class I1,
    class I2,
    std::enable_if_t<detail::Comparable<I1, I2>::value, int> = 0>
/// Tests two `Poly` objects for inequality.
///
/// \param _this The left-hand operand.
/// \param that The right-hand operand.
/// \returns `true` if the objects are not equal.
bool operator!=(Poly<I1> const& _this, Poly<I2> const& that) {
  return !(_this == that);
}

template <
    class I1,
    class I2,
    std::enable_if_t<detail::Orderable<I1, I2>::value, int> = 0>
/// Orders two `Poly` objects.
///
/// \param _this The left-hand operand.
/// \param that The right-hand operand.
/// \returns `true` if `_this` orders before `that`.
bool operator<(Poly<I1> const& _this, Poly<I2> const& that) {
  if (poly_empty(that)) {
    return false;
  } else if (poly_empty(_this)) {
    return true;
  } else if (poly_type(_this) != poly_type(that)) {
    throw BadPolyCast{};
  }
  return ::folly::poly_call<0, poly::IStrictlyOrderable>(_this, that);
}

template <
    class I1,
    class I2,
    std::enable_if_t<detail::Orderable<I1, I2>::value, int> = 0>
/// Orders two `Poly` objects.
///
/// \param _this The left-hand operand.
/// \param that The right-hand operand.
/// \returns `true` if `_this` orders after `that`.
bool operator>(Poly<I1> const& _this, Poly<I2> const& that) {
  return that < _this;
}

template <
    class I1,
    class I2,
    std::enable_if_t<detail::Orderable<I1, I2>::value, int> = 0>
/// Orders two `Poly` objects.
///
/// \param _this The left-hand operand.
/// \param that The right-hand operand.
/// \returns `true` if `_this` does not order after `that`.
bool operator<=(Poly<I1> const& _this, Poly<I2> const& that) {
  return !(that < _this);
}

template <
    class I1,
    class I2,
    std::enable_if_t<detail::Orderable<I1, I2>::value, int> = 0>
/// Orders two `Poly` objects.
///
/// \param _this The left-hand operand.
/// \param that The right-hand operand.
/// \returns `true` if `_this` does not order before `that`.
bool operator>=(Poly<I1> const& _this, Poly<I2> const& that) {
  return !(_this < that);
}

namespace poly {
/**
 * A `Poly` interface for types that are move-only.
 */
struct IMoveOnly : PolyExtends<> {
  /// Interface members injected into each move-only `Poly`.
  template <class Base>
  struct Interface : Base {
    /// Constructs an empty interface object.
    Interface() = default;
    /// Deleted copy constructor; move-only types cannot be copied.
    ///
    /// \param other The object that would be copied.
    Interface(Interface const& other) = delete;
    /// Move constructor.
    ///
    /// \param other The object to move from.
    Interface(Interface&& other) = default;
    /// Deleted copy assignment; move-only types cannot be copied.
    ///
    /// \param other The object that would be assigned from.
    /// \returns A reference to this object.
    Interface& operator=(Interface const& other) = delete;
    /// Move assignment.
    ///
    /// \param other The object to move from.
    /// \returns A reference to this object.
    Interface& operator=(Interface&& other) = default;
    using Base::Base;
  };
};

/**
 * A `Poly` interface for types that are copyable and movable.
 */
struct ISemiRegular : PolyExtends<> {};

/**
 * A `Poly` interface for types that are copyable, movable, and equality
 * comparable.
 */
struct IRegular : PolyExtends<ISemiRegular, IEqualityComparable> {};
} // namespace poly
} // namespace folly
