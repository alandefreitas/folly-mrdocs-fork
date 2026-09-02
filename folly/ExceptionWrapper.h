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

#include <cassert>
#include <cstdint>
#include <exception>
#include <iosfwd>
#include <memory>
#include <new>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <fmt/format.h>

#include <folly/CPortability.h>
#include <folly/CppAttributes.h>
#include <folly/Demangle.h>
#include <folly/ExceptionString.h>
#include <folly/FBString.h>
#include <folly/Portability.h>
#include <folly/Traits.h>
#include <folly/Utility.h>
#include <folly/functional/traits.h>
#include <folly/lang/Assume.h>
#include <folly/lang/Exception.h>

namespace folly {

#define FOLLY_REQUIRES_DEF(...) \
  std::enable_if_t<static_cast<bool>(__VA_ARGS__), long>

#define FOLLY_REQUIRES(...) FOLLY_REQUIRES_DEF(__VA_ARGS__) = __LINE__

//! Throwing exceptions can be a convenient way to handle errors. Storing
//! exceptions in an `exception_ptr` makes it easy to handle exceptions in a
//! different thread or at a later time. `exception_ptr` can also be used in a
//! very generic result/exception wrapper.
//!
//! However, inspecting exceptions through the `exception_ptr` interface, namely
//! through `rethrow_exception`, is expensive. This is a wrapper interface which
//! offers faster inspection.
//!
//! \par Example usage:
//! \code
//! exception_wrapper globalExceptionWrapper;
//!
//! // Thread1
//! void doSomethingCrazy() {
//!   int rc = doSomethingCrazyWithLameReturnCodes();
//!   if (rc == NAILED_IT) {
//!     globalExceptionWrapper = exception_wrapper();
//!   } else if (rc == FACE_PLANT) {
//!     globalExceptionWrapper = make_exception_wrapper<FacePlantException>();
//!   } else if (rc == FAIL_WHALE) {
//!     globalExceptionWrapper = make_exception_wrapper<FailWhaleException>();
//!   }
//! }
//!
//! // Thread2: Exceptions are ok!
//! void processResult() {
//!   try {
//!     globalExceptionWrapper.throw_exception();
//!   } catch (const FacePlantException& e) {
//!     LOG(ERROR) << "FACEPLANT!";
//!   } catch (const FailWhaleException& e) {
//!     LOG(ERROR) << "FAILWHALE!";
//!   }
//! }
//!
//! // Thread2: Exceptions are bad!
//! void processResult() {
//!   globalExceptionWrapper.handle(
//!       [&](FacePlantException& faceplant) {
//!         LOG(ERROR) << "FACEPLANT";
//!       },
//!       [&](FailWhaleException& failwhale) {
//!         LOG(ERROR) << "FAILWHALE!";
//!       },
//!       [](...) {
//!         LOG(FATAL) << "Unrecognized exception";
//!       });
//! }
//! \endcode
class exception_wrapper final {
 private:
  struct with_exception_from_fn_;
  struct with_exception_from_ex_;

  [[noreturn]] static void onNoExceptionError(char const* name);

  template <class Ex>
  using IsStdException = std::is_base_of<std::exception, std::decay_t<Ex>>;

  std::exception_ptr ptr_;

  template <class T>
  struct IsRegularExceptionType
      : StrictConjunction<
            std::is_copy_constructible<T>,
            std::negation<std::is_base_of<exception_wrapper, T>>,
            std::negation<std::is_abstract<T>>> {};

  template <class This, class Fn>
  static bool with_exception_(This& this_, Fn fn_, tag_t<void>);

  template <class This, class Fn, typename Ex>
  static bool with_exception_(This& this_, Fn fn_, tag_t<Ex>);

  template <class Ex, class This, class Fn>
  static bool with_exception_(This& this_, Fn fn_);

  template <class This, class... CatchFns>
  static void handle_(This& this_, char const* name, CatchFns&... fns);

