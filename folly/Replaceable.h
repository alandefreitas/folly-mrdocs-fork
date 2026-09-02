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

#include <initializer_list>
#include <new>
#include <type_traits>
#include <utility>

#include <folly/Portability.h>
#include <folly/Traits.h>
#include <folly/Utility.h>

/**
 * An instance of `Replaceable<T>` wraps an instance of `T`.
 *
 * You access the inner `T` instance with `operator*` and `operator->` (as if
 * it were a smart pointer).
 *
 * `Replaceable<T>` adds no indirection cost and performs no allocations.
 *
 * `Replaceable<T>` has the same size and alignment as `T`.
 *
 * You can replace the `T` within a `Replaceable<T>` using the `emplace` method
 * (presuming that it is constructible and destructible without throwing
 * exceptions). If the destructor or constructor you're using could throw an
 * exception you should use `Optional<T>` instead, as it's not a logic error
 * for that to be empty.
 *
 * Frequently Asked Questions
 * ==========================
 *
 * Why does this need to be so complicated?
 * ----------------------------------------
 *
 * If a `T` instance contains `const`-qualified member variables or reference
 * member variables we can't safely replace a `T` instance by destructing it
 * manually and using placement new. This is because compilers are permitted to
 * assume that the `const` or reference members of a named, referenced, or
 * pointed-to object do not change.
 *
 * For pointed-to objects in allocated storage you can use the pointer returned
 * by placement new or use the `launder` function to get a pointer to the new
 * object.  Note that `launder` doesn't affect its argument, it's still
 * undefined behaviour to use the original pointer. And none of this helps if
 * the object is a local or a member variable because the destructor call will
 * not have been laundered. In summary, this is the only way to use placement
 * new that is both simple and safe:
 *
 *      T* pT = new T(...);
 *      pT->~T();
 *      pT = ::new (pT) T(...);
 *      delete pT;
 *
 * What are the other safe solutions to this problem?
 * --------------------------------------------------
 *
 * * Ask the designer of `T` to de-`const` and -`reference` the members of `T`.
 *  - Makes `T` harder to reason about
 *  - Can reduce the performance of `T` methods
 *  - They can refuse to make the change
 * * Put the `T` on the heap and use a raw/unique/shared pointer.
 *  - Adds a level of indirection, costing performance.
 *  - Harder to reason about your code as you need to check for nullptr.
 * * Put the `T` in an `Optional`.
 *  - Harder to reason about your code as you need to check for None.
 * * Pass the problem on, making the new code also not-replaceable
 *  - Contagion is not really a solution
 *
 * Are there downsides to this?
 * ----------------------------
 *
 * There is a potential performance penalty after converting `T` to
 * `Replaceable<T>` if you have non-`T`-member-function code which repeatedly
 * examines the value of a `const` or `reference` data member of `T`, because
 * the compiler now has to look at the value each time whereas previously it
 * was permitted to load it once up-front and presume that it could never
 * change.
 *
 * Usage notes
 * ===========
 *
 * Don't store a reference to the `T` within a `Replaceable<T>` unless you can
 * show that its lifetime does not cross an `emplace` call. For safety a
 * reasonable rule is to always use `operator*()` to get a fresh temporary each
 * time you need a `T&.
 *
 * If you store a pointer to the `T` within a `Replaceable<T>` you **must**
 * launder it after each call to `emplace` before using it. Again you can
 * reasonably choose to always use `operator->()` to get a fresh temporary each
 * time you need a `T*.
 *
 * Thus far I haven't thought of a good reason to use `Replaceable<T>` or
 * `Replaceable<T> const&` as a function parameter type.
 *
 * `Replaceable<T>&` can make sense to pass to a function that conditionally
 * replaces the `T`, where `T` has `const` or reference member variables.
 *
 * The main use of `Replaceable<T>` is as a class member type or a local type
 * in long-running functions.
 *
 * It's probably time to rethink your design choices if you end up with
 * `Replaceable<Replaceable<T>>`, `Optional<Replaceable<T>>`,
 * `Replaceable<Optional<T>>`, `unique_ptr<Replaceable<T>>` etc. except as a
 *  result of template expansion.
 */

