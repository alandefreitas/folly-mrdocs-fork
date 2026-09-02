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

#include <compare>
#include <functional>
#include <type_traits>
#include <utility>

#include <folly/Traits.h>
#include <folly/Utility.h>

namespace folly {

template <typename Pointer>
class propagate_const;

/// Access the underlying pointer stored in a mutable wrapper.
///
/// @param obj The wrapper to access.
/// @return A reference to the stored pointer.
template <typename Pointer>
constexpr Pointer& get_underlying(propagate_const<Pointer>& obj) {
  return obj.pointer_;
}

/// Access the underlying pointer stored in a const wrapper.
///
/// @param obj The wrapper to access.
/// @return A const reference to the stored pointer.
template <typename Pointer>
constexpr Pointer const& get_underlying(propagate_const<Pointer> const& obj) {
  return obj.pointer_;
}

namespace detail {
template <class Pointer>
using is_propagate_const = is_instantiation_of<propagate_const, Pointer>;
template <typename T>
using is_decay_propagate_const = is_propagate_const<std::decay_t<T>>;

namespace propagate_const_adl {
using std::swap;
template <typename T>
auto adl_swap(T& a, T& b) noexcept(noexcept(swap(a, b)))
    -> decltype(swap(a, b)) {
  swap(a, b);
}
} // namespace propagate_const_adl
} // namespace detail

/// A const-propagating wrapper around a pointer-like type.
///
/// Mimics `std::experimental::propagate_const` from C++ Library Fundamentals
/// TS v2: accessing the pointee through a const wrapper yields const access.
template <typename Pointer>
class propagate_const {
 public:
  /// The type of the object pointed to by `Pointer`.
  using element_type =
      std::remove_reference_t<decltype(*std::declval<Pointer&>())>;

  /// Default-construct with a value-initialized underlying pointer.
  constexpr propagate_const() = default;
  /// Move-construct from another wrapper.
  ///
  /// @param other The wrapper to move from.
  constexpr propagate_const(propagate_const&& other) = default;
  /// Copy construction is deleted; the wrapper is move-only.
  ///
  /// @param other The wrapper to copy from (this overload is deleted).
  propagate_const(propagate_const const& other) = delete;

  /// Explicitly move-construct from a wrapper over a constructible pointer.
  ///
  /// @param other The wrapper to move from.
  template <typename OtherPointer>
    requires(
        std::is_constructible_v<Pointer, OtherPointer &&> &&
        !std::is_convertible_v<OtherPointer &&, Pointer>)
  constexpr explicit propagate_const(propagate_const<OtherPointer>&& other)
      : pointer_(static_cast<OtherPointer&&>(other.pointer_)) {}

  /// Implicitly move-construct from a wrapper over a convertible pointer.
  ///
  /// @param other The wrapper to move from.
  template <typename OtherPointer>
    requires(
        std::is_constructible_v<Pointer, OtherPointer &&> &&
        std::is_convertible_v<OtherPointer &&, Pointer>)
  constexpr propagate_const(propagate_const<OtherPointer>&& other)
      : pointer_(static_cast<OtherPointer&&>(other.pointer_)) {}

  /// Explicitly construct from a pointer value constructible into `Pointer`.
  ///
  /// @param other The pointer value to store.
  template <typename OtherPointer>
    requires(
        !detail::is_decay_propagate_const<OtherPointer>::value &&
        std::is_constructible_v<Pointer, OtherPointer &&> &&
        !std::is_convertible_v<OtherPointer &&, Pointer>)
  constexpr explicit propagate_const(OtherPointer&& other)
      : pointer_(static_cast<OtherPointer&&>(other)) {}

  /// Implicitly construct from a pointer value convertible to `Pointer`.
  ///
  /// @param other The pointer value to store.
  template <typename OtherPointer>
    requires(
        !detail::is_decay_propagate_const<OtherPointer>::value &&
        std::is_constructible_v<Pointer, OtherPointer &&> &&
        std::is_convertible_v<OtherPointer &&, Pointer>)
  constexpr propagate_const(OtherPointer&& other)
      : pointer_(static_cast<OtherPointer&&>(other)) {}