 public:
  //! Default-constructs an empty `exception_wrapper`
  //! \post `type() == nullptr`
  exception_wrapper() noexcept {}

  //! Move-constructs an `exception_wrapper`
  //! \param that The wrapper to move from.
  //! \post `*this` contains the value of `that` prior to the move
  //! \post `that.type() == nullptr`
  exception_wrapper(exception_wrapper&& that) noexcept;

  //! Copy-constructs an `exception_wrapper`
  //! \param that The wrapper to copy from.
  //! \post `*this` contains a copy of `that`, and `that` is unmodified
  //! \post `type() == that.type()`
  exception_wrapper(exception_wrapper const& that) = default;

  //! Move-assigns an `exception_wrapper`
  //! \param that The wrapper to move from.
  //! \return A reference to this wrapper.
  //! \pre `this != &that`
  //! \post `*this` contains the value of `that` prior to the move
  //! \post `that.type() == nullptr`
  exception_wrapper& operator=(exception_wrapper&& that) noexcept;

  //! Copy-assigns an `exception_wrapper`
  //! \param that The wrapper to copy from.
  //! \return A reference to this wrapper.
  //! \post `*this` contains a copy of `that`, and `that` is unmodified
  //! \post `type() == that.type()`
  exception_wrapper& operator=(exception_wrapper const& that) = default;

  //! Constructs an `exception_wrapper` from an existing `std::exception_ptr`.
  //! \param ptr The exception pointer to wrap.
  //! \post `!ptr || bool(*this)`
  explicit exception_wrapper(std::exception_ptr const& ptr) noexcept;
  //! Constructs an `exception_wrapper` from an existing `std::exception_ptr`.
  //! \param ptr The exception pointer to wrap.
  explicit exception_wrapper(std::exception_ptr&& ptr) noexcept;

  //! Constructs an `exception_wrapper` from an exception object.
  //! \param ex The exception object to store.
  //! \pre `typeid(ex) == typeid(typename decay<Ex>::type)`
  //! \post `bool(*this)`
  //! \post `type() == &typeid(ex)`
  //! \note Exceptions of types derived from `std::exception` can be implicitly
  //!     converted to an `exception_wrapper`.
  template <
      class Ex,
      class Ex_ = std::decay_t<Ex>,
      FOLLY_REQUIRES(
          std::conjunction_v<IsStdException<Ex_>, IsRegularExceptionType<Ex_>>)>
  /* implicit */ exception_wrapper(Ex&& ex);

  //! Constructs an `exception_wrapper` from an in-place exception object.
  //! \param inPlace Tag selecting in-place construction.
  //! \param ex The exception object to store.
  //! \pre `typeid(ex) == typeid(typename decay<Ex>::type)`
  //! \post `bool(*this)`
  //! \post `type() == &typeid(ex)`
  //! \note Exceptions of types not derived from `std::exception` can still be
  //!     used to construct an `exception_wrapper`, but you must specify
  //!     `std::in_place` as the first parameter.
  template <
      class Ex,
      class Ex_ = std::decay_t<Ex>,
      FOLLY_REQUIRES(IsRegularExceptionType<Ex_>::value)>
  exception_wrapper(std::in_place_t inPlace, Ex&& ex);

  //! Constructs an `exception_wrapper` holding an in-place-built `Ex`.
  //! \param inPlaceType Tag selecting the in-place-built exception type.
  //! \param as The arguments forwarded to construct the exception object.
  template <
      class Ex,
      typename... As,
      FOLLY_REQUIRES(IsRegularExceptionType<Ex>::value)>
  exception_wrapper(std::in_place_type_t<Ex> inPlaceType, As&&... as);

  //! Swaps the value of `*this` with the value of `that`
  //! \param that The wrapper to swap contents with.
  void swap(exception_wrapper& that) noexcept;

  //! Reports whether this wrapper holds an exception.
  //! \return `true` if `*this` is holding an exception.
  explicit operator bool() const noexcept;

