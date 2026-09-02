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

#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#include <folly/CPortability.h>
#include <folly/Portability.h>
#include <folly/Traits.h>

namespace folly {

/*
 * FOLLY_DECLVAL(T)
 *
 * This macro works like std::declval<T>() but does the same thing in a way
 * that does not require instantiating a function template.
 *
 * Use this macro instead of std::declval<T>() in places that are widely
 * instantiated to reduce compile-time overhead of instantiating function
 * templates.
 *
 * Note that, like std::declval<T>(), this macro can only be used in
 * unevaluated contexts.
 *
 * There are some small differences between this macro and std::declval<T>().
 * - This macro results in a value of type 'T' instead of 'T&&'.
 * - This macro requires the type T to be a complete type at the
 *   point of use.
 *   If this is a problem then use FOLLY_DECLVAL(T&&) instead, or if T might
 *   be 'void', then use FOLLY_DECLVAL(std::add_rvalue_reference_t<T>).
 */
#define FOLLY_DECLVAL(...) static_cast<__VA_ARGS__ (*)() noexcept>(nullptr)()

namespace detail {
template <typename T>
T decay_1_(T const volatile&&);
template <typename T>
T decay_1_(T const&);
template <typename T>
T* decay_1_(T*);

template <typename T>
auto decay_0_(int) -> decltype(detail::decay_1_(FOLLY_DECLVAL(T&&)));
template <typename T>
auto decay_0_(short) -> void;

template <typename T>
using decay_t = decltype(detail::decay_0_<T>(0));
} // namespace detail

/// Like std::decay_t but possibly faster to compile.
///
/// mimic: std::decay_t, C++14
using detail::decay_t;

/**
 *  copy
 *
 *  Usable when you have a function with two overloads:
 *
 *      class MyData;
 *      void something(MyData&&);
 *      void something(const MyData&);
 *
 *  Where the purpose is to make copies and moves explicit without having to
 *  spell out the full type names - in this case, for copies, to invoke copy
 *  constructors.
 *
 *  When the caller wants to pass a copy of an lvalue, the caller may:
 *
 *      void foo() {
 *        MyData data;
 *        something(folly::copy(data)); // explicit copy
 *        something(std::move(data)); // explicit move
 *        something(data); // const& - neither move nor copy
 *      }
 *
 *  Note: If passed an rvalue, invokes the move-ctor, not the copy-ctor. This
 *  can be used to to force a move, where just using std::move would not:
 *
 *      folly::copy(std::move(data)); // force-move, not just a cast to &&
 *
 *  Note: The following text appears in the standard:
 *
 *      In several places in this Clause the operation //DECAY_COPY(x)// is
 *      used. All such uses mean call the function `decay_copy(x)` and use the
 *      result, where `decay_copy` is defined as follows:
 *
 *        template <class T> decay_t<T> decay_copy(T&& v)
 *          { return std::forward<T>(v); }
 *
 *      http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n4296.pdf
 *        30.2.6 `decay_copy` [thread.decaycopy].
 *
 *  We mimic it, with a `noexcept` specifier for good measure.
 *
 *  \param value The value to copy or move.
 *  \returns A decayed copy of the argument.
 */

template <typename T>
constexpr detail::decay_t<T> copy(T&& value) noexcept(
    noexcept(detail::decay_t<T>(static_cast<T&&>(value)))) {
  return static_cast<T&&>(value);
}

/// Forwards a value with the value category of another type.
///
/// mimic: `std::forward_like`, C++23 / p2445r0.
///
/// \param dst The value to forward.
/// \returns The forwarded reference with the value category of Src.
template <typename Src, typename Dst>
constexpr like_t<Src, Dst>&& forward_like(Dst&& dst) noexcept {
  return std::forward<like_t<Src, Dst>>(static_cast<Dst&&>(dst));
}

/**
 * Initializer lists are a powerful compile time syntax introduced in C++11
 * but due to their often conflicting syntax they are not used by APIs for
 * construction.
 *
 * Further standard conforming compilers *strongly* favor an
 * std::initializer_list overload for construction if one exists.  The
 * following is a simple tag used to disambiguate construction with
 * initializer lists and regular uniform initialization.
 *
 * For example consider the following case
 *
 *  class Something {
 *  public:
 *    explicit Something(int);
 *    Something(std::initializer_list<int>);
 *
 *    operator int();
 *  };
 *
 *  ...
 *  Something something{1}; // SURPRISE!!
 *
 * The last call to instantiate the Something object will go to the
 * initializer_list overload.  Which may be surprising to users.
 *
 * If however this tag was used to disambiguate such construction it would be
 * easy for users to see which construction overload their code was referring
 * to.  For example
 *
 *  class Something {
 *  public:
 *    explicit Something(int);
 *    Something(folly::initlist_construct_t, std::initializer_list<int>);
 *
 *    operator int();
 *  };
 *
 *  ...
 *  Something something_one{1}; // not the initializer_list overload
 *  Something something_two{folly::initlist_construct, {1}}; // correct
 */
struct initlist_construct_t {};
/// Tag value used to disambiguate initializer-list construction.
constexpr initlist_construct_t initlist_construct{};

//  sorted_unique_t, sorted_unique
//
//  A generic tag type and value to indicate that some constructor or method
//  accepts a container in which the values are sorted and unique.
//
//  Example:
//
//    void takes_numbers(folly::sorted_unique_t, std::vector<int> alist) {
//      assert(std::is_sorted(alist.begin(), alist.end()));
//      assert(std::unique(alist.begin(), alist.end()) == alist.end());
//      for (i : alist) {
//        // some behavior which safe only when alist is sorted and unique
//      }
//    }
//    void takes_numbers(std::vector<int> alist) {
//      std::sort(alist.begin(), alist.end());
//      alist.erase(std::unique(alist.begin(), alist.end()), alist.end());
//      takes_numbers(folly::sorted_unique, alist);
//    }
//
//  mimic: std::sorted_unique_t, std::sorted_unique, p0429r6
/// Tag type indicating a container is sorted and unique.
struct sorted_unique_t {};
/// Tag value indicating a container is sorted and unique.
constexpr sorted_unique_t sorted_unique{};

//  sorted_equivalent_t, sorted_equivalent
//
//  A generic tag type and value to indicate that some constructor or method
//  accepts a container in which the values are sorted but not necessarily
//  unique.
//
//  Example:
//
//    void takes_numbers(folly::sorted_equivalent_t, std::vector<int> alist) {
//      assert(std::is_sorted(alist.begin(), alist.end()));
//      for (i : alist) {
//        // some behavior which safe only when alist is sorted
//      }
//    }
//    void takes_numbers(std::vector<int> alist) {
//      std::sort(alist.begin(), alist.end());
//      takes_numbers(folly::sorted_equivalent, alist);
//    }
//
//  mimic: std::sorted_equivalent_t, std::sorted_equivalent, p0429r6
/// Tag type indicating a container is sorted but not necessarily unique.
struct sorted_equivalent_t {};
/// Tag value indicating a container is sorted but not necessarily unique.
constexpr sorted_equivalent_t sorted_equivalent{};

/// Adapter that marks a type as transparent for heterogeneous lookup.
template <typename T>
struct transparent : T {
  /// Marker type enabling heterogeneous lookup.
  using is_transparent = void;
  using T::T;
};

/**
 * A simple function object that passes its argument through unchanged.
 *
 * Example:
 *
 *   int i = 42;
 *   int &j = identity(i);
 *   assert(&i == &j);
 *
 * Warning: passing a prvalue through identity turns it into an xvalue,
 * which can effect whether lifetime extension occurs or not. For instance:
 *
 *   auto&& x = std::make_unique<int>(42);
 *   cout << *x ; // OK, x refers to a valid unique_ptr.
 *
 *   auto&& y = identity(std::make_unique<int>(42));
 *   cout << *y ; // ERROR: y did not lifetime-extend the unique_ptr. It
 *                // is no longer valid
 */
struct identity_fn {
  /// Returns the argument unchanged.
  /// \param x The value to pass through.
  /// \returns The argument, forwarded unchanged.
  template <class T>
  constexpr T&& operator()(T&& x) const noexcept {
    return static_cast<T&&>(x);
  }
};
/// Type of the identity function object.
using Identity = identity_fn;
/// The identity function object.
inline constexpr identity_fn identity{};

#if FOLLY_CPLUSPLUS >= 202002 && !defined(__NVCC__)

/// literal_c_str
///
/// This can only wrap literal strings, since the constructor is marked
/// consteval.  Like with fmt::format_string.
struct literal_c_str {
  const char* const ptr; ///< Pointer to the wrapped literal string.
  /// Wraps a literal string; the constructor is consteval.
  /// \param p Pointer to the literal string to wrap.
  /* implicit */ consteval literal_c_str(const char* p) : ptr(p) {}
};

#endif

#if FOLLY_CPLUSPLUS >= 202002 && !defined(__NVCC__)

/// literal_string
///
/// A structural type representing a literal string. A structural type may be
/// a non-type template argument.
///
/// May at times be useful since language-level literal strings are not allowed
/// as non-type template arguments.
///
/// This may typically be used with vtag for passing the literal string as a
/// constant-expression via a non-type template argument.
///
/// Example:
///
///   template <size_t N, literal_string<char, N> Str>
///   void do_something_with_literal_string(vtag_t<Str>);
///
///   void do_something() {
///     do_something_with_literal_string(vtag<literal_string{"foobar"}>);
///   }
template <typename C, std::size_t N>
struct literal_string {
  C buffer[N] = {}; ///< Storage holding the literal's characters.

