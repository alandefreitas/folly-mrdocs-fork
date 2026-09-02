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

#include <exception>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <folly/CppAttributes.h>
#include <folly/ExceptionWrapper.h>
#include <folly/Likely.h>
#include <folly/Memory.h>
#include <folly/Portability.h>
#include <folly/Unit.h>
#include <folly/Utility.h>
#include <folly/functional/Invoke.h>
#include <folly/lang/Exception.h>

namespace folly {

/// Exception type thrown by `Try` when it is used incorrectly.
class FOLLY_EXPORT TryException : public std::logic_error {
 public:
  /// Inherit the message-taking constructors of `std::logic_error`.
  using std::logic_error::logic_error;

  /// Construct with an empty message.
  TryException() : std::logic_error{""} {}
};

/// Exception thrown when an uninitialized `Try` is accessed.
class FOLLY_EXPORT UsingUninitializedTry : public TryException {
 public:
  /// Construct the exception.
  UsingUninitializedTry() = default;

  /// Return the exception message.
  ///
  /// \returns A pointer to the null-terminated message string.
  char const* what() const noexcept override {
    return "Using uninitialized try";
  }
};

template <class T>
class Try;

namespace detail {
template <class T>
class TryBase {
 protected:
  enum class Contains {
    VALUE,
    EXCEPTION,
    NOTHING,
  };

 public:
  /*
   * Construct an empty Try
   */
  TryBase() noexcept : contains_(Contains::NOTHING) {}

  /*
   * Construct a Try with a value by copy
   *
   * @param v The value to copy in
   */
  explicit TryBase(const T& v) noexcept(
      std::is_nothrow_copy_constructible<T>::value)
      : contains_(Contains::VALUE), value_(v) {}

  /*
   * Construct a Try with a value by move
   *
   * @param v The value to move in
   */
  explicit TryBase(T&& v) noexcept(std::is_nothrow_move_constructible<T>::value)
      : contains_(Contains::VALUE), value_(std::move(v)) {}

  template <typename... Args>
  explicit TryBase(std::in_place_t, Args&&... args) noexcept(
      std::is_nothrow_constructible<T, Args&&...>::value)
      : contains_(Contains::VALUE), value_(static_cast<Args&&>(args)...) {}

  /// Implicit conversion from Try<void> to Try<Unit>
  template <class T2 = T>
  /* implicit */ TryBase(
      typename std::enable_if<std::is_same<Unit, T2>::value, Try<void> const&>::
          type t) noexcept;

  /*
   * Construct a Try with an exception_wrapper
   *
   * @param e The exception_wrapper
   */
  explicit TryBase(exception_wrapper e) noexcept
      : contains_(Contains::EXCEPTION), e_(std::move(e)) {}

  ~TryBase();

  // Move constructor
  TryBase(TryBase&& t) noexcept(std::is_nothrow_move_constructible<T>::value);
  // Move assigner
  TryBase& operator=(TryBase&& t) noexcept(
      std::is_nothrow_move_constructible<T>::value);

  // Copy constructor
  TryBase(const TryBase& t) noexcept(
      std::is_nothrow_copy_constructible<T>::value);
  // Copy assigner
  TryBase& operator=(const TryBase& t) noexcept(
      std::is_nothrow_copy_constructible<T>::value);

 protected:
  void destroy() noexcept;