  //! Reports whether this wrapper is empty.
  //! \return `!bool(*this)`
  bool operator!() const noexcept;

  //! Make this `exception_wrapper` empty
  //! \post `!*this`
  void reset();

  //! Reports whether an exception pointer is held.
  //! \return `true` if this `exception_wrapper` holds an exception.
  bool has_exception_ptr() const noexcept;

  //! Returns a mutable pointer to the held `std::exception`, if any.
  //! \return a pointer to the `std::exception` held by `*this`, if it holds
  //!     one; otherwise, returns `nullptr`.
  std::exception* get_mutable_exception() const noexcept;
  //! Returns a const pointer to the held `std::exception`, if any.
  //! \return a pointer to the `std::exception` held by `*this`, if it holds
  //!     one; otherwise, returns `nullptr`.
  std::exception const* get_exception() const noexcept;

  //! \returns a pointer to the `Ex` held by `*this`, if it holds an object
  //!     whose type `From` permits `std::is_convertible<From*, Ex*>`;
  //!     otherwise, returns `nullptr`.
  //!
  //! `folly::get_exception<Ex>(ew)` is identical, but avoids the "dependent
  //! template" wart, and supports inspecting other types.
  //!
  //! This is most efficient when `Ex` matches the exact stored type, or when
  //! the type alias `Ex::folly_get_exception_hint_types` has a good hint.
  //! \overload
  template <typename Ex>
  Ex* get_mutable_exception() const noexcept;
  //! Returns a const pointer to the held exception as `Ex`, if convertible.
  //! \return a pointer to the `Ex` held by `*this`, or `nullptr` if none.
  template <typename Ex>
  Ex const* get_exception() const noexcept;

  //! Returns a `std::exception_ptr` referencing the held exception.
  //! \return A `std::exception_ptr` that references the exception held by
  //!     `*this`.
  std::exception_ptr to_exception_ptr() const& noexcept;
  //! Deleted rvalue overload of `to_exception_ptr`.
  // NB: Can add this back, if a good use-case arises.
  std::exception_ptr to_exception_ptr() && = delete;

  //! Returns a mutable reference to the held `std::exception_ptr`.
  //! \return A reference to the held `std::exception_ptr`.
  std::exception_ptr& exception_ptr() & noexcept;
  //! Returns a const reference to the held `std::exception_ptr`.
  //! \return A const reference to the held `std::exception_ptr`.
  std::exception_ptr const& exception_ptr() const& noexcept;
  //! Returns an rvalue reference to the held `std::exception_ptr`.
  //! \return An rvalue reference to the held `std::exception_ptr`.
  std::exception_ptr&& exception_ptr() && noexcept;
  //! Returns a const rvalue reference to the held `std::exception_ptr`.
  //! \return A const rvalue reference to the held `std::exception_ptr`.
  std::exception_ptr const&& exception_ptr() const&& noexcept;

  //! Compares two wrappers for pointing to the same exception object.
  //! \param lhs The left-hand wrapper.
  //! \param rhs The right-hand wrapper.
  //! \return `true` if the wrappers point to the same exception object
  friend inline bool operator==(
      exception_wrapper const& lhs, exception_wrapper const& rhs) noexcept {
    return lhs.ptr_ == rhs.ptr_;
  }

  //! Returns the `typeid` of the wrapped exception object. If there is no
  //!     wrapped exception object, returns `nullptr`.
  //! \return A pointer to the `type_info` of the wrapped exception, or
  //!     `nullptr` if empty.
  std::type_info const* type() const noexcept;

  //! Returns a description of the wrapped exception.
  //! \return If `get_exception() != nullptr`, `class_name() + ": " +
  //!     get_exception()->what()`; otherwise, `class_name()`.
  folly::fbstring what() const;

  //! Returns the class name of the wrapped exception.
  //! \return If `!*this`, the empty string; otherwise, if `!type()`, text that
  //!     is not a class name; otherwise, the demangling of `type()->name()`.
  folly::fbstring class_name() const;