  /// Constructs the structural string from a character array literal.
  /// \param buf The character array to copy into the buffer.
  consteval /* implicit */ literal_string(C const (&buf)[N]) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
      buffer[i] = buf[i];
    }
  }

  /// Returns the length of the string, excluding the null terminator.
  /// \returns The number of characters in the string.
  constexpr std::size_t size() const noexcept { return N - 1; }
  /// Returns a pointer to the underlying characters.
  /// \returns A pointer to the first character.
  constexpr C const* data() const noexcept { return buffer; }
  /// Returns a null-terminated pointer to the characters.
  /// \returns A pointer to the null-terminated string.
  constexpr C const* c_str() const noexcept { return buffer; }

  /// Converts the literal to a string-like type.
  /// \returns A String constructed from the literal's characters.
  template <
      typename String,
      decltype((void(String(FOLLY_DECLVAL(C const*), N - 1)), 0)) = 0>
  constexpr explicit operator String() const //
      noexcept(noexcept(String(FOLLY_DECLVAL(C const*), N - 1))) {
    return String(data(), N - 1);
  }
};

inline namespace literals {
inline namespace string_literals {

/// User-defined literal yielding the literal string value.
/// \returns The literal_string value of the template argument.
template <literal_string Str>
consteval decltype(Str) operator""_lit() noexcept {
  return Str;
}
/// User-defined literal yielding a value tag of the literal string.
/// \returns A value tag wrapping the literal string.
template <literal_string Str>
consteval vtag_t<Str> operator""_litv() noexcept {
  return vtag<Str>;
}

} // namespace string_literals
} // namespace literals