  Contains contains_;
  union {
    T value_;
    exception_wrapper e_;
  };
};
} // namespace detail

/// A wrapper that contains either an instance of `T`, an exception, or nothing.
///
/// Exceptions are stored as `exception_wrapper`s so that the user can minimize
/// rethrows if so desired. To represent success or a captured exception, use
/// `Try<Unit>`.
template <class T>
class Try
    : detail::TryBase<T>,
      moveonly_::EnableCopyMove<
          std::is_copy_constructible<T>::value,
          std::is_move_constructible<T>::value> {
  static_assert(
      !std::is_reference<T>::value, "Try may not be used with reference types");
  using typename detail::TryBase<T>::Contains;

 public:
  using detail::TryBase<T>::TryBase;

  /// The value type for the `Try`.
  using element_type = T;

  /// In-place construct the value in the `Try` object.
  ///
  /// Destroys any previous value prior to constructing the new value.
  /// Leaves `*this` in an empty state if the construction of `T` throws.
  ///
  /// \param args The arguments forwarded to `T`'s constructor.
  /// \returns A reference to the newly constructed value.
  template <typename... Args>
  T& emplace(Args&&... args) noexcept(
      std::is_nothrow_constructible<T, Args&&...>::value);

  /// In-place construct an exception in the `Try` object.
  ///
  /// Destroys any previous value prior to constructing the new value.
  /// Leaves `*this` in an empty state if the construction of the
  /// `exception_wrapper` throws.
  ///
  /// Any arguments passed to `emplaceException()` are forwarded on to the
  /// `exception_wrapper` constructor.
  ///
  /// \param args The arguments forwarded to the `exception_wrapper` constructor.
  /// \returns A reference to the newly constructed `exception_wrapper`.
  template <typename... Args>
  exception_wrapper& emplaceException(Args&&... args) noexcept(
      std::is_nothrow_constructible<exception_wrapper, Args&&...>::value);

  /// Get a mutable reference to the contained value.
  ///
  /// Rethrows if the `Try` contains an exception or is empty.
  ///
  /// \returns A mutable reference to the contained value.
  T& value() & [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]];

  /// Get an rvalue reference to the contained value.
  ///
  /// Rethrows if the `Try` contains an exception or is empty.
  ///
  /// \returns An rvalue reference to the contained value.
  T&& value() &&;

