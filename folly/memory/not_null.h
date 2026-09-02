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

/**
 * C++ Core Guideline's not_null PtrT>.
 *
 * not_null<PtrT> holds a pointer-like type PtrT which is not nullptr.
 *
 * not_null<T*> is a drop-in replacement for T* (as long as it's never null).
 * Specializations not_null_unique_ptr<T> and not_null_shared_ptr<T> are
 * drop-in replacements for unique_ptr<T> and shared_ptr<T>, respecitively.
 *
 * Example:
 *    void foo(not_null<int*> nnpi) {
 *      *nnpi = 7; // Safe, since `nnpi` is not null.
 *    }
 *
 *    void bar(not_null_shared_ptr<int> nnspi) {
 *      foo(nnsp.get());
 *    }
 *
 * Notes:
 *    - Constructing a not_null<PtrT> from a nullptr-equivalent argument throws
 *      a std::invalid_argument exception.
 *    - Cannot be used after move.
 *    - In debug mode, not_null checks that it is not null on all accesses,
 *      since use-after-move can cause the underlying PtrT to be null.
 */

#include <cstddef>
#include <functional>
#include <iosfwd>
#include <memory>
#include <type_traits>

#include <folly/Traits.h>

namespace folly {

namespace detail {
template <typename T>
struct is_not_null;
template <typename FromT, typename ToT>
struct is_not_null_convertible;
template <typename FromT, typename ToPtrT>
struct is_not_null_nothrow_constructible;
template <typename FromPtrT, typename ToT>
struct is_not_null_castable;
template <typename FromPtrT, typename ToT>
struct is_not_null_move_castable;

template <bool, template <typename...> class>
struct check_constraint_if_not {};

template <template <typename...> class DelayedConstraint>
struct check_constraint_if_not<true, DelayedConstraint> {
  template <typename... Args>
  constexpr static bool apply = true;
};

template <template <typename...> class DelayedConstraint>
struct check_constraint_if_not<false, DelayedConstraint> {
  template <typename... Args>
  constexpr static bool apply = DelayedConstraint<Args...>::value;
};

} // namespace detail

/// Base that grants trusted subclasses a token to bypass null checks.
class guaranteed_not_null_provider {
 protected:
  /// Tag type marking a pointer as already guaranteed non-null.
  struct guaranteed_not_null {};
};

/// Default policy handling null-pointer violations for not_null.
class default_null_handler {
 public:
  /// Handle termination when the invariant of the pointer being null is
  /// violated, such as when doing debug checking after moving from a not_null
  /// smart pointer.
  ///
  /// \param msg Diagnostic message describing the violation.
  [[noreturn]] static inline void handle_terminate(const char* msg);

  /// Handle when code attempts to construct or assign a not_null object from a
  /// null pointer.
  ///
  /// \param msg Diagnostic message describing the violation.
  [[noreturn]] static inline void handle_throw(const char* msg);
};

/**
 * not_null_base, the common interface for all not_null subclasses.
 *  - Implicitly constructs and casts just like a PtrT.
 *  - Has unwrap() function to access the underlying PtrT.
 */
template <typename PtrT, typename NullHandlerT>
class not_null_base : protected guaranteed_not_null_provider {
  template <bool>
  struct implicit_tag {};

 public:
  /// The wrapped pointer-like type.
  using pointer = PtrT;
  /// The type pointed to by the wrapped pointer.
  using element_type = typename std::pointer_traits<PtrT>::element_type;

  /**
   * Construction:
   *  - Throws std::invalid_argument if null.
   *  - Cannot default construct.
   *  - Cannot construct from nullptr.
   *  - Allows implicit construction iff PtrT allows implicit construction.
   *  - Construction from another not_null skips null check (in opt builds).
   */
  not_null_base() = delete;
  /// Deleted: a not_null cannot be constructed from a null pointer.
  /// \param unused Ignored null pointer argument.
  /* implicit */ not_null_base(std::nullptr_t unused) = delete;
  /// Destroys the object, destroying the wrapped pointer.
  ~not_null_base() = default;