#endif

namespace detail {

template <typename T>
struct inheritable_inherit_ : T {
  using T::T;
  template <
      typename... A,
      std::enable_if_t<std::is_constructible<T, A...>::value, int> = 0>
  /* implicit */ FOLLY_ERASE inheritable_inherit_(A&&... a) noexcept(
      noexcept(T(static_cast<A&&>(a)...)))
      : T(static_cast<A&&>(a)...) {}
};

template <typename T>
struct inheritable_contain_ {
  T v;
  template <
      typename... A,
      std::enable_if_t<std::is_constructible<T, A...>::value, int> = 0>
  /* implicit */ FOLLY_ERASE inheritable_contain_(A&&... a) noexcept(
      noexcept(T(static_cast<A&&>(a)...)))
      : v(static_cast<A&&>(a)...) {}
  FOLLY_ERASE operator T&() & noexcept { return v; }
  FOLLY_ERASE operator T&&() && noexcept { return static_cast<T&&>(v); }
  FOLLY_ERASE operator T const&() const& noexcept { return v; }
  FOLLY_ERASE operator T const&&() const&& noexcept {
    return static_cast<T const&&>(v);
  }
};

template <typename T>
constexpr bool is_inheritable_v_ = //
    (std::is_class_v<T> || std::is_union_v<T>) &&
    !(std::is_abstract_v<T> || std::is_final_v<T>);

template <bool>
struct inheritable_;
template <>
struct inheritable_<false> {
  template <typename T>
  using apply = inheritable_contain_<T>;
};
template <>
struct inheritable_<true> {
  template <typename T>
  using apply = inheritable_inherit_<T>;
};

//  inheritable
//
//  A class wrapping an arbitrary type T which is always inheritable, and which
//  enables empty-base-optimization when possible.
template <typename T, bool C = is_inheritable_v_<T>>
using inheritable = typename inheritable_<C>::template apply<T>;

} // namespace detail

// Prevent child classes from finding anything in folly:: by ADL.
/// Internal namespace hosting the copy/move control base types.
namespace moveonly_ {

/**
 * Disallow copy but not move in derived types. This is essentially
 * boost::noncopyable (the implementation is almost identical), except:
 * 1) It doesn't delete move constructor and move assignment.
 * 2) It has public methods, enabling aggregate initialization.
 */
struct MoveOnly {
  /// Default-constructs the base.
  constexpr MoveOnly() noexcept = default;
  /// Destroys the base.
  ~MoveOnly() noexcept = default;

  /// Move-constructs the base.
  /// \param other The object to move from.
  MoveOnly(MoveOnly&& other) noexcept = default;
  /// Move-assigns the base.
  /// \param other The object to move from.
  /// \returns A reference to this object.
  MoveOnly& operator=(MoveOnly&& other) noexcept = default;
  /// Copy construction is disabled.
  MoveOnly(const MoveOnly& other) = delete;
  /// Copy assignment is disabled.
  MoveOnly& operator=(const MoveOnly& other) = delete;
};

/**
 * Disallow copy and move for derived types. This is essentially
 * boost::noncopyable (the implementation is almost identical), except it has
 * public methods, enabling aggregate initialization.
 */
struct NonCopyableNonMovable {
  /// Default-constructs the base.
  constexpr NonCopyableNonMovable() noexcept = default;
  /// Destroys the base.
  ~NonCopyableNonMovable() noexcept = default;

  /// Move construction is disabled.
  NonCopyableNonMovable(NonCopyableNonMovable&& other) = delete;
  /// Move assignment is disabled.
  NonCopyableNonMovable& operator=(NonCopyableNonMovable&& other) = delete;
  /// Copy construction is disabled.
  NonCopyableNonMovable(const NonCopyableNonMovable& other) = delete;
  /// Copy assignment is disabled.
  NonCopyableNonMovable& operator=(const NonCopyableNonMovable& other) = delete;
};

/// Empty base that permits both copy and move.
struct Default {};

/// Base type selecting copy/move capabilities from the given flags.
template <bool Copy, bool Move>
using EnableCopyMove = std::conditional_t<
    Copy,
    Default,
    std::conditional_t<Move, MoveOnly, NonCopyableNonMovable>>;

} // namespace moveonly_

/// Base that disallows copy but allows move in derived types.
using moveonly_::MoveOnly;
/// Base that disallows both copy and move in derived types.
using moveonly_::NonCopyableNonMovable;

/// variadic_noop
/// variadic_noop_fn
///
/// An invocable object and type that has no side-effects - that does nothing
/// when invoked regardless of the arguments with which it is invoked - and that
/// returns void.
///
/// May be invoked with any arguments. Returns void.
struct variadic_noop_fn {
  /// Does nothing regardless of the arguments passed.
  /// \param args The ignored arguments.
  template <typename... A>
  constexpr void operator()(A&&... args) const noexcept {}
};
/// An invocable object that does nothing and returns void.
inline constexpr variadic_noop_fn variadic_noop;