  /// Get a const reference to the contained value.
  ///
  /// Rethrows if the `Try` contains an exception or is empty.
  ///
  /// \returns A const reference to the contained value.
  const T& value() const& [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]];

  /// Get a const rvalue reference to the contained value.
  ///
  /// Rethrows if the `Try` contains an exception or is empty.
  ///
  /// \returns A const rvalue reference to the contained value.
  const T&& value() const&&;

  /// Return the contained value, or a fallback if there is none.
  ///
  /// Returns a copy of the contained value if `*this` has a value, otherwise
  /// returns a value constructed from `defaultValue`. The selected constructor
  /// of the return value may throw exceptions.
  ///
  /// \param defaultValue The value used to build the result when empty.
  /// \returns The contained value or a value built from `defaultValue`.
  template <class U>
  T value_or(U&& defaultValue) const&;

  /// Return the contained value, or a fallback if there is none.
  ///
  /// Returns the contained value if `*this` has a value, otherwise returns a
  /// value constructed from `defaultValue`. The selected constructor of the
  /// return value may throw exceptions.
  ///
  /// \param defaultValue The value used to build the result when empty.
  /// \returns The contained value or a value built from `defaultValue`.
  template <class U>
  T value_or(U&& defaultValue) &&;

  /// Rethrow if the `Try` contains an exception or is empty.
  ///
  /// Otherwise do nothing.
  void throwUnlessValue() const;

  /// Rethrow if the `Try` contains an exception or is empty.
  ///
  /// Replaced by `throwUnlessValue`.
  [[deprecated("Replaced by throwUnlessValue")]] void throwIfFailed() const;

  /// Const dereference operator.
  ///
  /// Rethrows if the `Try` contains an exception or is empty.
  ///
  /// \returns A const reference to the contained value.
  const T& operator*() const& [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return value();
  }

  /// Dereference operator.
  ///
  /// If the `Try` contains an exception it will be rethrown.
  ///
  /// \returns A mutable reference to the contained value.
  T& operator*() & [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] { return value(); }

  /// Mutable rvalue dereference operator.
  ///
  /// If the `Try` contains an exception it will be rethrown.
  ///
  /// \returns An rvalue reference to the contained value.
  T&& operator*() && { return std::move(value()); }

  /// Const rvalue dereference operator.
  ///
  /// If the `Try` contains an exception it will be rethrown.
  ///
  /// \returns A const rvalue reference to the contained value.
  const T&& operator*() const&& { return std::move(value()); }

  /// Const arrow operator.
  ///
  /// Rethrows if the `Try` contains an exception or is empty.
  ///
  /// \returns A const pointer to the contained value.
  const T* operator->() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return &value();
  }

  /// Arrow operator.
  ///
  /// If the `Try` contains an exception it will be rethrown.
  ///
  /// \returns A mutable pointer to the contained value.
  T* operator->() [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] { return &value(); }

  /// Check whether the `Try` contains a value.
  ///
  /// \returns `true` if the `Try` contains a value, `false` otherwise.
  bool hasValue() const noexcept { return this->contains_ == Contains::VALUE; }

  /// Check whether the `Try` contains an exception.
  ///
  /// \returns `true` if the `Try` contains an exception, `false` otherwise.
  bool hasException() const noexcept {
    return this->contains_ == Contains::EXCEPTION;
  }

  /// Check whether the `Try` contains an exception of type `Ex`.
  ///
  /// \returns `true` if the `Try` contains an exception of type `Ex`, `false`
  ///          otherwise.
  template <class Ex>
  bool hasException() const noexcept {
    return hasException() && this->e_.template is_compatible_with<Ex>();
  }

  /// Get a mutable reference to the contained exception.
  ///
  /// Throws `TryException` if the `Try` does not contain an exception.
  ///
  /// \returns A mutable reference to the contained `exception_wrapper`.
  exception_wrapper& exception() & {
    if (!hasException()) {
      throw_exception<TryException>("Try does not contain an exception");
    }
    return this->e_;
  }

  /// Get an rvalue reference to the contained exception.
  ///
  /// Throws `TryException` if the `Try` does not contain an exception.
  ///
  /// \returns An rvalue reference to the contained `exception_wrapper`.
  exception_wrapper&& exception() && {
    if (!hasException()) {
      throw_exception<TryException>("Try does not contain an exception");
    }
    return std::move(this->e_);
  }

  /// Get a const reference to the contained exception.
  ///
  /// Throws `TryException` if the `Try` does not contain an exception.
  ///
  /// \returns A const reference to the contained `exception_wrapper`.
  const exception_wrapper& exception() const& {
    if (!hasException()) {
      throw_exception<TryException>("Try does not contain an exception");
    }
    return this->e_;
  }

  /// Get a const rvalue reference to the contained exception.
  ///
  /// Throws `TryException` if the `Try` does not contain an exception.
  ///
  /// \returns A const rvalue reference to the contained `exception_wrapper`.
  const exception_wrapper&& exception() const&& {
    if (!hasException()) {
      throw_exception<TryException>("Try does not contain an exception");
    }
    return std::move(this->e_);
  }

  /// Get a pointer to the held `std::exception`, if any.
  ///
  /// \returns A pointer to the `std::exception` held by `*this`, if one is
  ///          held; otherwise, returns `nullptr`.
  std::exception const* tryGetExceptionObject() const noexcept {
    return hasException() ? this->e_.get_exception() : nullptr;
  }

  /// Get a pointer to the held exception as type `Ex`, if compatible.
  ///
  /// \returns A pointer to the `Ex` held by `*this`, if it holds an object
  ///          whose type `From` permits `std::is_convertible<From*, Ex*>`;
  ///          otherwise, returns `nullptr`.
  template <class Ex>
  Ex const* tryGetExceptionObject() const noexcept {
    return hasException() ? this->e_.template get_exception<Ex>() : nullptr;
  }

  /// If the `Try` contains an exception of type `Ex`, execute `func(Ex)`.
  ///
  /// \param func A function that takes a single parameter of type `const Ex&`.
  /// \returns `true` if the `Try` held an `Ex` and `func` was executed, `false`
  ///          otherwise.
  template <class Ex, class F>
  bool withException(F func) {
    if (!hasException()) {
      return false;
    }
    return this->e_.template with_exception<Ex>(std::move(func));
  }

  /// If the `Try` contains an exception of type `Ex`, execute `func(Ex)`.
  ///
  /// \param func A function that takes a single parameter of type `const Ex&`.
  /// \returns `true` if the `Try` held an `Ex` and `func` was executed, `false`
  ///          otherwise.
  template <class Ex, class F>
  bool withException(F func) const {
    if (!hasException()) {
      return false;
    }
    return this->e_.template with_exception<Ex>(std::move(func));
  }

  /// If the `Try` contains a compatible exception, execute `func(Ex)`.
  ///
  /// The exception type `Ex` is deduced from the first parameter of `func`.
  ///
  /// \param func A function that takes a single parameter of type `const Ex&`.
  /// \returns `true` if the `Try` held an `Ex` and `func` was executed, `false`
  ///          otherwise.
  template <class F>
  bool withException(F func) {
    if (!hasException()) {
      return false;
    }
    return this->e_.with_exception(std::move(func));
  }

  /// If the `Try` contains a compatible exception, execute `func(Ex)`.
  ///
  /// The exception type `Ex` is deduced from the first parameter of `func`.
  ///
  /// \param func A function that takes a single parameter of type `const Ex&`.
  /// \returns `true` if the `Try` held an `Ex` and `func` was executed, `false`
  ///          otherwise.
  template <class F>
  bool withException(F func) const {
    if (!hasException()) {
      return false;
    }
    return this->e_.with_exception(std::move(func));
  }

  /// Get the wrapped `Try` itself when `isTry` is `true`.
  ///
  /// \returns The `Try` forwarded as an `R`.
  template <bool isTry, typename R>
  typename std::enable_if<isTry, R>::type get() {
    return std::forward<R>(*this);
  }

  /// Get the contained value when `isTry` is `false`.
  ///
  /// \returns The contained value forwarded as an `R`.
  template <bool isTry, typename R>
  typename std::enable_if<!isTry, R>::type get() {
    return std::forward<R>(value());
  }
};