  //! Reports whether the wrapped exception matches a given catch clause.
  //! \tparam Ex The expression type to check for compatibility with.
  //! \return `true` if and only if `*this` wraps an exception that would be
  //!     caught with a `catch(Ex const&)` clause.
  //! \note If `*this` is empty, this function returns `false`.
  template <class Ex>
  bool is_compatible_with() const noexcept;

  //! Throws the wrapped expression.
  //! \pre `bool(*this)`
  [[noreturn]] void throw_exception() const;

  //! Terminates the process with the wrapped expression.
  [[noreturn]] void terminate_with() const noexcept { throw_exception(); }

  //! Throws the wrapped expression nested into another exception.
  //! \pre `bool(*this)`
  //! \param ex Exception in *this will be thrown nested into ex;
  //      see std::throw_with_nested() for details on this semantic.
  template <class Ex>
  [[noreturn]] void throw_with_nested(Ex&& ex) const;

  //! Call `fn` with the wrapped exception (if any), if `fn` can accept it.
  //! \par Example
  //! \code
  //! exception_wrapper ew{std::runtime_error("goodbye cruel world")};
  //!
  //! assert( ew.with_exception([](std::runtime_error& e){/*...*/}) );
  //!
  //! assert( !ew.with_exception([](int& e){/*...*/}) );
  //!
  //! assert( !exception_wrapper{}.with_exception([](int& e){/*...*/}) );
  //! \endcode
  //! \tparam Ex Optionally, the type of the exception that `fn` accepts.
  //! \tparam Fn The type of a monomorphic function object.
  //! \param fn A function object to call with the wrapped exception
  //! \return `true` if and only if `fn` was called.
  //! \note Optionally, you may explicitly specify the type of the exception
  //!     that `fn` expects, as in
  //! \code
  //! ew.with_exception<std::runtime_error>([](auto&& e) { /*...*/; });
  //! \endcode
  //! \note The handler is not invoked with an active exception.
  //!     **Do not try to rethrow the exception with `throw;` from within your
  //!     handler -- that is, a throw expression with no operand.** This may
  //!     cause your process to terminate. (It is perfectly ok to throw from
  //!     a handler so long as you specify the exception to throw, as in
  //!     `throw e;`.)
  template <class Ex = void const, class Fn>
  bool with_exception(Fn fn);
  //! \overload
  template <class Ex = void const, class Fn>
  bool with_exception(Fn fn) const;

  //! Handle the wrapped expression as if with a series of `catch` clauses,
  //!     propagating the exception if no handler matches.
  //! \par Example
  //! \code
  //! exception_wrapper ew{std::runtime_error("goodbye cruel world")};
  //!
  //! ew.handle(
  //!   [&](std::logic_error const& e) {
  //!      LOG(DFATAL) << "ruh roh";
  //!      ew.throw_exception(); // rethrow the active exception without
  //!                           // slicing it. Will not be caught by other
  //!                           // handlers in this call.
  //!   },
  //!   [&](std::exception const& e) {
  //!      LOG(ERROR) << ew.what();
  //!   });
  //! \endcode
  //! In the above example, any exception _not_ derived from `std::exception`
  //!     will be propagated. To specify a catch-all clause, pass a lambda that
  //!     takes a C-style ellipses, as in:
  //! \code
  //! ew.handle(/*...* /, [](...) { /* handle unknown exception */ } )
  //! \endcode
  //! \pre `!*this`
  //! \tparam CatchFns A pack of unary monomorphic function object types.
  //! \param fns A pack of unary monomorphic function objects to be treated as
  //!     an ordered list of potential exception handlers.
  //! \note The handlers are not invoked with an active exception.
  //!     **Do not try to rethrow the exception with `throw;` from within your
  //!     handler -- that is, a throw expression with no operand.** This may
  //!     cause your process to terminate. (It is perfectly ok to throw from
  //!     a handler so long as you specify the exception to throw, as in
  //!     `throw e;`.)
  template <class... CatchFns>
  void handle(CatchFns... fns);
  //! \overload
  template <class... CatchFns>
  void handle(CatchFns... fns) const;