/// variadic_constant_of
/// variadic_constant_of_fn
///
/// An invocable object and type that has no side-effects - that does nothing
/// when invoked regardless of the arguments with which it is invoked - and that
/// returns a constant value.
template <auto Value>
struct variadic_constant_of_fn {
  /// Type of the constant value returned.
  using value_type = decltype(Value);
  /// The constant value returned on invocation.
  static inline constexpr value_type value = Value;
  /// Returns the constant value regardless of the arguments passed.
  /// \param args The ignored arguments.
  /// \returns The constant value.
  template <typename... A>
  constexpr value_type operator()(A&&... args) const noexcept {
    return value;
  }
};
/// An invocable object that ignores its arguments and returns a constant.
template <auto Value>
inline constexpr variadic_constant_of_fn<Value> variadic_constant_of;

//  unsafe_default_initialized
//  unsafe_default_initialized_cv
//
//  An object which is explicitly convertible to any default-constructible type
//  and which, upon conversion, yields a default-initialized value of that type.
//
//  https://en.cppreference.com/w/cpp/language/default_initialization
//
//  For fundamental types, a default-initialized instance may have indeterminate
//  value. Reading an indeterminate value is undefined behavior but may offer a
//  performance optimization. When using an indeterminate value as a performance
//  optimization, it is best to be explicit.
//
//  Useful as an escape hatch when enabling warnings or errors:
//  * gcc:
//    * uninitialized
//    * maybe-uninitialized
//  * clang:
//    * uninitialized
//    * conditional-uninitialized
//    * sometimes-uninitialized
//    * uninitialized-const-reference
//  * msvc:
//    * C4701: potentially uninitialized local variable used
//    * C4703: potentially uninitialized local pointer variable used
//
//  Example:
//
//      int local = folly::unsafe_default_initialized;
//      store_value_into_int_ptr(&value); // suppresses possible warning
//      use_value(value); // suppresses possible warning
/// Convertible to any default-constructible type, yielding a
/// default-initialized value on conversion.
struct unsafe_default_initialized_cv {
  FOLLY_PUSH_WARNING
  // MSVC requires warning disables to be outside of function definition
  // Uninitialized local variable 'uninit' used
  FOLLY_MSVC_DISABLE_WARNING(4700)
  // Potentially uninitialized local variable 'uninit' used
  FOLLY_MSVC_DISABLE_WARNING(4701)
  // Potentially uninitialized local pointer variable 'uninit' used
  FOLLY_MSVC_DISABLE_WARNING(4703)
  // Using uninitialized memory `uninit`
  FOLLY_MSVC_DISABLE_WARNING(6001)
  FOLLY_GNU_DISABLE_WARNING("-Wuninitialized")
  // Clang doesn't implement -Wmaybe-uninitialized and warns about it
  FOLLY_GCC_DISABLE_WARNING("-Wmaybe-uninitialized")
  /// Converts to a default-initialized value of the target type.
  /// \returns A default-initialized value of type T.
  template <typename T>
  FOLLY_ERASE constexpr /* implicit */ operator T() const noexcept {
#if defined(__cpp_lib_is_constant_evaluated)
#if __cpp_lib_is_constant_evaluated >= 201811L
#if (defined(_MSC_VER) && !defined(__MSVC_RUNTIME_CHECKS)) || \
    (defined(__clang__) && !defined(__GNUC__))
    if (!std::is_constant_evaluated()) {
      T uninit;
      return uninit;
    }
#endif
#endif
#endif
    return T();
  }
  FOLLY_POP_WARNING
};
/// Object yielding a default-initialized value on conversion.
inline constexpr unsafe_default_initialized_cv unsafe_default_initialized{};

/// to_bool
/// to_bool_fn
///
/// Constructs a boolean from the argument.
///
/// Particularly useful for testing sometimes-weak function declarations. They
/// may be declared weak on some platforms but not on others. GCC likes to warn
/// about them but the warning is unhelpful.
struct to_bool_fn {
  /// Constructs a boolean from the argument.
  /// \param t The value to convert.
  /// \returns The value converted to bool.
  template <typename..., typename T>
  FOLLY_ERASE constexpr auto operator()(T const& t) const noexcept
      -> decltype(static_cast<bool>(t)) {
    FOLLY_PUSH_WARNING
    FOLLY_GCC_DISABLE_WARNING("-Waddress")
    FOLLY_GCC_DISABLE_WARNING("-Wnonnull-compare")
    return static_cast<bool>(t);
    FOLLY_POP_WARNING
  }
};
/// An invocable object converting its argument to bool.
inline constexpr to_bool_fn to_bool{};

/// Converts an integral value to its signed counterpart.
struct to_signed_fn {
  /// Converts the argument to the corresponding signed type.
  /// \param t The value to convert.
  /// \returns The value as its signed counterpart.
  template <typename..., typename T>
  constexpr auto operator()(T const& t) const noexcept ->
      typename std::make_signed<T>::type {
    using S = typename std::make_signed<T>::type;
    // note: static_cast<S>(t) would be more straightforward, but it would also
    // be implementation-defined behavior and that is typically to be avoided;
    // the following code optimized into the same thing, though
    constexpr auto m = static_cast<T>(std::numeric_limits<S>::max());
    return static_cast<S>(m < t ? -static_cast<S>(~t) + S{-1} : t);
  }
};
/// An invocable object converting an integral to its signed counterpart.
inline constexpr to_signed_fn to_signed{};