/// Specialization of `Try` for the `void` value type.
///
/// Encapsulates either success or an exception.
template <>
class Try<void> {
 public:
  /*
   * The value type for the Try
   */
  using element_type = void;

  // Construct a Try holding a successful and void result
  Try() noexcept : hasValue_(true) {}

  /*
   * Construct a Try with an exception_wrapper
   *
   * @param e The exception_wrapper
   */
  explicit Try(exception_wrapper e) noexcept
      : hasValue_(false), e_(std::move(e)) {}

  /// Implicit conversion from `Try<Unit>` to `Try<void>`.
  ///
  /// \param t The `Try<Unit>` to convert from.
  /* implicit */ inline Try(const Try<Unit>& t) noexcept;

  // Copy assigner
  inline Try& operator=(const Try<void>& t) noexcept;

  // Copy constructor
  Try(const Try<void>& t) noexcept : hasValue_(t.hasValue_) {
    if (t.hasException()) {
      new (&e_) exception_wrapper(t.e_);
    }
  }

  ~Try() {
    if (hasException()) {
      e_.~exception_wrapper();
    }
  }

  /*
   * In-place construct a 'void' value into this Try object.
   *
   * This has the effect of clearing any existing exception stored in the
   * Try object.
   */
  void emplace() noexcept {
    if (hasException()) {
      e_.~exception_wrapper();
      hasValue_ = true;
    }
  }

  /*
   * In-place construct an exception in the Try object.
   *
   * Destroys any previous value prior to constructing the new value.
   * Leaves *this in an empty state if the construction of the exception_wrapper
   * throws.
   *
   * Any arguments passed to emplaceException() are forwarded on to the
   * exception_wrapper constructor.
   *
   * @returns reference to the newly constructed exception_wrapper.
   */
  template <typename... Args>
  exception_wrapper& emplaceException(Args&&... args) noexcept(
      std::is_nothrow_constructible<exception_wrapper, Args&&...>::value);

  // If the Try contains an exception, throws it
  void value() const { throwIfFailed(); }
  // Dereference operator. If the Try contains an exception, throws it
  void operator*() const { return value(); }

