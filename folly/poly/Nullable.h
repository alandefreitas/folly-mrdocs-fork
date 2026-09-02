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
#include <folly/poly/Regular.h>

/// The Folly library.
namespace folly {
/// Interfaces and helpers for the `Poly` type-erasure utility.
namespace poly {
/**
 * A `Poly` interface that can be used to make Poly objects initializable from
 * `nullptr` (to create an empty `Poly`) and equality comparable to `nullptr`
 * (to test for emptiness).
 */
struct INullablePointer : PolyExtends<IEqualityComparable> {
  /// Interface members injected into each `Poly` that models this concept.
  template <class Base>
  struct Interface : Base {
    /// Constructs an empty interface object.
    Interface() = default;
    using Base::Base;

    /// Constructs an empty `Poly` from `nullptr`.
    ///
    /// \param p A null pointer constant.
    /* implicit */ Interface(std::nullptr_t p) : Base{} {
      static_assert(
          std::is_default_constructible<PolySelf<Base>>::value,
          "Cannot initialize a non-default constructible Poly with nullptr");
    }

    /// Resets the `Poly` object to the empty state.
    ///
    /// \param p A null pointer constant.
    /// \returns A reference to the emptied `Poly` object.
    PolySelf<Base>& operator=(std::nullptr_t p) {
      static_assert(
          std::is_default_constructible<PolySelf<Base>>::value,
          "Cannot initialize a non-default constructible Poly with nullptr");
      auto& self = static_cast<PolySelf<Base>&>(*this);
      self = PolySelf<Base>();
      return self;
    }

    /// Tests whether a `Poly` object is empty.
    ///
    /// \param lhs A null pointer constant.
    /// \param self The `Poly` object to test.
    /// \returns `true` if `self` is empty.
    friend bool operator==(
        std::nullptr_t lhs, PolySelf<Base> const& self) noexcept {
      return poly_empty(self);
    }
    /// Tests whether a `Poly` object is empty.
    ///
    /// \param self The `Poly` object to test.
    /// \param rhs A null pointer constant.
    /// \returns `true` if `self` is empty.
    friend bool operator==(
        PolySelf<Base> const& self, std::nullptr_t rhs) noexcept {
      return poly_empty(self);
    }
    /// Tests whether a `Poly` object is non-empty.
    ///
    /// \param lhs A null pointer constant.
    /// \param self The `Poly` object to test.
    /// \returns `true` if `self` holds a value.
    friend bool operator!=(
        std::nullptr_t lhs, PolySelf<Base> const& self) noexcept {
      return !poly_empty(self);
    }
    /// Tests whether a `Poly` object is non-empty.
    ///
    /// \param self The `Poly` object to test.
    /// \param rhs A null pointer constant.
    /// \returns `true` if `self` holds a value.
    friend bool operator!=(
        PolySelf<Base> const& self, std::nullptr_t rhs) noexcept {
      return !poly_empty(self);
    }
  };
};

/**
 * A `Poly` interface that can be used to make `Poly` objects contextually
 * convertible to `bool` (`true` if and only if non-empty). It also gives
 * `Poly` objects a unary logical negation operator.
 */
struct IBooleanTestable : PolyExtends<> {
  /// Interface members injected into each `Poly` that models this concept.
  template <class Base>
  struct Interface : Base {
    /// Tests whether the `Poly` object is empty.
    ///
    /// \returns `true` if the object is empty, `false` otherwise.
    constexpr bool operator!() const noexcept { return poly_empty(*this); }
    /// Tests whether the `Poly` object is non-empty.
    ///
    /// \returns `true` if the object holds a value, `false` otherwise.
    constexpr explicit operator bool() const noexcept { return !!*this; }
  };
};
} // namespace poly
} // namespace folly