  //! Implements the `folly::get_exception<Ex>(ew)` protocol (const overload).
  //! \param tag Tag selecting the `folly::get_exception` protocol overload.
  //! \return a const pointer to the held `Ex`, or `nullptr` if none.
  // Implement the `folly::get_exception<Ex>(ew)` protocol
  template <typename Ex>
  Ex const* get_exception(get_exception_tag_t tag) const noexcept {
    return get_exception<Ex>();
  }
  //! Implements the `folly::get_exception<Ex>(ew)` protocol (mutable overload).
  //! \param tag Tag selecting the `folly::get_exception` protocol overload.
  //! \return a mutable pointer to the held `Ex`, or `nullptr` if none.
  template <typename Ex>
  Ex* get_mutable_exception(get_exception_tag_t tag) const noexcept {
    return get_mutable_exception<Ex>();
  }
};

/**
 * Builds an `exception_wrapper` wrapping a newly constructed exception.
 *
 * \param as The arguments forwarded to construct the `Ex` exception.
 * \return An `exception_wrapper` that wraps an instance of type `Ex`
 *     that has been constructed with arguments `std::forward<As>(as)...`.
 */
template <class Ex, typename... As>
exception_wrapper make_exception_wrapper(As&&... as) {
  return exception_wrapper{std::in_place_type<Ex>, std::forward<As>(as)...};
}

/**
 * Inserts `ew.what()` into the ostream `sout`.
 * \param sout The output stream to write to.
 * \param ew The exception wrapper to format.
 * \return `sout`
 */
template <class Ch>
std::basic_ostream<Ch>& operator<<(
    std::basic_ostream<Ch>& sout, exception_wrapper const& ew) {
  sout << ew.class_name();
  if (auto e = ew.get_exception()) {
    sout << ": " << e->what();
  }
  return sout;
}

/**
 * Swaps the value of `a` with the value of `b`.
 *
 * \param a The first wrapper.
 * \param b The second wrapper.
 */
inline void swap(exception_wrapper& a, exception_wrapper& b) noexcept {
  a.swap(b);
}

//! Returns a string describing the exception held by `ew`.
//! \param ew The exception wrapper to describe.
//! \return A string describing the wrapped exception.
// For consistency with exceptionStr() functions in ExceptionString.h
fbstring exceptionStr(exception_wrapper const& ew);

//! `try_and_catch` is a convenience for `try {} catch(...) {}`` that returns an
//! `exception_wrapper` with the thrown exception, if any.
//! \param fn The function to invoke and guard against exceptions.
//! \return An `exception_wrapper` holding the thrown exception, if any.
template <typename F>
exception_wrapper try_and_catch(F&& fn) noexcept {
  auto x = [&] { return void(static_cast<F&&>(fn)()), std::exception_ptr{}; };
  return exception_wrapper{catch_exception(x, current_exception)};
}

/**
 * A convenience shorthand for `exception_wrapper(current_exception())`.
 *
 * \return An `exception_wrapper` holding the currently handled exception.
 */
inline exception_wrapper current_exception_wrapper() noexcept {
  return exception_wrapper{current_exception()};
}

} // namespace folly

/// Formatter specialization that formats an `exception_wrapper` via `what()`.
template <>
struct fmt::formatter<folly::exception_wrapper> : formatter<folly::fbstring> {
  /// Formats the exception wrapper into the output context.
  ///
  /// \param ew The exception wrapper to format.
  /// \param ctx The formatting context to write to.
  /// \returns An iterator past the end of the written output.
  template <typename Context>
  auto format(const folly::exception_wrapper& ew, Context& ctx) const {
    return formatter<folly::fbstring>::format(ew.what(), ctx);
  }
};

#include <folly/ExceptionWrapper-inl.h>

#undef FOLLY_REQUIRES
#undef FOLLY_REQUIRES_DEF