  /// Move-assign from another wrapper.
  ///
  /// @param other The wrapper to move from.
  /// @return A reference to this wrapper.
  constexpr propagate_const& operator=(propagate_const&& other) = default;
  /// Copy assignment is deleted; the wrapper is move-only.
  ///
  /// @param other The wrapper to copy from (this overload is deleted).
  /// @return A reference to this wrapper.
  propagate_const& operator=(propagate_const const& other) = delete;

  /// Move-assign from a wrapper over a convertible pointer.
  ///
  /// @param other The wrapper to move from.
  /// @return A reference to this wrapper.
  template <typename OtherPointer>
    requires(std::is_convertible_v<OtherPointer &&, Pointer>)
  constexpr propagate_const& operator=(propagate_const<OtherPointer>&& other) {
    pointer_ = static_cast<OtherPointer&&>(other.pointer_);
    return *this;
  }

  /// Assign from a convertible pointer value.
  ///
  /// @param other The pointer value to assign.
  /// @return A reference to this wrapper.
  template <typename OtherPointer>
    requires(
        !detail::is_decay_propagate_const<OtherPointer>::value &&
        std::is_convertible_v<OtherPointer &&, Pointer>)
  constexpr propagate_const& operator=(OtherPointer&& other) {
    pointer_ = static_cast<OtherPointer&&>(other);
    return *this;
  }

  /// Swap the underlying pointer with another wrapper's.
  ///
  /// @param other The wrapper to swap with.
  constexpr void swap(propagate_const& other) noexcept(
      noexcept(detail::propagate_const_adl::adl_swap(
          std::declval<Pointer&>(), other.pointer_))) {
    detail::propagate_const_adl::adl_swap(pointer_, other.pointer_);
  }

  /// Return a mutable pointer to the pointed-to object.
  ///
  /// @return A mutable pointer to the pointed-to object.
  constexpr element_type* get() { return get_(pointer_); }

  /// Return a const pointer to the pointed-to object.
  ///
  /// @return A const pointer to the pointed-to object.
  constexpr element_type const* get() const { return get_(pointer_); }

  /// Test whether the underlying pointer is non-null.
  ///
  /// @return `true` if the underlying pointer is non-null.
  constexpr explicit operator bool() const {
    return static_cast<bool>(pointer_);
  }

  /// Dereference to a mutable reference to the pointed-to object.
  ///
  /// @return A mutable reference to the pointed-to object.
  constexpr element_type& operator*() { return *get(); }

  /// Dereference to a const reference to the pointed-to object.
  ///
  /// @return A const reference to the pointed-to object.
  constexpr element_type const& operator*() const { return *get(); }

  /// Member-access the pointed-to object through a mutable pointer.
  ///
  /// @return A mutable pointer to the pointed-to object.
  constexpr element_type* operator->() { return get(); }

  /// Member-access the pointed-to object through a const pointer.
  ///
  /// @return A const pointer to the pointed-to object.
  constexpr element_type const* operator->() const { return get(); }

  /// Implicitly convert to a mutable pointer to the pointed-to object.
  ///
  /// @return A mutable pointer to the pointed-to object.
  template <typename OtherPointer = Pointer>
    requires(
        std::is_pointer_v<OtherPointer> ||
        std::is_convertible_v<OtherPointer, element_type*>)
  constexpr operator element_type*() {
    return get();
  }

  /// Implicitly convert to a const pointer to the pointed-to object.
  ///
  /// @return A const pointer to the pointed-to object.
  template <typename OtherPointer = Pointer>
    requires(
        std::is_pointer_v<OtherPointer> ||
        std::is_convertible_v<OtherPointer, element_type const*>)
  constexpr operator element_type const*() const {
    return get();
  }

 private:
  /// Grants the free `get_underlying` access to the stored pointer.
  ///
  /// \param pc The instance whose stored pointer is accessed.
  /// \returns A reference to the stored pointer.
  friend constexpr Pointer& get_underlying<>(propagate_const& pc);
  /// Grants the const free `get_underlying` access to the stored pointer.
  ///
  /// \param pc The instance whose stored pointer is accessed.
  /// \returns A const reference to the stored pointer.
  friend constexpr Pointer const& get_underlying<>(propagate_const const& pc);
  template <typename OtherPointer>
  friend class propagate_const;