  /// Copy constructor; performs no null check.
  ///
  /// Copy and move constructors from other not_null_base types don't do any
  /// null checking and thus will only throw if the underlying type's
  /// constructor throws.
  ///
  /// \param nn Source object to copy from.
  not_null_base(const not_null_base& nn) = default;
  /// Move constructor; performs no null check.
  /// \param nn Source object to move from.
  not_null_base(not_null_base&& nn) = default;

  /// Implicitly constructs from a pointer-like value convertible to PtrT.
  /// \param u Pointer-like value to wrap; must not be null.
  /// \param unused Tag selecting the implicit overload.
  template <typename U>
    requires detail::is_not_null_convertible<U&&, PtrT>::value
  /* implicit */ not_null_base(U&& u, implicit_tag<true> unused = {}) noexcept(
      detail::is_not_null_nothrow_constructible<U&&, PtrT>::value);

  /// Explicitly constructs from a pointer-like value not implicitly convertible
  /// to PtrT.
  /// \param u Pointer-like value to wrap; must not be null.
  /// \param unused Tag selecting the explicit overload.
  template <typename U>
    requires(!detail::is_not_null_convertible<U &&, PtrT>::value)
  explicit not_null_base(U&& u, implicit_tag<false> unused = {}) noexcept(
      detail::is_not_null_nothrow_constructible<U&&, PtrT>::value);

  /// Constructs without a null check, for trusted callsites.
  /// \param ptr Pointer known to be non-null.
  /// \param unused Token proving the pointer is guaranteed non-null.
  explicit not_null_base(
      PtrT&& ptr,
      guaranteed_not_null_provider::guaranteed_not_null unused) noexcept;

  /**
   * Assignment:
   *  - Due to implicit construction, just need to assign from self.
   *  - Cannot assign from nullptr.
   */
  /// Deleted: cannot assign a null pointer to a not_null.
  /// \param unused Ignored null pointer argument.
  /// \returns Reference to this object.
  not_null_base& operator=(std::nullptr_t unused) = delete;
  /// Copy assignment.
  /// \param nn Source object to copy from.
  /// \returns Reference to this object.
  not_null_base& operator=(const not_null_base& nn) = default;
  /// Move assignment.
  /// \param nn Source object to move from.
  /// \returns Reference to this object.
  not_null_base& operator=(not_null_base&& nn) = default;

  /**
   * Dereferencing:
   *  - Does not return mutable references, since that would allow the
   *    underlying pointer to be assigned to nullptr.
   */
  /// Dereferences the wrapped pointer.
  /// \returns A reference to the pointed-to object.
  element_type& operator*() const noexcept;
  /// Accesses members through the wrapped pointer.
  /// \returns A reference to the wrapped pointer.
  const PtrT& operator->() const noexcept;

  /**
   * Casting:
   *  - Implicit casting to PtrT allowed, so that not_null<PtrT> can be used
   *    wherever a PtrT is expected.
   *  - Does not return mutable references, since that would allow the
   *    underlying pointer to be assigned to nullptr.
   *  - Boolean cast is always true.
   */
  /// Implicitly converts to a const reference to the wrapped pointer.
  /// \returns A const reference to the wrapped pointer.
  operator const PtrT&() const& noexcept;
  /// Implicitly converts an rvalue to the wrapped pointer.
  /// \returns An rvalue reference to the wrapped pointer.
  operator PtrT&&() && noexcept;

  /// Converts to another pointer type constructible from the wrapped pointer.
  /// \returns The wrapped pointer converted to type U.
  template <
      typename U,
      typename = std::enable_if_t<detail::check_constraint_if_not<
          std::is_same_v<PtrT, folly::remove_cvref_t<U>>,
          detail::is_not_null_castable>::template apply<PtrT, U>>>
  operator U() const& noexcept(std::is_nothrow_constructible_v<U, const PtrT&>);