/// Converts an integral value to its unsigned counterpart.
struct to_unsigned_fn {
  /// Converts the argument to the corresponding unsigned type.
  /// \param t The value to convert.
  /// \returns The value as its unsigned counterpart.
  template <typename..., typename T>
  constexpr auto operator()(T const& t) const noexcept ->
      typename std::make_unsigned<T>::type {
    using U = typename std::make_unsigned<T>::type;
    return static_cast<U>(t);
  }
};
/// An invocable object converting an integral to its unsigned counterpart.
inline constexpr to_unsigned_fn to_unsigned{};

namespace detail {
template <typename Src, typename Dst>
inline constexpr bool is_to_narrow_convertible_v =
    (std::is_integral<Dst>::value) &&
    (std::is_signed<Dst>::value == std::is_signed<Src>::value);
}

/// Holds an integral value for possibly-narrowing conversion to a target type.
template <typename Src>
class to_narrow_convertible {
  static_assert(std::is_integral<Src>::value, "not an integer");

  template <typename Dst>
  struct to_
      : std::bool_constant<detail::is_to_narrow_convertible_v<Src, Dst>> {};

 public:
  /// Wraps the source value.
  /// \param value The integral value to wrap.
  explicit constexpr to_narrow_convertible(Src const& value) noexcept
      : value_(value) {}
  /// Copy-constructs the wrapper.
  /// \param other The wrapper to copy from.
  explicit to_narrow_convertible(to_narrow_convertible const& other) = default;
  /// Move-constructs the wrapper.
  /// \param other The wrapper to move from.
  explicit to_narrow_convertible(to_narrow_convertible&& other) = default;
  /// Copy-assigns the wrapper.
  /// \param other The wrapper to copy from.
  /// \returns A reference to this object.
  to_narrow_convertible& operator=(to_narrow_convertible const& other) =
      default;
  /// Move-assigns the wrapper.
  /// \param other The wrapper to move from.
  /// \returns A reference to this object.
  to_narrow_convertible& operator=(to_narrow_convertible&& other) = default;

  /// Converts the wrapped value to the destination integral type.
  /// \returns The wrapped value narrowed to Dst.
  template <typename Dst, std::enable_if_t<to_<Dst>::value, int> = 0>
  /* implicit */ constexpr operator Dst() const noexcept {
    FOLLY_PUSH_WARNING
    FOLLY_MSVC_DISABLE_WARNING(4244) // lossy conversion: arguments
    FOLLY_MSVC_DISABLE_WARNING(4267) // lossy conversion: variables
    FOLLY_GNU_DISABLE_WARNING("-Wconversion")
    return value_;
    FOLLY_POP_WARNING
  }

 private:
  Src value_;
};

//  to_narrow
//
//  A utility for performing explicit possibly-narrowing integral conversion
//  without specifying the destination type. Does not permit changing signs.
//  Sometimes preferable to static_cast<Dst>(src) to document the intended
//  semantics of the cast.
//
//  Models explicit conversion with an elided destination type. Sits in between
//  a stricter explicit conversion with a named destination type and a more
//  lenient implicit conversion. Implemented with implicit conversion in order
//  to take advantage of the undefined-behavior sanitizer's inspection of all
//  implicit conversions - it checks for truncation, with suppressions in place
//  for warnings which guard against narrowing implicit conversions.
/// Creates a to_narrow_convertible for possibly-narrowing integral conversion.
struct to_narrow_fn {
  /// Wraps the argument for narrowing conversion.
  /// \param src The integral value to wrap.
  /// \returns A to_narrow_convertible holding the value.
  template <typename..., typename Src>
  constexpr auto operator()(Src const& src) const noexcept
      -> to_narrow_convertible<Src> {
    return to_narrow_convertible<Src>{src};
  }
};
/// An invocable object performing possibly-narrowing integral conversion.
inline constexpr to_narrow_fn to_narrow{};

/// Holds a floating-point value for conversion to an integral target type.
template <typename Src>
class to_integral_convertible {
  static_assert(std::is_floating_point<Src>::value, "not a floating-point");

  template <typename Dst>
  static constexpr bool to_ = std::is_integral<Dst>::value;

 public:
  /// Wraps the source value.
  /// \param value The floating-point value to wrap.
  explicit constexpr to_integral_convertible(Src const& value) noexcept
      : value_(value) {}

  /// Copy-constructs the wrapper.
  /// \param other The wrapper to copy from.
  explicit to_integral_convertible(to_integral_convertible const& other) =
      default;
  /// Move-constructs the wrapper.
  /// \param other The wrapper to move from.
  explicit to_integral_convertible(to_integral_convertible&& other) = default;
  /// Copy-assigns the wrapper.
  /// \param other The wrapper to copy from.
  /// \returns A reference to this object.
  to_integral_convertible& operator=(to_integral_convertible const& other) =
      default;
  /// Move-assigns the wrapper.
  /// \param other The wrapper to move from.
  /// \returns A reference to this object.
  to_integral_convertible& operator=(to_integral_convertible&& other) = default;

  /// Converts the wrapped value to the destination integral type.
  /// \returns The wrapped value converted to Dst.
  template <typename Dst, std::enable_if_t<to_<Dst>, int> = 0>
  /* implicit */ constexpr operator Dst() const noexcept {
    FOLLY_PUSH_WARNING
    FOLLY_MSVC_DISABLE_WARNING(4244) // lossy conversion: arguments
    FOLLY_MSVC_DISABLE_WARNING(4267) // lossy conversion: variables
    FOLLY_GNU_DISABLE_WARNING("-Wconversion")
    return value_;
    FOLLY_POP_WARNING
  }