namespace folly {

template <class T>
class Replaceable;

/// Implementation details for `Replaceable`; not part of the public API.
namespace replaceable_detail {
/* Mixin templates to give `replaceable<T>` the following properties:
 *
 * 1. Trivial destructor if `T` has a trivial destructor; user-provided
 *    otherwise
 * 2. Move constructor if `T` has a move constructor; deleted otherwise
 * 3. Move assignment operator if `T` has a move constructor; deleted
 *    otherwise
 * 4. Copy constructor if `T` has a copy constructor; deleted otherwise
 * 5. Copy assignment operator if `T` has a copy constructor; deleted
 *    otherwise
 *
 * Has to be done in this way because we can't `enable_if` them away
 */
/// Mixin supplying a destructor for `Replaceable<T>` that matches the
/// destructibility of `T`.
template <
    class T,
    bool = std::is_destructible<T>::value,
    bool = std::is_trivially_destructible<T>::value>
struct dtor_mixin;

/* Destructible and trivially destructible */
template <class T>
struct dtor_mixin<T, true, true> {};

/* Destructible and not trivially destructible */
template <class T>
struct dtor_mixin<T, true, false> {
  dtor_mixin() = default;
  dtor_mixin(dtor_mixin&&) = default;
  dtor_mixin(dtor_mixin const&) = default;
  dtor_mixin& operator=(dtor_mixin&&) = default;
  dtor_mixin& operator=(dtor_mixin const&) = default;
  ~dtor_mixin() noexcept(std::is_nothrow_destructible<T>::value) {
    T* destruct_ptr = std::launder(
        reinterpret_cast<T*>(
            reinterpret_cast<Replaceable<T>*>(this)->storage_));
    destruct_ptr->~T();
  }
};

/* Not destructible */
template <class T, bool A>
struct dtor_mixin<T, false, A> {
  dtor_mixin() = default;
  dtor_mixin(dtor_mixin&&) = default;
  dtor_mixin(dtor_mixin const&) = default;
  dtor_mixin& operator=(dtor_mixin&&) = default;
  dtor_mixin& operator=(dtor_mixin const&) = default;
  ~dtor_mixin() = delete;
};

/// Mixin supplying default and move constructors for `Replaceable<T>` that
/// match the corresponding properties of `T`.
template <
    class T,
    bool = std::is_default_constructible<T>::value,
    bool = std::is_move_constructible<T>::value>
struct default_and_move_ctor_mixin;

/* Not default-constructible and not move-constructible */
template <class T>
struct default_and_move_ctor_mixin<T, false, false> {
  default_and_move_ctor_mixin() = delete;
  default_and_move_ctor_mixin(default_and_move_ctor_mixin&&) = delete;
  default_and_move_ctor_mixin(default_and_move_ctor_mixin const&) = default;
  default_and_move_ctor_mixin& operator=(default_and_move_ctor_mixin&&) =
      default;
  default_and_move_ctor_mixin& operator=(default_and_move_ctor_mixin const&) =
      default;

 protected:
  inline explicit default_and_move_ctor_mixin(int) {}
};

/* Default-constructible and move-constructible */
template <class T>
struct default_and_move_ctor_mixin<T, true, true> {
  inline default_and_move_ctor_mixin() noexcept(
      std::is_nothrow_constructible<T>::value) {
    ::new (reinterpret_cast<Replaceable<T>*>(this)->storage_) T();
  }
  inline default_and_move_ctor_mixin(
      default_and_move_ctor_mixin&&
          other) noexcept(std::is_nothrow_constructible<T, T&&>::value) {
    ::new (reinterpret_cast<Replaceable<T>*>(this)->storage_)
        T(*std::move(reinterpret_cast<Replaceable<T>&>(other)));
  }
  default_and_move_ctor_mixin(default_and_move_ctor_mixin const&) = default;
  default_and_move_ctor_mixin& operator=(default_and_move_ctor_mixin&&) =
      default;
  inline default_and_move_ctor_mixin& operator=(
      default_and_move_ctor_mixin const&) = default;