  /// Converts an rvalue to another pointer type constructible from the wrapped
  /// pointer.
  /// \returns The wrapped pointer converted to type U.
  template <
      typename U,
      typename = std::enable_if_t<detail::check_constraint_if_not<
          std::is_same_v<PtrT, folly::remove_cvref_t<U>>,
          detail::is_not_null_move_castable>::template apply<PtrT, U>>>
  operator U() && noexcept(std::is_nothrow_constructible_v<U, PtrT&&>);

  /// Boolean conversion; always true since the pointer is never null.
  /// \returns Always `true`.
  explicit inline operator bool() const noexcept { return true; }

  /**
   * Swap
   */
  /// Swaps the wrapped pointer with another not_null_base.
  /// \param other Object to swap contents with.
  void swap(not_null_base& other) noexcept;

  /**
   * Accessor:
   *  - Can explicitly access the underlying type via `unwrap`.
   *  - Does not return mutable references, since that would allow the
   *    underlying pointer to be assigned to nullptr.
   */
  /// Accesses the underlying pointer.
  /// \returns A const reference to the wrapped pointer.
  const PtrT& unwrap() const& noexcept;
  /// Accesses the underlying pointer of an rvalue.
  /// \returns An rvalue reference to the wrapped pointer.
  PtrT&& unwrap() && noexcept;

 protected:
  /// Throws std::invalid_argument if this object's pointer is null.
  void throw_if_null() const;
  /// Throws std::invalid_argument if the given pointer is null.
  /// \param ptr Pointer to check.
  template <typename T>
  static void throw_if_null(const T& ptr);
  /// Terminates if the given pointer is null.
  /// \param ptr Pointer to check.
  template <typename T>
  static void terminate_if_null(const T& ptr);
  /// Forwards a deleter, throwing if it is null.
  /// \param deleter Deleter to forward.
  /// \returns The forwarded deleter.
  template <typename Deleter>
  static Deleter&& forward_or_throw_if_null(Deleter&& deleter);

  /// Non-const accessor for the underlying pointer.
  /// \returns A mutable reference to the wrapped pointer.
  PtrT& mutable_unwrap() noexcept;

 private:
  struct private_tag {};
  template <typename U>
  not_null_base(U&& u, private_tag);