  template <typename T>
  constexpr static T* get_(T* t) {
    return t;
  }
  template <typename T>
  constexpr static auto get_(T& t) -> decltype(t.get()) {
    return t.get();
  }

  Pointer pointer_;
};

/// Swap the underlying pointers of two wrappers.
///
/// @param a The first wrapper.
/// @param b The second wrapper.
template <typename Pointer>
constexpr void swap(
    propagate_const<Pointer>& a,
    propagate_const<Pointer>& b) noexcept(noexcept(a.swap(b))) {
  a.swap(b);
}

// nullptr comparisons: != and reversed == are synthesized from this by C++20.
/// Equality-compare a wrapper against `nullptr`.
///
/// @param a The wrapper operand.
/// @param np The null pointer literal to compare against.
/// @return `true` if the underlying pointer is null.
template <typename Pointer>
constexpr bool operator==(
    propagate_const<Pointer> const& a, std::nullptr_t np) {
  return get_underlying(a) == nullptr;
}

// Homogeneous comparisons: != is synthesized from ==; <, <=, >, >= are
// synthesized from <=>.
/// Equality-compare two wrappers by their underlying pointers.
///
/// @param a The left-hand wrapper.
/// @param b The right-hand wrapper.
/// @return `true` if the underlying pointers are equal.
template <typename Pointer>
constexpr bool operator==(
    propagate_const<Pointer> const& a, propagate_const<Pointer> const& b) {
  return get_underlying(a) == get_underlying(b);
}

/// Three-way-compare two wrappers by their underlying pointers.
///
/// @param a The left-hand wrapper.
/// @param b The right-hand wrapper.
/// @return The result of comparing the underlying pointers.
template <typename Pointer>
constexpr auto operator<=>(
    propagate_const<Pointer> const& a,
    propagate_const<Pointer> const&
        b) noexcept(noexcept(get_underlying(a) <=> get_underlying(b)))
    -> decltype(get_underlying(a) <=> get_underlying(b)) {
  return get_underlying(a) <=> get_underlying(b);
}

//  Note: contrary to the specification, the heterogeneous comparison operators
//  only participate in overload resolution when the equivalent heterogeneous
//  comparison operators on the underlying pointers, as returned by invocation
//  of get_underlying, would also participate in overload resolution.
//
//  C++20: != is synthesized from ==; the reversed (Other, propagate_const)
//  forms of == and <=> are synthesized automatically; <, <=, >, >= are
//  synthesized from <=>.

/// Equality-compare a wrapper against a value of another type.
///
/// @param a The wrapper operand.
/// @param b The other operand.
/// @return `true` if the underlying pointer equals `b`.
template <typename Pointer, typename Other>
constexpr auto operator==(propagate_const<Pointer> const& a, Other const& b)
    -> decltype(get_underlying(a) == b, false) {
  return get_underlying(a) == b;
}

/// Three-way-compare a wrapper against a value of another type.
///
/// @param a The wrapper operand.
/// @param b The other operand.
/// @return The result of comparing the underlying pointer with `b`.
template <typename Pointer, typename Other>
constexpr auto operator<=>(propagate_const<Pointer> const& a, Other const& b)
    -> decltype(get_underlying(a) <=> b) {
  return get_underlying(a) <=> b;
}

} // namespace folly

/// Standard library customization points for `folly::propagate_const`.
namespace std {

/// `std::hash` specialization for `folly::propagate_const`.
template <typename Pointer>
struct hash<folly::propagate_const<Pointer>> : private hash<Pointer> {
  using hash<Pointer>::hash;

  /// Hash a wrapper by hashing its underlying pointer.
  ///
  /// @param obj The wrapper to hash.
  /// @return The hash of the underlying pointer.
  size_t operator()(folly::propagate_const<Pointer> const& obj) const {
    return hash<Pointer>::operator()(folly::get_underlying(obj));
  }
};

/// `std::equal_to` specialization for `folly::propagate_const`.
template <typename Pointer>
struct equal_to<folly::propagate_const<Pointer>> : private equal_to<Pointer> {
  using equal_to<Pointer>::equal_to;