 protected:
  inline explicit default_and_move_ctor_mixin(int) {}
};

/* Default-constructible and not move-constructible */
template <class T>
struct default_and_move_ctor_mixin<T, true, false> {
  inline default_and_move_ctor_mixin() noexcept(
      std::is_nothrow_constructible<T>::value) {
    ::new (reinterpret_cast<Replaceable<T>*>(this)->storage_) T();
  }
  default_and_move_ctor_mixin(default_and_move_ctor_mixin&&) = delete;
  default_and_move_ctor_mixin(default_and_move_ctor_mixin const&) = default;
  default_and_move_ctor_mixin& operator=(default_and_move_ctor_mixin&&) =
      default;
  default_and_move_ctor_mixin& operator=(default_and_move_ctor_mixin const&) =
      default;

 protected:
  inline explicit default_and_move_ctor_mixin(int) {}
};

/* Not default-constructible but is move-constructible */
template <class T>
struct default_and_move_ctor_mixin<T, false, true> {
  default_and_move_ctor_mixin() = delete;
  inline default_and_move_ctor_mixin(
      default_and_move_ctor_mixin&&
          other) noexcept(std::is_nothrow_constructible<T, T&&>::value) {
    ::new (reinterpret_cast<Replaceable<T>*>(this)->storage_)
        T(*std::move(reinterpret_cast<Replaceable<T>&>(other)));
  }
  default_and_move_ctor_mixin(default_and_move_ctor_mixin const&) = default;
  default_and_move_ctor_mixin& operator=(default_and_move_ctor_mixin&&) =
      default;
  default_and_move_ctor_mixin& operator=(default_and_move_ctor_mixin const&) =
      default;