 private:
  Src value_;
};

//  to_integral
//
//  A utility for performing explicit floating-point-to-integral conversion
//  without specifying the destination type. Sometimes preferable to
//  static_cast<Dst>(src) to document the intended semantics of the cast.
//
//  Models explicit conversion with an elided destination type. Sits in between
//  a stricter explicit conversion with a named destination type and a more
//  lenient implicit conversion. Implemented with implicit conversion in order
//  to take advantage of the undefined-behavior sanitizer's inspection of all
//  implicit conversions.
/// Creates a to_integral_convertible for floating-point-to-integral conversion.
struct to_integral_fn {
  /// Wraps the argument for integral conversion.
  /// \param src The floating-point value to wrap.
  /// \returns A to_integral_convertible holding the value.
  template <typename..., typename Src>
  constexpr auto operator()(Src const& src) const noexcept
      -> to_integral_convertible<Src> {
    return to_integral_convertible<Src>{src};
  }
};
/// An invocable object performing floating-point-to-integral conversion.
inline constexpr to_integral_fn to_integral{};

/// Holds an integral value for conversion to a floating-point target type.
template <typename Src>
class to_floating_point_convertible {
  static_assert(std::is_integral<Src>::value, "not a floating-point");

  template <typename Dst>
  static constexpr bool to_ = std::is_floating_point<Dst>::value;

 public:
  /// Wraps the source value.
  /// \param value The integral value to wrap.
  explicit constexpr to_floating_point_convertible(Src const& value) noexcept
      : value_(value) {}

  /// Copy-constructs the wrapper.
  /// \param other The wrapper to copy from.
  explicit to_floating_point_convertible(
      to_floating_point_convertible const& other) = default;
  /// Move-constructs the wrapper.
  /// \param other The wrapper to move from.
  explicit to_floating_point_convertible(
      to_floating_point_convertible&& other) = default;
  /// Copy-assigns the wrapper.
  /// \param other The wrapper to copy from.
  /// \returns A reference to this object.
  to_floating_point_convertible& operator=(
      to_floating_point_convertible const& other) = default;
  /// Move-assigns the wrapper.
  /// \param other The wrapper to move from.
  /// \returns A reference to this object.
  to_floating_point_convertible& operator=(
      to_floating_point_convertible&& other) = default;

  /// Converts the wrapped value to the destination floating-point type.
  /// \returns The wrapped value converted to Dst.
  template <typename Dst, std::enable_if_t<to_<Dst>, int> = 0>
  /* implicit */ constexpr operator Dst() const noexcept {
    FOLLY_PUSH_WARNING
    FOLLY_GNU_DISABLE_WARNING("-Wconversion")
    return value_;
    FOLLY_POP_WARNING
  }

 private:
  Src value_;
};

//  to_floating_point
//
//  A utility for performing explicit integral-to-floating-point conversion
//  without specifying the destination type. Sometimes preferable to
//  static_cast<Dst>(src) to document the intended semantics of the cast.
//
//  Models explicit conversion with an elided destination type. Sits in between
//  a stricter explicit conversion with a named destination type and a more
//  lenient implicit conversion. Implemented with implicit conversion in order
//  to take advantage of the undefined-behavior sanitizer's inspection of all
//  implicit conversions.
/// Creates a to_floating_point_convertible for integral-to-floating conversion.
struct to_floating_point_fn {
  /// Wraps the argument for floating-point conversion.
  /// \param src The integral value to wrap.
  /// \returns A to_floating_point_convertible holding the value.
  template <typename..., typename Src>
  constexpr auto operator()(Src const& src) const noexcept
      -> to_floating_point_convertible<Src> {
    return to_floating_point_convertible<Src>{src};
  }
};
/// An invocable object performing integral-to-floating-point conversion.
inline constexpr to_floating_point_fn to_floating_point{};

/// Converts an enumeration value to its underlying integral type.
struct to_underlying_fn {
  /// Converts an enum value to its underlying type.
  /// \param e The enumeration value to convert.
  /// \returns The underlying integral value.
  template <typename..., class E>
  constexpr std::underlying_type_t<E> operator()(E e) const noexcept {
    static_assert(std::is_enum<E>::value, "not an enum type");
    return static_cast<std::underlying_type_t<E>>(e);
  }
};
/// An invocable object converting an enum to its underlying type.
inline constexpr to_underlying_fn to_underlying{};

namespace detail {
template <typename R>
using invocable_to_detect = decltype(FOLLY_DECLVAL(R)());

template <
    typename F,
    //  MSVC 14.16.27023 does not permit these to be in the class body:
    //    error C2833: 'operator decltype' is not a recognized operator or type
    //  TODO: return these to the class body and remove the static assertions
    typename TML = detected_t<invocable_to_detect, F&>,
    typename TCL = detected_t<invocable_to_detect, F const&>,
    typename TMR = detected_t<invocable_to_detect, F&&>,
    typename TCR = detected_t<invocable_to_detect, F const&&>>
class invocable_to_convertible : private inheritable<F> {
 private:
  static_assert(std::is_same<F, decay_t<F>>::value, "mismatch");

  using base = inheritable<F>;

  template <typename R>
  using result_t = detected_t<invocable_to_detect, R>;
  template <typename R>
  static constexpr bool detected_v = is_detected_v<invocable_to_detect, R>;
  template <typename R>
  using if_invocable_as_v = std::enable_if_t<detected_v<R>, int>;
  template <typename R>
  static constexpr bool nx_v = noexcept(FOLLY_DECLVAL(R)());
  template <typename G>
  static constexpr bool constructible_v = std::is_constructible<F, G&&>::value;