  /// Compare two wrapped pointers using `std::equal_to`.
  ///
  /// @param a The left-hand wrapper.
  /// @param b The right-hand wrapper.
  /// @return `true` if `a`'s pointer equals `b`'s pointer.
  constexpr bool operator()(
      folly::propagate_const<Pointer> const& a,
      folly::propagate_const<Pointer> const& b) {
    return equal_to<Pointer>::operator()(
        folly::get_underlying(a), folly::get_underlying(b));
  }
};

/// `std::not_equal_to` specialization for `folly::propagate_const`.
template <typename Pointer>
struct not_equal_to<folly::propagate_const<Pointer>>
    : private not_equal_to<Pointer> {
  using not_equal_to<Pointer>::not_equal_to;

  /// Compare two wrapped pointers using `std::not_equal_to`.
  ///
  /// @param a The left-hand wrapper.
  /// @param b The right-hand wrapper.
  /// @return `true` if `a`'s pointer differs from `b`'s pointer.
  constexpr bool operator()(
      folly::propagate_const<Pointer> const& a,
      folly::propagate_const<Pointer> const& b) {
    return not_equal_to<Pointer>::operator()(
        folly::get_underlying(a), folly::get_underlying(b));
  }
};

/// `std::less` specialization for `folly::propagate_const`.
template <typename Pointer>
struct less<folly::propagate_const<Pointer>> : private less<Pointer> {
  using less<Pointer>::less;

  /// Compare two wrapped pointers using `std::less`.
  ///
  /// @param a The left-hand wrapper.
  /// @param b The right-hand wrapper.
  /// @return `true` if `a`'s pointer is less than `b`'s pointer.
  constexpr bool operator()(
      folly::propagate_const<Pointer> const& a,
      folly::propagate_const<Pointer> const& b) {
    return less<Pointer>::operator()(
        folly::get_underlying(a), folly::get_underlying(b));
  }
};

/// `std::greater` specialization for `folly::propagate_const`.
template <typename Pointer>
struct greater<folly::propagate_const<Pointer>> : private greater<Pointer> {
  using greater<Pointer>::greater;

  /// Compare two wrapped pointers using `std::greater`.
  ///
  /// @param a The left-hand wrapper.
  /// @param b The right-hand wrapper.
  /// @return `true` if `a`'s pointer is greater than `b`'s pointer.
  constexpr bool operator()(
      folly::propagate_const<Pointer> const& a,
      folly::propagate_const<Pointer> const& b) {
    return greater<Pointer>::operator()(
        folly::get_underlying(a), folly::get_underlying(b));
  }
};

/// `std::less_equal` specialization for `folly::propagate_const`.
template <typename Pointer>
struct less_equal<folly::propagate_const<Pointer>>
    : private less_equal<Pointer> {
  using less_equal<Pointer>::less_equal;

  /// Compare two wrapped pointers using `std::less_equal`.
  ///
  /// @param a The left-hand wrapper.
  /// @param b The right-hand wrapper.
  /// @return `true` if `a`'s pointer is not greater than `b`'s pointer.
  constexpr bool operator()(
      folly::propagate_const<Pointer> const& a,
      folly::propagate_const<Pointer> const& b) {
    return less_equal<Pointer>::operator()(
        folly::get_underlying(a), folly::get_underlying(b));
  }
};

/// `std::greater_equal` specialization for `folly::propagate_const`.
template <typename Pointer>
struct greater_equal<folly::propagate_const<Pointer>>
    : private greater_equal<Pointer> {
  using greater_equal<Pointer>::greater_equal;

  /// Compare two wrapped pointers using `std::greater_equal`.
  ///
  /// @param a The left-hand wrapper.
  /// @param b The right-hand wrapper.
  /// @return `true` if `a`'s pointer is not less than `b`'s pointer.
  constexpr bool operator()(
      folly::propagate_const<Pointer> const& a,
      folly::propagate_const<Pointer> const& b) {
    return greater_equal<Pointer>::operator()(
        folly::get_underlying(a), folly::get_underlying(b));
  }
};

} // namespace std