  // If the Try contains an exception, throws it
  inline void throwIfFailed() const;
  inline void throwUnlessValue() const;

  // @returns False if the Try contains an exception, true otherwise
  bool hasValue() const noexcept { return hasValue_; }
  // @returns True if the Try contains an exception, false otherwise
  bool hasException() const noexcept { return !hasValue_; }

  // @returns True if the Try contains an exception of type Ex, false otherwise
  template <class Ex>
  bool hasException() const noexcept {
    return hasException() && e_.is_compatible_with<Ex>();
  }

  /*
   * @throws TryException if the Try doesn't contain an exception
   *
   * @returns mutable reference to the exception contained by this Try
   */
  exception_wrapper& exception() & {
    if (!hasException()) {
      throw_exception<TryException>("Try does not contain an exception");
    }
    return e_;
  }

  exception_wrapper&& exception() && {
    if (!hasException()) {
      throw_exception<TryException>("Try does not contain an exception");
    }
    return std::move(e_);
  }

  const exception_wrapper& exception() const& {
    if (!hasException()) {
      throw_exception<TryException>("Try does not contain an exception");
    }
    return e_;
  }

  const exception_wrapper&& exception() const&& {
    if (!hasException()) {
      throw_exception<TryException>("Try does not contain an exception");
    }
    return std::move(e_);
  }

  /*
   * @returns a pointer to the `std::exception` held by `*this`, if one is held;
   *          otherwise, returns `nullptr`.
   */
  std::exception const* tryGetExceptionObject() const noexcept {
    return hasException() ? e_.get_exception() : nullptr;
  }

  /*
   * @returns a pointer to the `Ex` held by `*this`, if it holds an object whose
   *          type `From` permits `std::is_convertible<From*, Ex*>`; otherwise,
   *          returns `nullptr`.
   */
  template <class E>
  E const* tryGetExceptionObject() const noexcept {
    return hasException() ? e_.get_exception<E>() : nullptr;
  }

  /*
   * If the Try contains an exception and it is of type Ex, execute func(Ex)
   *
   * @param func a function that takes a single parameter of type const Ex&
   *
   * @returns True if the Try held an Ex and func was executed, false otherwise
   */
  template <class Ex, class F>
  bool withException(F func) {
    if (!hasException()) {
      return false;
    }
    return e_.with_exception<Ex>(std::move(func));
  }
  template <class Ex, class F>
  bool withException(F func) const {
    if (!hasException()) {
      return false;
    }
    return e_.with_exception<Ex>(std::move(func));
  }

  /*
   * If the Try contains an exception and it is of type compatible with Ex as
   * deduced from the first parameter of func, execute func(Ex)
   *
   * @param func a function that takes a single parameter of type const Ex&
   *
   * @returns True if the Try held an Ex and func was executed, false otherwise
   */
  template <class F>
  bool withException(F func) {
    if (!hasException()) {
      return false;
    }
    return e_.with_exception(std::move(func));
  }
  template <class F>
  bool withException(F func) const {
    if (!hasException()) {
      return false;
    }
    return e_.with_exception(std::move(func));
  }

  template <bool, typename R>
  R get() {
    return std::forward<R>(*this);
  }

 private:
  bool hasValue_;
  union {
    exception_wrapper e_;
  };
};

/// Trait that detects whether a type is a `Try`.
template <typename T>
struct isTry : std::false_type {};

/// Trait specialization that matches `Try<T>`.
template <typename T>
struct isTry<Try<T>> : std::true_type {};

/// Execute a function and capture its result as a `Try`.
///
/// \param f A function to execute and capture the result of (value or
///          exception).
/// \returns A `Try` holding the result of `f`.
template <typename F>
typename std::enable_if<
    !std::is_same<invoke_result_t<F>, void>::value,
    Try<invoke_result_t<F>>>::type
makeTryWithNoUnwrap(F&& f) noexcept;

/// Specialization of `makeTryWithNoUnwrap` for functions returning `void`.
///
/// \param f A function to execute and capture the result of.
/// \returns A `Try<void>` holding the result of `f`.
template <typename F>
typename std::
    enable_if<std::is_same<invoke_result_t<F>, void>::value, Try<void>>::type
    makeTryWithNoUnwrap(F&& f) noexcept;