  using FML = F&;
  using FCL = F const&;
  using FMR = F&&;
  using FCR = F const&&;
  static_assert(std::is_same<TML, result_t<FML>>::value, "mismatch");
  static_assert(std::is_same<TCL, result_t<FCL>>::value, "mismatch");
  static_assert(std::is_same<TMR, result_t<FMR>>::value, "mismatch");
  static_assert(std::is_same<TCR, result_t<FCR>>::value, "mismatch");

  using BML = base&;
  using BCL = base const&;
  using BMR = base&&;
  using BCR = base const&&;

 public:
  template <typename G, std::enable_if_t<constructible_v<G&&>, int> = 0>
  FOLLY_ERASE explicit constexpr invocable_to_convertible(G&& g) noexcept(
      noexcept(F(static_cast<G&&>(g))))
      : inheritable<F>(static_cast<G&&>(g)) {}

  template <typename..., typename R = FML, if_invocable_as_v<R> = 0>
  FOLLY_ERASE constexpr operator TML() & noexcept(nx_v<R>) {
    return static_cast<FML>(static_cast<BML>(*this))();
  }
  template <typename..., typename R = FCL, if_invocable_as_v<R> = 0>
  FOLLY_ERASE constexpr operator TCL() const& noexcept(nx_v<R>) {
    return static_cast<FCL>(static_cast<BCL>(*this))();
  }
  template <typename..., typename R = FMR, if_invocable_as_v<R> = 0>
  FOLLY_ERASE constexpr operator TMR() && noexcept(nx_v<R>) {
    return static_cast<FMR>(static_cast<BMR>(*this))();
  }
  template <typename..., typename R = FCR, if_invocable_as_v<R> = 0>
  FOLLY_ERASE constexpr operator TCR() const&& noexcept(nx_v<R>) {
    return static_cast<FCR>(static_cast<BCR>(*this))();
  }
};
} // namespace detail

//  invocable_to
//  invocable_to_fn
//
//  Given an invocable, returns an object which is implicitly convertible to the
//  type which the invocable returns when invoked with no arguments. Conversion
//  invokes the invocables and returns the value.
//
//  The return object has unspecified type with the following semantics:
//  * It stores a decay-copy of the passed invocable.
//  * It defines four-way conversion operators. Each conversion operator purely
//    forwards to the invocable as forwarded-like the convertible, and has the
//    same exception specification and the same participation in overload
//    resolution as invocation of the invocable.
//
//  Example:
//
//    Given a setup:
//
//      struct stable {
//        int value = 0;
//        stable() = default;
//        stable(stable const&); // expensive!
//      };
//      std::list<stable const> list;
//
//    The goal is to insert a stable with a value of 7 to the back of the list.
//
//    The obvious ways are expensive:
//
//      stable obj;
//      obj.value = 7;
//      list.push_back(obj); // or variations with emplace_back or std::move
//
//    With a lambda and copy elision optimization (NRVO), the expense remains:
//
//      list.push_back(std::invoke([] {
//        stable obj;
//        obj.value = 7;
//        return obj;
//      }));
//
//    But conversion, as done with this utility, makes this goal achievable.
//
//      list.emplace_back(folly::invocable_to([] {
//        stable obj;
//        obj.value = 7;
//        return obj;
//      }));
/// Wraps an invocable in an object implicitly convertible to its result type.
struct invocable_to_fn {
  /// Wraps the invocable for lazy conversion to its result type.
  /// \param f The invocable to wrap.
  /// \returns An object that invokes f on conversion.
  template <
      typename F,
      typename...,
      typename D = detail::decay_t<F>,
      typename R = detail::invocable_to_convertible<D>,
      std::enable_if_t<std::is_constructible<D, F&&>::value, int> = 0>
  FOLLY_ERASE constexpr R operator()(F&& f) const
      noexcept(noexcept(R(static_cast<F&&>(f)))) {
    return R(static_cast<F&&>(f));
  }
};
/// An invocable object producing a lazily-converting wrapper.
inline constexpr invocable_to_fn invocable_to{};

/// object_from_member
/// object_from_member_fn
///
/// Returns the object containing the given field.
///
/// Similar to container_of (linux kernel).
///
/// Example:
///
///   using obj_t = std::pair<int, float>;
///   obj_t obj = {1, 3.0};
///   assert(&obj == object_from_member(obj_t::second, &obj.second);
struct object_from_member_fn {
 private:
  template <typename M, typename O>
  using ptr_t = M O::*;

  template <typename M, typename O>
  static std::ptrdiff_t off(ptr_t<M, O> const p) noexcept {
    O* o = nullptr;
    M* m = &(o->*p);
    return reinterpret_cast<char*>(m) - reinterpret_cast<char*>(o);
  }

 public:
  /// Returns the enclosing object from a pointer to one of its members.
  /// \param p Pointer-to-member identifying the field.
  /// \param m Pointer to the member subobject.
  /// \returns A pointer to the enclosing object.
  template <typename M, typename O>
  O* operator()(ptr_t<M, O> const p, M* m) const noexcept {
    return reinterpret_cast<O*>(reinterpret_cast<char*>(m) - off(p));
  }
  /// Returns the enclosing object from a pointer to one of its members.
  /// \param p Pointer-to-member identifying the field.
  /// \param m Pointer to the const member subobject.
  /// \returns A pointer to the enclosing const object.
  template <typename M, typename O>
  O const* operator()(ptr_t<M, O> const p, M const* m) const noexcept {
    return reinterpret_cast<O*>(reinterpret_cast<char const*>(m) - off(p));
  }