 protected:
  inline explicit default_and_move_ctor_mixin(int) {}
};

/// Mixin supplying the move-assignment operator for `Replaceable<T>` that
/// matches the corresponding properties of `T`.
template <
    class T,
    bool = (std::is_destructible<T>::value) &&
        (std::is_move_constructible<T>::value)>
struct move_assignment_mixin;

/* Not (destructible and move-constructible) */
template <class T>
struct move_assignment_mixin<T, false> {
  move_assignment_mixin() = default;
  move_assignment_mixin(move_assignment_mixin&&) = default;
  move_assignment_mixin(move_assignment_mixin const&) = default;
  move_assignment_mixin& operator=(move_assignment_mixin&&) = delete;
  move_assignment_mixin& operator=(move_assignment_mixin const&) = default;
};

/* Both destructible and move-constructible */
template <class T>
struct move_assignment_mixin<T, true> {
  move_assignment_mixin() = default;
  move_assignment_mixin(move_assignment_mixin&&) = default;
  move_assignment_mixin(move_assignment_mixin const&) = default;
  inline move_assignment_mixin&
  operator=(move_assignment_mixin&& other) noexcept(
      std::is_nothrow_destructible<T>::value &&
      std::is_nothrow_move_constructible<T>::value) {
    T* destruct_ptr = std::launder(
        reinterpret_cast<T*>(
            reinterpret_cast<Replaceable<T>*>(this)->storage_));
    destruct_ptr->~T();
    ::new (reinterpret_cast<Replaceable<T>*>(this)->storage_)
        T(*std::move(reinterpret_cast<Replaceable<T>&>(other)));
    return *this;
  }
  move_assignment_mixin& operator=(move_assignment_mixin const&) = default;
};

/// Mixin supplying the copy constructor for `Replaceable<T>` that matches the
/// copy-constructibility of `T`.
template <class T, bool = std::is_copy_constructible<T>::value>
struct copy_ctor_mixin;

/* Not copy-constructible */
template <class T>
struct copy_ctor_mixin<T, false> {
  copy_ctor_mixin() = default;
  copy_ctor_mixin(copy_ctor_mixin&&) = default;
  copy_ctor_mixin(copy_ctor_mixin const&) = delete;
  copy_ctor_mixin& operator=(copy_ctor_mixin&&) = default;
  copy_ctor_mixin& operator=(copy_ctor_mixin const&) = delete;
};

/* Copy-constructible */
template <class T>
struct copy_ctor_mixin<T, true> {
  copy_ctor_mixin() = default;
  inline copy_ctor_mixin(copy_ctor_mixin const& other) noexcept(
      std::is_nothrow_constructible<T, T const&>::value) {
    ::new (reinterpret_cast<Replaceable<T>*>(this)->storage_)
        T(*reinterpret_cast<Replaceable<T> const&>(other));
  }
  copy_ctor_mixin(copy_ctor_mixin&&) = default;
  copy_ctor_mixin& operator=(copy_ctor_mixin&&) = default;
  copy_ctor_mixin& operator=(copy_ctor_mixin const&) = default;
};

/// Mixin supplying the copy-assignment operator for `Replaceable<T>` that
/// matches the corresponding properties of `T`.
template <
    class T,
    bool = (std::is_destructible<T>::value) &&
        (std::is_copy_constructible<T>::value)>
struct copy_assignment_mixin;

/* Not (destructible and copy-constructible) */
template <class T>
struct copy_assignment_mixin<T, false> {
  copy_assignment_mixin() = default;
  copy_assignment_mixin(copy_assignment_mixin&&) = default;
  copy_assignment_mixin(copy_assignment_mixin const&) = default;
  copy_assignment_mixin& operator=(copy_assignment_mixin&&) = default;
  copy_assignment_mixin& operator=(copy_assignment_mixin const&) = delete;
};

/* Both destructible and copy-constructible */
template <class T>
struct copy_assignment_mixin<T, true> {
  copy_assignment_mixin() = default;
  copy_assignment_mixin(copy_assignment_mixin&&) = default;
  copy_assignment_mixin(copy_assignment_mixin const&) = default;
  copy_assignment_mixin& operator=(copy_assignment_mixin&&) = default;
  inline copy_assignment_mixin&
  operator=(copy_assignment_mixin const& other) noexcept(
      std::is_nothrow_destructible<T>::value &&
      std::is_nothrow_copy_constructible<T>::value) {
    T* destruct_ptr = std::launder(
        reinterpret_cast<T*>(
            reinterpret_cast<Replaceable<T>*>(this)->storage_));
    destruct_ptr->~T();
    ::new (reinterpret_cast<Replaceable<T>*>(this)->storage_)
        T(*reinterpret_cast<Replaceable<T> const&>(other));
    return *this;
  }
};

/// Trait that is true if `T` is constructible from any `Replaceable<T>`
/// reference.
template <typename T>
struct is_constructible_from_replaceable
    : std::bool_constant<
          std::is_constructible<T, Replaceable<T>&>::value ||
          std::is_constructible<T, Replaceable<T>&&>::value ||
          std::is_constructible<T, const Replaceable<T>&>::value ||
          std::is_constructible<T, const Replaceable<T>&&>::value> {};

/// Trait that is true if any `Replaceable<T>` reference is convertible to `T`.
template <typename T>
struct is_convertible_from_replaceable
    : std::bool_constant<
          std::is_convertible<Replaceable<T>&, T>::value ||
          std::is_convertible<Replaceable<T>&&, T>::value ||
          std::is_convertible<const Replaceable<T>&, T>::value ||
          std::is_convertible<const Replaceable<T>&&, T>::value> {};
} // namespace replaceable_detail

/// Trait that is true if `T` is an instantiation of `Replaceable`.
template <class T>
using is_replaceable = is_instantiation_of<Replaceable, T>;

/**
 * Makes a `Replaceable` with a type deduced from its input.
 *
 * @param t The value used to construct the contained object.
 * @return A `Replaceable` holding a copy of `t`.
 */
template <class T>
constexpr Replaceable<std::decay_t<T>> make_replaceable(T&& t) {
  return Replaceable<std::decay_t<T>>(std::forward<T>(t));
}

/**
 * Makes a `Replaceable<T>` by in-place constructing `T` from the arguments.
 *
 * @param args Arguments forwarded to the `T` constructor.
 * @return A `Replaceable<T>` holding the newly constructed object.
 */
template <class T, class... Args>
constexpr Replaceable<T> make_replaceable(Args&&... args) {
  return Replaceable<T>(std::in_place, std::forward<Args>(args)...);
}

/**
 * Makes a `Replaceable<T>` by in-place constructing `T` from an initializer
 * list and additional arguments.
 *
 * @param il The initializer list forwarded to the `T` constructor.
 * @param args Additional arguments forwarded to the `T` constructor.
 * @return A `Replaceable<T>` holding the newly constructed object.
 */
template <class T, class U, class... Args>
constexpr Replaceable<T> make_replaceable(
    std::initializer_list<U> il, Args&&... args) {
  return Replaceable<T>(std::in_place, il, std::forward<Args>(args)...);
}

/// Wraps a `T` that can be replaced in place without allocation or indirection.
template <class T>
class alignas(T) Replaceable
    : public replaceable_detail::dtor_mixin<T>,
      public replaceable_detail::default_and_move_ctor_mixin<T>,
      public replaceable_detail::copy_ctor_mixin<T>,
      public replaceable_detail::move_assignment_mixin<T>,
      public replaceable_detail::copy_assignment_mixin<T> {
  using ctor_base = replaceable_detail::default_and_move_ctor_mixin<T>;

 public:
  /// The type of the wrapped object.
  using value_type = T;

  /// Default-constructs the contained object.
  constexpr Replaceable() = default;
  /// Copy-constructs from another `Replaceable`.
  ///
  /// \param other The object to copy from.
  constexpr Replaceable(const Replaceable& other) = default;
  /// Move-constructs from another `Replaceable`.
  ///
  /// \param other The object to move from.
  constexpr Replaceable(Replaceable&& other) = default;

  /// Copy-assigns by replacing the contained object with a copy of the other's.
  ///
  /// \param other The object to copy from.
  /// \returns A reference to this object.
  Replaceable& operator=(const Replaceable& other) = default;
  /// Move-assigns by replacing the contained object with the other's.
  ///
  /// \param other The object to move from.
  /// \returns A reference to this object.
  Replaceable& operator=(Replaceable&& other) = default;

  /// Destroys the contained object.
  ~Replaceable() = default;

  /**
   * In-place constructs the contained `T` from the given arguments.
   *
   * These are modeled very closely on the definition of `std::optional` in
   * C++17.
   *
   * @param tag Disambiguation tag selecting in-place construction.
   * @param args Arguments forwarded to the `T` constructor.
   */
  template <class... Args>
    requires(std::is_constructible<T, Args && ...>::value)
  constexpr explicit Replaceable(std::in_place_t tag, Args&&... args)
      // clang-format off
      noexcept(std::is_nothrow_constructible<T, Args&&...>::value)
      // clang-format on
      : ctor_base(0) {
    ::new (storage_) T(std::forward<Args>(args)...);
  }

  /**
   * In-place constructs the contained `T` from an initializer list and
   * additional arguments.
   *
   * @param tag Disambiguation tag selecting in-place construction.
   * @param il The initializer list forwarded to the `T` constructor.
   * @param args Additional arguments forwarded to the `T` constructor.
   */
  template <class U, class... Args>
    requires(
        std::is_constructible<T, std::initializer_list<U>, Args && ...>::value)
  constexpr explicit Replaceable(
      std::in_place_t tag, std::initializer_list<U> il, Args&&... args)
      // clang-format off
      noexcept(std::is_nothrow_constructible<
          T,
          std::initializer_list<U>,
          Args&&...>::value)
      // clang-format on
      : ctor_base(0) {
    ::new (storage_) T(il, std::forward<Args>(args)...);
  }

  /**
   * Implicitly constructs the contained `T` from a convertible value.
   *
   * @param other The value forwarded to the `T` constructor.
   */
  template <class U = T>
    requires(
        std::is_constructible<T, U &&>::value &&
        !std::is_same<std::decay_t<U>, std::in_place_t>::value &&
        !std::is_same<Replaceable<T>, std::decay_t<U>>::value &&
        std::is_convertible<U &&, T>::value)
  constexpr /* implicit */ Replaceable(U&& other)
      // clang-format off
      noexcept(std::is_nothrow_constructible<T, U&&>::value)
      // clang-format on
      : ctor_base(0) {
    ::new (storage_) T(std::forward<U>(other));
  }

  /**
   * Explicitly constructs the contained `T` from a non-convertible value.
   *
   * @param other The value forwarded to the `T` constructor.
   */
  template <class U = T>
    requires(
        std::is_constructible<T, U &&>::value &&
        !std::is_same<std::decay_t<U>, std::in_place_t>::value &&
        !std::is_same<Replaceable<T>, std::decay_t<U>>::value &&
        !std::is_convertible<U &&, T>::value)
  constexpr explicit Replaceable(U&& other)
      // clang-format off
      noexcept(std::is_nothrow_constructible<T, U&&>::value)
      // clang-format on
      : ctor_base(0) {
    ::new (storage_) T(std::forward<U>(other));
  }

  /**
   * Implicitly constructs the contained `T` by copying from another
   * `Replaceable`.
   *
   * @param other The `Replaceable` whose value is copied.
   */
  template <class U>
    requires(
        std::is_constructible<T, const U&>::value &&
        !replaceable_detail::is_constructible_from_replaceable<T>::value &&
        !replaceable_detail::is_convertible_from_replaceable<T>::value &&
        std::is_convertible<const U&, T>::value)
  /* implicit */ Replaceable(const Replaceable<U>& other)
      // clang-format off
      noexcept(std::is_nothrow_constructible<T, U const&>::value)
      // clang-format on
      : ctor_base(0) {
    ::new (storage_) T(*other);
  }

  /**
   * Explicitly constructs the contained `T` by copying from another
   * `Replaceable`.
   *
   * @param other The `Replaceable` whose value is copied.
   */
  template <class U>
    requires(
        std::is_constructible<T, const U&>::value &&
        !replaceable_detail::is_constructible_from_replaceable<T>::value &&
        !replaceable_detail::is_convertible_from_replaceable<T>::value &&
        !std::is_convertible<const U&, T>::value)
  explicit Replaceable(const Replaceable<U>& other)
      // clang-format off
      noexcept(std::is_nothrow_constructible<T, U const&>::value)
      // clang-format on
      : ctor_base(0) {
    ::new (storage_) T(*other);
  }

  /**
   * Implicitly constructs the contained `T` by moving from another
   * `Replaceable`.
   *
   * @param other The `Replaceable` whose value is moved.
   */
  template <class U>
    requires(
        std::is_constructible<T, U &&>::value &&
        !replaceable_detail::is_constructible_from_replaceable<T>::value &&
        !replaceable_detail::is_convertible_from_replaceable<T>::value &&
        std::is_convertible<U &&, T>::value)
  /* implicit */ Replaceable(Replaceable<U>&& other)
      // clang-format off
      noexcept(std::is_nothrow_constructible<T, U&&>::value)
      // clang-format on
      : ctor_base(0) {
    ::new (storage_) T(std::move(*other));
  }

  /**
   * Explicitly constructs the contained `T` by moving from another
   * `Replaceable`.
   *
   * @param other The `Replaceable` whose value is moved.
   */
  template <class U>
    requires(
        std::is_constructible<T, U &&>::value &&
        !replaceable_detail::is_constructible_from_replaceable<T>::value &&
        !replaceable_detail::is_convertible_from_replaceable<T>::value &&
        !std::is_convertible<U &&, T>::value)
  explicit Replaceable(Replaceable<U>&& other)
      // clang-format off
      noexcept(std::is_nothrow_constructible<T, U&&>::value)
      // clang-format on
      : ctor_base(0) {
    ::new (storage_) T(std::move(*other));
  }

  /**
   * `emplace` destructs the contained object and in-place constructs the
   * replacement.
   *
   * The destructor must not throw (as usual). The constructor must not throw
   * because that would violate the invariant that a `Replaceable<T>` always
   * contains a T instance.
   *
   * As these methods are `noexcept` the program will be terminated if an
   * exception is thrown. If you are encountering this issue you should look at
   * using `Optional` instead.
   *
   * @param args Arguments forwarded to the `T` constructor.
   * @return A reference to the newly constructed contained object.
   */
  template <class... Args>
  T& emplace(Args&&... args) noexcept {
    T* destruct_ptr = std::launder(reinterpret_cast<T*>(storage_));
    destruct_ptr->~T();
    return *::new (storage_) T(std::forward<Args>(args)...);
  }

  /**
   * Destructs the contained object and in-place constructs the replacement
   * from an initializer list and additional arguments.
   *
   * @param il The initializer list forwarded to the `T` constructor.
   * @param args Additional arguments forwarded to the `T` constructor.
   * @return A reference to the newly constructed contained object.
   */
  template <class U, class... Args>
  T& emplace(std::initializer_list<U> il, Args&&... args) noexcept {
    T* destruct_ptr = std::launder(reinterpret_cast<T*>(storage_));
    destruct_ptr->~T();
    return *::new (storage_) T(il, std::forward<Args>(args)...);
  }

  /**
   * `swap` just calls `swap(T&, T&)`.
   *
   * @param other The `Replaceable` to swap contents with.
   */
  void swap(Replaceable& other) noexcept(
      std::is_nothrow_swappable_v<Replaceable>) {
    using std::swap;
    swap(*(*this), *other);
  }

  /**
   * Accesses the contained object. Intended to be very unsurprising.
   *
   * @return A const pointer to the contained object.
   */
  constexpr const T* operator->() const {
    return std::launder(reinterpret_cast<T const*>(storage_));
  }

  /**
   * Accesses the contained object.
   *
   * @return A pointer to the contained object.
   */
  constexpr T* operator->() {
    return std::launder(reinterpret_cast<T*>(storage_));
  }

  /**
   * Accesses the contained object.
   *
   * @return A const lvalue reference to the contained object.
   */
  constexpr const T& operator*() const& {
    return *std::launder(reinterpret_cast<T const*>(storage_));
  }

  /**
   * Accesses the contained object.
   *
   * @return An lvalue reference to the contained object.
   */
  constexpr T& operator*() & {
    return *std::launder(reinterpret_cast<T*>(storage_));
  }

  /**
   * Accesses the contained object.
   *
   * @return An rvalue reference to the contained object.
   */
  constexpr T&& operator*() && {
    return std::move(*std::launder(reinterpret_cast<T*>(storage_)));
  }

  /**
   * Accesses the contained object.
   *
   * @return A const rvalue reference to the contained object.
   */
  constexpr const T&& operator*() const&& {
    return std::move(*std::launder(reinterpret_cast<T const*>(storage_)));
  }

 private:
  friend struct replaceable_detail::dtor_mixin<T>;
  friend struct replaceable_detail::default_and_move_ctor_mixin<T>;
  friend struct replaceable_detail::copy_ctor_mixin<T>;
  friend struct replaceable_detail::move_assignment_mixin<T>;
  friend struct replaceable_detail::copy_assignment_mixin<T>;
  aligned_storage_for_t<T> storage_[1];
};

/// Deduces `Replaceable<T>` from a value of type `T`.
template <class T>
Replaceable(T) -> Replaceable<T>;

} // namespace folly