  PtrT ptr_;
};

/**
 * not_null specializable class.
 *
 * Default implementation is not_null_base.
 */
template <typename PtrT, typename NullHandlerT = default_null_handler>
class not_null : public not_null_base<PtrT, NullHandlerT> {
 public:
  /// The wrapped pointer-like type.
  using pointer = typename not_null_base<PtrT, NullHandlerT>::pointer;
  /// The type pointed to by the wrapped pointer.
  using element_type = typename not_null_base<PtrT, NullHandlerT>::element_type;
  using not_null_base<PtrT, NullHandlerT>::not_null_base;
};

/**
 * not_null<std::unique_ptr<>> specialization.
 *
 * alias: not_null_unique_ptr
 *
 * Provides API compatibility with unique_ptr, except:
 *  - Pointer arguments must be non-null.
 *  - Cannot reset().
 *  - Functions are not noexcept, since debug-mode checks can throw exceptions.
 *  - Promotes returned pointers to be not_null pointers. Implicit casting
 *    allows these to be used in place of regular pointers.
 *
 * Notes:
 *  - Has make_not_null_unique, equivalent to std::make_unique
 */
template <typename T, typename Deleter, typename NullHandlerT>
class not_null<std::unique_ptr<T, Deleter>, NullHandlerT>
    : public not_null_base<std::unique_ptr<T, Deleter>, NullHandlerT> {
 public:
  /// The not_null-wrapped raw pointer type returned by accessors.
  using pointer =
      not_null<typename std::unique_ptr<T, Deleter>::pointer, NullHandlerT>;
  /// The type of the managed object.
  using element_type = typename std::unique_ptr<T, Deleter>::element_type;
  /// The deleter type used to destroy the managed object.
  using deleter_type = typename std::unique_ptr<T, Deleter>::deleter_type;

  /**
   * Constructors. Most are inherited from not_null_base.
   */
  using not_null_base<std::unique_ptr<T, Deleter>, NullHandlerT>::not_null_base;

  /// Constructs from a non-null pointer and a copied deleter.
  /// \param p Non-null pointer to manage.
  /// \param d Deleter to copy.
  not_null(pointer p, const Deleter& d);
  /// Constructs from a non-null pointer and a moved deleter.
  /// \param p Non-null pointer to manage.
  /// \param d Deleter to move.
  not_null(pointer p, Deleter&& d);

  /**
   * not_null_unique_ptr cannot be released - that would cause it to be null.
   */
  /// Deleted: releasing would leave the pointer null.
  /// \returns The managed pointer.
  pointer release() = delete;

  /**
   * not_null_unique_ptr can only be reset to a non-null pointer.
   */
  /// Deleted: cannot reset to a null pointer.
  /// \param unused Ignored null pointer argument.
  void reset(std::nullptr_t unused) = delete;
  /// Resets to manage a different non-null pointer.
  /// \param ptr Non-null pointer to manage.
  void reset(pointer ptr) noexcept;

  /**
   * get() returns a not_null (pointer type is not_null<T*>).
   *
   * Due to implicit casting, can still capture the result of get() as a regular
   * pointer type:
   *
   *   int* ptr = not_null_unique_ptr<int>(...).get(); // valid
   *
   * \returns The managed pointer as a not_null pointer.
   */
  pointer get() const noexcept;

  /**
   * get_deleter(): same as for unique_ptr.
   */
  /// Accesses the deleter.
  /// \returns A reference to the stored deleter.
  Deleter& get_deleter() noexcept;
  /// Accesses the deleter.
  /// \returns A const reference to the stored deleter.
  const Deleter& get_deleter() const noexcept;
};

/// A not_null wrapper over std::unique_ptr.
/// \implementationdefined
template <
    typename T,
    typename Deleter = std::default_delete<T>,
    typename NullHandlerT = default_null_handler>
using not_null_unique_ptr = not_null<std::unique_ptr<T, Deleter>, NullHandlerT>;

/// Creates a not_null_unique_ptr, like std::make_unique.
/// \param args Arguments forwarded to the constructed object.
/// \returns A not_null_unique_ptr owning the new object.
template <typename T, typename... Args>
not_null_unique_ptr<T> make_not_null_unique(Args&&... args);

/**
 * not_null<std::shared_ptr<>> specialization.
 *
 * alias: not_null_shared_ptr
 *
 * Provides API compatibility with shared_ptr, except:
 *  - Pointer arguments must be non-null.
 *  - Cannot reset().
 *  - Functions are not noexcept, since debug-mode checks can throw exceptions.
 *  - Promotes returned pointers to be not_null pointers. Implicit casting
 *    allows these to be used in place of regular pointers.
 *
 * Notes:
 *  - Has make_not_null_shared, equivalent to std::make_shared.
 */
template <typename T, typename NullHandlerT>
class not_null<std::shared_ptr<T>, NullHandlerT>
    : public not_null_base<std::shared_ptr<T>, NullHandlerT> {
 public:
  /// The type of the managed object.
  using element_type = typename std::shared_ptr<T>::element_type;
  /// The not_null-wrapped raw pointer type returned by accessors.
  using pointer = not_null<element_type*, NullHandlerT>;
  /// The corresponding weak pointer type.
  using weak_type = typename std::shared_ptr<T>::weak_type;

  /**
   * Constructors. Most are inherited from not_null_base.
   */
  using not_null_base<std::shared_ptr<T>, NullHandlerT>::not_null_base;

  /// Constructs from a non-null raw pointer and a deleter.
  /// \param ptr Non-null pointer to manage.
  /// \param d Deleter used to destroy the object.
  template <typename U, typename Deleter>
  not_null(U* ptr, Deleter d);
  /// Constructs from a not_null raw pointer and a deleter.
  /// \param ptr Non-null pointer to manage.
  /// \param d Deleter used to destroy the object.
  template <typename U, typename Deleter, typename UNullHandlerT>
  not_null(not_null<U*, UNullHandlerT> ptr, Deleter d);

  /**
   * Aliasing constructors.
   *
   * Notes:
   *  - The aliased shared_ptr argument, @r, is allowed to be null. The
   *    constructed object is not null iff @ptr is.
   *  - Don't template on the null handler of raw value not_null ptr wrappers
   *    so that we get automatic implicit conversion to the null handler of this
   *    destination type when aliasing raw pointer values.
   */
  /// Aliasing constructor sharing ownership with a shared_ptr.
  /// \param r Shared owner to alias; may be null.
  /// \param ptr Non-null stored pointer.
  template <typename U>
  not_null(
      const std::shared_ptr<U>& r,
      not_null<element_type*, NullHandlerT> ptr) noexcept;
  /// Aliasing constructor sharing ownership with a not_null shared pointer.
  /// \param r Shared owner to alias.
  /// \param ptr Non-null stored pointer.
  template <typename U, typename UNullHandlerT>
  not_null(
      const not_null<std::shared_ptr<U>, UNullHandlerT>& r,
      not_null<element_type*, NullHandlerT> ptr) noexcept;
  /// Aliasing constructor taking ownership from an rvalue shared_ptr.
  /// \param r Shared owner to alias; may be null.
  /// \param ptr Non-null stored pointer.
  template <typename U>
  not_null(
      std::shared_ptr<U>&& r,
      not_null<element_type*, NullHandlerT> ptr) noexcept;
  /// Aliasing constructor taking ownership from an rvalue not_null shared
  /// pointer.
  /// \param r Shared owner to alias.
  /// \param ptr Non-null stored pointer.
  template <typename U, typename UNullHandlerT>
  not_null(
      not_null<std::shared_ptr<U>, UNullHandlerT>&& r,
      not_null<element_type*, NullHandlerT> ptr) noexcept;

  /**
   * not_null_shared_ptr can only be reset to a non-null pointer.
   */
  /// Deleted: cannot reset to an empty (null) state.
  void reset() = delete;
  /// Resets to manage a different non-null raw pointer.
  /// \param ptr Non-null pointer to manage.
  template <typename U>
  void reset(U* ptr);
  /// Resets to manage a different not_null raw pointer.
  /// \param ptr Non-null pointer to manage.
  template <typename U, typename UNullHandlerT>
  void reset(not_null<U*, UNullHandlerT> ptr) noexcept;
  /// Resets to manage a non-null raw pointer with a deleter.
  /// \param ptr Non-null pointer to manage.
  /// \param d Deleter used to destroy the object.
  template <typename U, typename Deleter>
  void reset(U* ptr, Deleter d);
  /// Resets to manage a not_null raw pointer with a deleter.
  /// \param ptr Non-null pointer to manage.
  /// \param d Deleter used to destroy the object.
  template <typename U, typename Deleter, typename UNullHandlerT>
  void reset(not_null<U*, UNullHandlerT> ptr, Deleter d);

  /**
   * get() returns a not_null.
   *
   * Due to implicit casting, can still capture the result of get() as a regular
   * pointer type:
   *
   *   int* ptr = not_null_shared_ptr<int>(...).get(); // valid
   *
   * \returns The managed pointer as a not_null pointer.
   */
  pointer get() const noexcept;

  /**
   * use_count()
   * owner_before()
   *
   * Same as shared_ptr.
   *
   * Notes:
   *  - unique() is deprecated in c++17, so is not implemented here. Can call
   *    not_null_shared_ptr.unwrap().unique() as a workaround, until unique()
   *    is removed in C++20.
   */
  /// Returns the number of shared_ptr objects sharing ownership.
  /// \returns The current use count.
  long use_count() const noexcept;
  /// Orders this pointer relative to another shared_ptr by ownership.
  /// \param other Shared pointer to compare against.
  /// \returns `true` if this precedes `other` in owner-based order.
  template <typename U>
  bool owner_before(const std::shared_ptr<U>& other) const noexcept;
  /// Orders this pointer relative to another not_null shared pointer by
  /// ownership.
  /// \param other Not-null shared pointer to compare against.
  /// \returns `true` if this precedes `other` in owner-based order.
  template <typename U, typename UNullHandlerT>
  bool owner_before(
      const not_null<std::shared_ptr<U>, UNullHandlerT>& other) const noexcept;
};

/// A not_null wrapper over std::shared_ptr.
/// \implementationdefined
template <typename T, typename NullHandlerT = default_null_handler>
using not_null_shared_ptr = not_null<std::shared_ptr<T>, NullHandlerT>;

/// Creates a not_null_shared_ptr, like std::make_shared.
/// \param args Arguments forwarded to the constructed object.
/// \returns A not_null_shared_ptr owning the new object.
template <typename T, typename... Args>
not_null_shared_ptr<T> make_not_null_shared(Args&&... args);

/// Creates a not_null_shared_ptr using an allocator, like std::allocate_shared.
/// \param alloc Allocator used for the shared control block and object.
/// \param args Arguments forwarded to the constructed object.
/// \returns A not_null_shared_ptr owning the new object.
template <typename T, typename Alloc, typename... Args>
not_null_shared_ptr<T> allocate_not_null_shared(
    const Alloc& alloc, Args&&... args);

/**
 * Comparison:
 *  - Forwards to underlying PtrT.
 *  - Works when one of the operands is not not_null.
 *  - Works when one of the operands is nullptr.
 *
 * \param op The comparison operator token to generate overloads for.
 */
#define FB_NOT_NULL_MK_OP(op)                                                 \
  template <typename PtrT, typename T, typename LhsNullHandlerT>              \
  bool operator op(const not_null<PtrT, LhsNullHandlerT>& lhs, const T& rhs); \
  template <                                                                  \
      typename PtrT,                                                          \
      typename T,                                                             \
      typename RhsNullHandlerT,                                               \
      typename = std::enable_if_t<!detail::is_not_null<T>::value>>            \
  bool operator op(const T& lhs, const not_null<PtrT, RhsNullHandlerT>& rhs);
/// Equality comparison forwarding to the underlying pointer.
/// \implementationdefined
FB_NOT_NULL_MK_OP(==)
/// Inequality comparison forwarding to the underlying pointer.
/// \implementationdefined
FB_NOT_NULL_MK_OP(!=)
/// Less-than comparison forwarding to the underlying pointer.
/// \implementationdefined
FB_NOT_NULL_MK_OP(<)
/// Less-than-or-equal comparison forwarding to the underlying pointer.
/// \implementationdefined
FB_NOT_NULL_MK_OP(<=)
/// Greater-than comparison forwarding to the underlying pointer.
/// \implementationdefined
FB_NOT_NULL_MK_OP(>)
/// Greater-than-or-equal comparison forwarding to the underlying pointer.
/// \implementationdefined
FB_NOT_NULL_MK_OP(>=)
#undef FB_NOT_NULL_MK_OP

/**
 * Output:
 *  - Forwards to underlying PtrT.
 */
/// Writes the underlying pointer to an output stream.
/// \param os Output stream to write to.
/// \param ptr Not-null pointer whose underlying value is written.
/// \returns The output stream `os`.
template <typename U, typename V, typename PtrT, typename NullHandlerT>
std::basic_ostream<U, V>& operator<<(
    std::basic_ostream<U, V>& os, const not_null<PtrT, NullHandlerT>& ptr);

/**
 * Swap
 */
/// Swaps the contents of two not_null pointers.
/// \param lhs First pointer to swap.
/// \param rhs Second pointer to swap.
template <typename PtrT>
void swap(not_null<PtrT>& lhs, not_null<PtrT>& rhs) noexcept;

/**
 * Getters
 */
/// Returns the deleter owned by a not_null_shared_ptr.
/// \param ptr Not-null shared pointer to query.
/// \returns A pointer to the stored deleter, or nullptr if none of type Deleter.
template <typename Deleter, typename T, typename NullHandlerT>
Deleter* get_deleter(const not_null_shared_ptr<T, NullHandlerT>& ptr);

/**
 * Casting
 */
/// static_cast equivalent for not_null_shared_ptr.
/// \param r Source shared pointer.
/// \returns The casted not_null_shared_ptr.
template <
    typename T,
    typename U,
    typename TNullHandlerT = default_null_handler,
    typename UNullHandlerT = default_null_handler>
not_null_shared_ptr<T, TNullHandlerT> static_pointer_cast(
    const not_null_shared_ptr<U, UNullHandlerT>& r);
/// static_cast equivalent for an rvalue not_null_shared_ptr.
/// \param r Source shared pointer.
/// \returns The casted not_null_shared_ptr.
template <
    typename T,
    typename U,
    typename TNullHandlerT = default_null_handler,
    typename UNullHandlerT = default_null_handler>
not_null_shared_ptr<T, TNullHandlerT> static_pointer_cast(
    not_null_shared_ptr<U, UNullHandlerT>&& r);
/// dynamic_cast equivalent for not_null_shared_ptr.
/// \param r Source shared pointer.
/// \returns The casted shared_ptr, which may be null if the cast fails.
template <typename T, typename U, typename UNullHandlerT = default_null_handler>
std::shared_ptr<T> dynamic_pointer_cast(
    const not_null_shared_ptr<U, UNullHandlerT>& r);
/// dynamic_cast equivalent for an rvalue not_null_shared_ptr.
/// \param r Source shared pointer.
/// \returns The casted shared_ptr, which may be null if the cast fails.
template <typename T, typename U, typename UNullHandlerT = default_null_handler>
std::shared_ptr<T> dynamic_pointer_cast(
    not_null_shared_ptr<U, UNullHandlerT>&& r);
/// const_cast equivalent for not_null_shared_ptr.
/// \param r Source shared pointer.
/// \returns The casted not_null_shared_ptr.
template <
    typename T,
    typename U,
    typename TNullHandlerT = default_null_handler,
    typename UNullHandlerT = default_null_handler>
not_null_shared_ptr<T, TNullHandlerT> const_pointer_cast(
    const not_null_shared_ptr<U, UNullHandlerT>& r);
/// const_cast equivalent for an rvalue not_null_shared_ptr.
/// \param r Source shared pointer.
/// \returns The casted not_null_shared_ptr.
template <
    typename T,
    typename U,
    typename TNullHandlerT = default_null_handler,
    typename UNullHandlerT = default_null_handler>
not_null_shared_ptr<T, TNullHandlerT> const_pointer_cast(
    not_null_shared_ptr<U, UNullHandlerT>&& r);
/// reinterpret_cast equivalent for not_null_shared_ptr.
/// \param r Source shared pointer.
/// \returns The casted not_null_shared_ptr.
template <
    typename T,
    typename U,
    typename TNullHandlerT = default_null_handler,
    typename UNullHandlerT = default_null_handler>
not_null_shared_ptr<T, TNullHandlerT> reinterpret_pointer_cast(
    const not_null_shared_ptr<U, UNullHandlerT>& r);
/// reinterpret_cast equivalent for an rvalue not_null_shared_ptr.
/// \param r Source shared pointer.
/// \returns The casted not_null_shared_ptr.
template <
    typename T,
    typename U,
    typename TNullHandlerT = default_null_handler,
    typename UNullHandlerT = default_null_handler>
not_null_shared_ptr<T, TNullHandlerT> reinterpret_pointer_cast(
    not_null_shared_ptr<U, UNullHandlerT>&& r);

/// Deduction guide: deduces not_null from a pointer-like initializer.
template <typename PtrT>
not_null(PtrT&&) -> not_null<std::remove_cv_t<std::remove_reference_t<PtrT>>>;

} // namespace folly

/// Standard library namespace, reopened to specialize std::hash.
namespace std {
/**
 * Hashing:
 *  - Forwards to underlying PtrT.
 */
template <typename PtrT, typename NullHandlerT>
struct hash<::folly::not_null<PtrT, NullHandlerT>> : hash<PtrT> {};
} // namespace std

#include <folly/memory/not_null-inl.h>