  /// Returns the enclosing object from a reference to one of its members.
  /// \param p Pointer-to-member identifying the field.
  /// \param m Reference to the member subobject.
  /// \returns A reference to the enclosing object.
  template <typename M, typename O>
  O& operator()(ptr_t<M, O> const p, M& m) const noexcept {
    return *operator()(p, &m);
  }
  /// Returns the enclosing object from a reference to one of its members.
  /// \param p Pointer-to-member identifying the field.
  /// \param m Reference to the const member subobject.
  /// \returns A reference to the enclosing const object.
  template <typename M, typename O>
  O const& operator()(ptr_t<M, O> const p, M const& m) const noexcept {
    return *operator()(p, &m);
  }
  /// Returns the enclosing object from an rvalue reference to one of its
  /// members.
  /// \param p Pointer-to-member identifying the field.
  /// \param m Rvalue reference to the member subobject.
  /// \returns An rvalue reference to the enclosing object.
  template <typename M, typename O>
  O&& operator()(ptr_t<M, O> const p, M&& m) const noexcept {
    return static_cast<O&&>(*operator()(p, &m));
  }
  /// Returns the enclosing object from a const reference to one of its members.
  /// \param p Pointer-to-member identifying the field.
  /// \param m Reference to the const member subobject.
  /// \returns A const rvalue reference to the enclosing object.
  template <typename M, typename O>
  O const&& operator()(ptr_t<M, O> const p, M const& m) const noexcept {
    return static_cast<O const&&>(*operator()(p, &m));
  }
};
/// An invocable object recovering an object from a pointer to its member.
inline constexpr object_from_member_fn object_from_member{};

#define FOLLY_DETAIL_FORWARD_BODY(...)                     \
  noexcept(noexcept(__VA_ARGS__))->decltype(__VA_ARGS__) { \
    return __VA_ARGS__;                                    \
  }

/// FOLLY_FOR_EACH_THIS_OVERLOAD_IN_CLASS_BODY_DELEGATE
///
/// Helper macro to add 4 delegated, qualifier-overloaded methods to a class
///
/// Example:
///
///     template <typename T>
///     class optional {
///      public:
///       bool has_value() const;
///
///       T& value() & {
///         if (!has_value()) { throw std::bad_optional_access(); }
///         return m_value;
///       }
///
///       const T& value() const& {
///         if (!has_value()) { throw std::bad_optional_access(); }
///         return m_value;
///       }
///
///       T&& value() && {
///         if (!has_value()) { throw std::bad_optional_access(); }
///         return std::move(m_value);
///       }
///
///       const T&& value() const&& {
///         if (!has_value()) { throw std::bad_optional_access(); }
///         return std::move(m_value);
///       }
///     };
///
/// This is equivalent to
///
///     template <typename T>
///     class optional {
///       template <typename Self>
///       decltype(auto) value_impl(Self&& self) {
///         if (!self.has_value()) {
///           throw std::bad_optional_access();
///         }
///         return std::forward<Self>(self).m_value;
///       }
///       // ...
///
///      public:
///       bool has_value() const;
///
///       FOLLY_FOR_EACH_THIS_OVERLOAD_IN_CLASS_BODY_DELEGATE(value,
///       value_impl);
///     };
///
/// Note: This can be migrated to C++23's deducing this:
/// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html
///
/// \param MEMBER Name of the member function to generate.
/// \param DELEGATE Name of the static implementation to delegate to.
// clang-format off
#define FOLLY_FOR_EACH_THIS_OVERLOAD_IN_CLASS_BODY_DELEGATE(MEMBER, DELEGATE) \
  template <class... Args>                                                    \
  [[maybe_unused]] FOLLY_ERASE_HACK_GCC                                       \
  constexpr auto MEMBER(Args&&... args) & FOLLY_DETAIL_FORWARD_BODY(          \
      ::folly::remove_cvref_t<decltype(*this)>::DELEGATE(                     \
          *this, static_cast<Args&&>(args)...))                               \
  template <class... Args>                                                    \
  [[maybe_unused]] FOLLY_ERASE_HACK_GCC                                       \
  constexpr auto MEMBER(Args&&... args) const& FOLLY_DETAIL_FORWARD_BODY(     \
      ::folly::remove_cvref_t<decltype(*this)>::DELEGATE(                     \
          *this, static_cast<Args&&>(args)...))                               \
  template <class... Args>                                                    \
  [[maybe_unused]] FOLLY_ERASE_HACK_GCC                                       \
  constexpr auto MEMBER(Args&&... args) && FOLLY_DETAIL_FORWARD_BODY(         \
      ::folly::remove_cvref_t<decltype(*this)>::DELEGATE(                     \
          std::move(*this), static_cast<Args&&>(args)...))                    \
  template <class... Args>                                                    \
  [[maybe_unused]] FOLLY_ERASE_HACK_GCC                                       \
  constexpr auto MEMBER(Args&&... args) const&& FOLLY_DETAIL_FORWARD_BODY(    \
      ::folly::remove_cvref_t<decltype(*this)>::DELEGATE(                     \
          std::move(*this), static_cast<Args&&>(args)...))                    \
  /* enforce semicolon after macro */ static_assert(true)
// clang-format on
} // namespace folly