/// Execute a function and capture its result as a `Try`.
///
/// \param f A function to execute and capture the result of (value or
///          exception).
/// \returns A `Try` holding the result of `f`.
template <typename F>
typename std::
    enable_if<!isTry<invoke_result_t<F>>::value, Try<invoke_result_t<F>>>::type
    makeTryWith(F&& f) noexcept;

/// Specialization of `makeTryWith` for functions that return `Try<T>`.
///
/// Causes `makeTryWith` to not double-wrap the `Try`.
///
/// \param f A function to execute and capture the result of.
/// \returns The result of `f` if `f` did not throw; otherwise a `Try<T>`
///          containing the exception.
template <typename F>
typename std::enable_if<isTry<invoke_result_t<F>>::value, invoke_result_t<F>>::
    type
    makeTryWith(F&& f) noexcept;

/// Try to in-place construct a new value from the specified arguments.
///
/// If `T`'s constructor throws an exception then this is caught and the
/// `Try<T>` object is initialised to hold that exception.
///
/// \param t The `Try<T>` to construct the value into.
/// \param args The arguments passed to `T`'s constructor.
/// \returns A pointer to the newly constructed value, or `nullptr` on failure.
template <typename T, typename... Args>
T* tryEmplace(Try<T>& t, Args&&... args) noexcept;

/// Overload of `tryEmplace()` for `Try<void>`.
///
/// \param t The `Try<void>` to construct the value into.
inline void tryEmplace(Try<void>& t) noexcept;

/// Try to in-place construct a new value from the result of a function.
///
/// If the function completes successfully then attempts to in-place construct
/// a value of type `T`, passing the result of the function as the only
/// parameter.
///
/// If either the call to the function completes with an exception or the
/// constructor completes with an exception then the exception is caught and
/// stored in the `Try` object.
///
/// \param t The `Try<T>` to construct the value into.
/// \param func The function whose result is used to construct the value.
/// \returns A pointer to the newly constructed object if it completed
///          successfully, otherwise `nullptr` if the operation completed with
///          an exception.
template <typename T, typename Func>
T* tryEmplaceWith(Try<T>& t, Func&& func) noexcept;

/// Specialization of `tryEmplaceWith()` for `Try<void>`.
///
/// Calls `func()` and if it doesn't throw an exception then calls
/// `t.emplace()`. If `func()` throws then captures the exception in `t` using
/// `t.emplaceException()`. `Func` must be callable with zero parameters and
/// must return `void`.
///
/// \param t The `Try<void>` to construct the value into.
/// \param func The function to invoke.
/// \returns `true` if `func()` completed without an exception, `false` if
///          `func()` threw an exception.
template <typename Func>
bool tryEmplaceWith(Try<void>& t, Func&& func) noexcept;

/// Unwrap a tuple of `Try` values into a tuple of values.
///
/// Unwraps a tuple-like type containing a sequence of `Try<Type>` instances
/// (`Tuple<Try<Type>...>`) to `std::tuple<Type...>`.
///
/// \param tuple The tuple-like value of `Try<Type>` instances to unwrap.
/// \returns A `std::tuple<Type...>` holding the unwrapped values.
template <typename Tuple>
auto unwrapTryTuple(Tuple&& tuple);

/// Try to move the value/exception from another `Try` object.
///
/// If `T`'s constructor throws an exception then this is caught and the
/// `Try<T>` object is initialised to hold that exception.
///
/// \param t The `Try<T>` to assign into.
/// \param other The `Try<T>` to move the value or exception from.
template <typename T>
void tryAssign(Try<T>& t, Try<T>&& other) noexcept;

/// Deduction guide that deduces the value type of a `Try` from a value.
///
/// \param value The value used to deduce the `Try`'s element type.
template <typename T>
Try(T&& value) -> Try<std::decay_t<T>>;

} // namespace folly

#include <folly/Try-inl.h>
