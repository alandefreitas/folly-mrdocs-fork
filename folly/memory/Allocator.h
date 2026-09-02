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
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <memory>
#include <type_traits>

#include <folly/Likely.h>
#include <folly/Memory.h>
#include <folly/Traits.h>
#include <folly/functional/Invoke.h>
#include <folly/lang/Exception.h>
#include <folly/memory/Malloc.h>

namespace folly {

/**
 * SysAllocator
 *
 * Resembles std::allocator, the default Allocator, but wraps std::malloc and
 * std::free.
 */
template <typename T>
class SysAllocator {
 private:
  using Self = SysAllocator<T>;

 public:
  /// The type of object allocated by this allocator.
  using value_type = T;

  /// Constructs a default `SysAllocator`.
  constexpr SysAllocator() = default;

  /// Copy-constructs a `SysAllocator`.
  ///
  /// \param other The source allocator to copy from.
  constexpr SysAllocator(SysAllocator const& other) = default;

  /// Constructs a `SysAllocator` from an allocator for a different type.
  ///
  /// \tparam U The value type of the source allocator.
  /// \param other The source allocator to rebind from.
  template <typename U, std::enable_if_t<!std::is_same<U, T>::value, int> = 0>
  constexpr SysAllocator(SysAllocator<U> const& other) noexcept {}

  /// Allocates storage for `count` objects of type `T`.
  ///
  /// \param count The number of objects to allocate storage for.
  /// \returns A pointer to the allocated storage.
  T* allocate(size_t count) {
    auto const p = std::malloc(sizeof(T) * count);
    if (!p) {
      throw_exception<std::bad_alloc>();
    }
    return static_cast<T*>(p);
  }
  /// Deallocates storage previously obtained from `allocate`.
  ///
  /// \param p Pointer to the storage to deallocate.
  /// \param count The number of objects the storage was allocated for.
  void deallocate(T* p, size_t count) { sizedFree(p, count * sizeof(T)); }

  /// Compares two `SysAllocator` instances for equality.
  ///
  /// \param a The first allocator.
  /// \param b The second allocator.
  /// \returns Always `true`, since all instances are interchangeable.
  friend bool operator==(Self const& a, Self const& b) noexcept {
    return true;
  }
  /// Compares two `SysAllocator` instances for inequality.
  ///
  /// \param a The first allocator.
  /// \param b The second allocator.
  /// \returns Always `false`, since all instances are interchangeable.
  friend bool operator!=(Self const& a, Self const& b) noexcept {
    return false;
  }
};

/// An alignment policy that carries a runtime alignment value.
///
/// Provides the alignment for `AlignedSysAllocator`, chosen at run time.
class DefaultAlign {
 private:
  using Self = DefaultAlign;
  std::size_t align_;

 public:
  /// Constructs the policy with a runtime alignment.
  ///
  /// \param align The alignment, in bytes. Must be a power of two and at
  ///     least `sizeof(void*)`.
  explicit DefaultAlign(std::size_t align) noexcept : align_(align) {
    assert(!(align_ < sizeof(void*)) && bool("bad align: too small"));
    assert(!(align_ & (align_ - 1)) && bool("bad align: not power-of-two"));
  }
  /// Returns the configured alignment.
  ///
  /// \returns The alignment, in bytes.
  std::size_t operator()() const noexcept { return align_; }

  /// Compares two policies for equality.
  ///
  /// \param a The first policy.
  /// \param b The second policy.
  /// \returns `true` if both hold the same alignment.
  friend bool operator==(Self const& a, Self const& b) noexcept {
    return a.align_ == b.align_;
  }
  /// Compares two policies for inequality.
  ///
  /// \param a The first policy.
  /// \param b The second policy.
  /// \returns `true` if the policies hold different alignments.
  friend bool operator!=(Self const& a, Self const& b) noexcept {
    return a.align_ != b.align_;
  }
};

/// An alignment policy that carries a compile-time alignment value.
///
/// Provides a fixed alignment for `AlignedSysAllocator`.
///
/// \tparam Align The alignment, in bytes. Must be a power of two and at
///     least `sizeof(void*)`.
template <std::size_t Align>
class FixedAlign {
 private:
  static_assert(!(Align < sizeof(void*)), "bad align: too small");
  static_assert(!(Align & (Align - 1)), "bad align: not power-of-two");
  using Self = FixedAlign<Align>;

 public:
  /// Returns the fixed alignment.
  ///
  /// \returns The alignment, in bytes.
  constexpr std::size_t operator()() const noexcept { return Align; }

  /// Compares two policies for equality.
  ///
  /// \param a The first policy.
  /// \param b The second policy.
  /// \returns Always `true`, since all instances are interchangeable.
  friend bool operator==(Self const& a, Self const& b) noexcept {
    return true;
  }
  /// Compares two policies for inequality.
  ///
  /// \param a The first policy.
  /// \param b The second policy.
  /// \returns Always `false`, since all instances are interchangeable.
  friend bool operator!=(Self const& a, Self const& b) noexcept {
    return false;
  }
};

/**
 * AlignedSysAllocator
 *
 * Resembles std::allocator, the default Allocator, but wraps aligned_malloc and
 * aligned_free.
 *
 * Accepts a policy parameter for providing the alignment, which must:
 *   * be invocable as std::size_t(std::size_t) noexcept
 *     * taking the type alignment and returning the allocation alignment
 *   * be noexcept-copy-constructible
 *   * have noexcept operator==
 *   * have noexcept operator!=
 *   * not be final
 *
 * DefaultAlign and FixedAlign<std::size_t>, provided above, are valid policies.
 */
template <typename T, typename Align = DefaultAlign>
class AlignedSysAllocator : private Align {
 private:
  using Self = AlignedSysAllocator<T, Align>;

  template <typename, typename>
  friend class AlignedSysAllocator;

  constexpr Align const& align() const { return *this; }

 public:
  static_assert(std::is_nothrow_copy_constructible<Align>::value);
  static_assert(is_nothrow_invocable_r_v<std::size_t, Align>);

  /// The type of object allocated by this allocator.
  using value_type = T;

  /// Propagates the allocator on container copy assignment.
  using propagate_on_container_copy_assignment = std::true_type;
  /// Propagates the allocator on container move assignment.
  using propagate_on_container_move_assignment = std::true_type;
  /// Propagates the allocator on container swap.
  using propagate_on_container_swap = std::true_type;

  using Align::Align;

  /// Constructs the allocator with a default-constructed alignment policy.
  ///
  /// \tparam S Deduced alignment policy type, defaulting to `Align`.
  // TODO: remove this ctor, which is is no longer required as of under gcc7
  template <
      typename S = Align,
      std::enable_if_t<std::is_default_constructible<S>::value, int> = 0>
  constexpr AlignedSysAllocator() noexcept(noexcept(Align())) : Align() {}

  /// Copy-constructs an `AlignedSysAllocator`.
  ///
  /// \param other The source allocator to copy from.
  constexpr AlignedSysAllocator(AlignedSysAllocator const& other) = default;

  /// Constructs an `AlignedSysAllocator` from an allocator for a different type.
  ///
  /// \tparam U The value type of the source allocator.
  /// \param other The source allocator to rebind from.
  template <typename U, std::enable_if_t<!std::is_same<U, T>::value, int> = 0>
  constexpr AlignedSysAllocator(
      AlignedSysAllocator<U, Align> const& other) noexcept
      : Align(other.align()) {}

  /// Allocates aligned storage for `count` objects of type `T`.
  ///
  /// \param count The number of objects to allocate storage for.
  /// \returns A pointer to the allocated storage.
  T* allocate(size_t count) {
    auto const a = align()() < alignof(T) ? alignof(T) : align()();
    auto const p = aligned_malloc(sizeof(T) * count, a);
    if (!p) {
      if (FOLLY_UNLIKELY(errno != ENOMEM)) {
        std::terminate();
      }
      throw_exception<std::bad_alloc>();
    }
    return static_cast<T*>(p);
  }
  /// Deallocates storage previously obtained from `allocate`.
  ///
  /// \param p Pointer to the storage to deallocate.
  /// \param count The number of objects the storage was allocated for.
  void deallocate(T* p, size_t count) { aligned_free(p); }

  /// Compares two allocators for equality.
  ///
  /// \param a The first allocator.
  /// \param b The second allocator.
  /// \returns `true` if both use the same alignment policy.
  friend bool operator==(Self const& a, Self const& b) noexcept {
    return a.align() == b.align();
  }
  /// Compares two allocators for inequality.
  ///
  /// \param a The first allocator.
  /// \param b The second allocator.
  /// \returns `true` if the allocators use different alignment policies.
  friend bool operator!=(Self const& a, Self const& b) noexcept {
    return a.align() != b.align();
  }
};

/**
 * CxxAllocatorAdaptor
 *
 * A type conforming to C++ concept Allocator, delegating operations to an
 * unowned Inner which has this required interface:
 *
 *   void* allocate(std::size_t)
 *   void deallocate(void*, std::size_t)
 *
 * Note that Inner is *not* a C++ Allocator.
 */
template <typename T, class Inner, bool FallbackToStdAlloc = false>
class CxxAllocatorAdaptor : private std::allocator<T> {
 private:
  using Self = CxxAllocatorAdaptor<T, Inner, FallbackToStdAlloc>;

  template <typename U, typename UInner, bool UFallback>
  friend class CxxAllocatorAdaptor;

  Inner* inner_ = nullptr;

 public:
  /// The type of object allocated by this allocator.
  using value_type = T;

  /// Propagates the allocator on container copy assignment.
  using propagate_on_container_copy_assignment = std::true_type;
  /// Propagates the allocator on container move assignment.
  using propagate_on_container_move_assignment = std::true_type;
  /// Propagates the allocator on container swap.
  using propagate_on_container_swap = std::true_type;

  /// Constructs an adaptor with no bound `Inner`.
  ///
  /// Available only when `FallbackToStdAlloc` is `true`, in which case
  /// allocation falls back to `std::allocator<T>`.
  ///
  /// \tparam X Deduced flag equal to `FallbackToStdAlloc`.
  template <bool X = FallbackToStdAlloc, std::enable_if_t<X, int> = 0>
  constexpr explicit CxxAllocatorAdaptor() {}

  /// Constructs an adaptor bound to an `Inner` instance.
  ///
  /// \param ref The unowned `Inner` to delegate allocations to.
  constexpr explicit CxxAllocatorAdaptor(Inner& ref) : inner_(&ref) {}

  /// Copy-constructs a `CxxAllocatorAdaptor`.
  ///
  /// \param other The source adaptor to copy from.
  constexpr CxxAllocatorAdaptor(CxxAllocatorAdaptor const& other) = default;

  /// Constructs an adaptor from one for a different value type.
  ///
  /// \tparam U The value type of the source adaptor.
  /// \param other The source adaptor to rebind from.
  template <typename U, std::enable_if_t<!std::is_same<U, T>::value, int> = 0>
  constexpr CxxAllocatorAdaptor(
      CxxAllocatorAdaptor<U, Inner, FallbackToStdAlloc> const& other)
      : inner_(other.inner_) {}

  /// Copy-assigns a `CxxAllocatorAdaptor`.
  ///
  /// \param other The source adaptor.
  /// \returns A reference to this adaptor.
  CxxAllocatorAdaptor& operator=(CxxAllocatorAdaptor const& other) = default;

  /// Assigns from an adaptor for a different value type.
  ///
  /// \tparam U The value type of the source adaptor.
  /// \param other The source adaptor to rebind from.
  /// \returns A reference to this adaptor.
  template <typename U, std::enable_if_t<!std::is_same<U, T>::value, int> = 0>
  CxxAllocatorAdaptor& operator=(
      CxxAllocatorAdaptor<U, Inner, FallbackToStdAlloc> const& other) noexcept {
    inner_ = other.inner_;
    return *this;
  }

  /// Allocates storage for `n` objects of type `T`.
  ///
  /// \param n The number of objects to allocate storage for.
  /// \returns A pointer to the allocated storage.
  T* allocate(std::size_t n) {
    if (FallbackToStdAlloc && inner_ == nullptr) {
      return std::allocator<T>::allocate(n);
    }
    return static_cast<T*>(inner_->allocate(sizeof(T) * n));
  }

  /// Deallocates storage previously obtained from `allocate`.
  ///
  /// \param p Pointer to the storage to deallocate.
  /// \param n The number of objects the storage was allocated for.
  void deallocate(T* p, std::size_t n) {
    if (inner_ != nullptr) {
      inner_->deallocate(p, sizeof(T) * n);
    } else {
      assert(FallbackToStdAlloc);
      std::allocator<T>::deallocate(p, n);
    }
  }

  /// Compares two adaptors for equality.
  ///
  /// \param a The first adaptor.
  /// \param b The second adaptor.
  /// \returns `true` if both refer to the same `Inner`.
  friend bool operator==(Self const& a, Self const& b) noexcept {
    return a.inner_ == b.inner_;
  }
  /// Compares two adaptors for inequality.
  ///
  /// \param a The first adaptor.
  /// \param b The second adaptor.
  /// \returns `true` if the adaptors refer to different `Inner` instances.
  friend bool operator!=(Self const& a, Self const& b) noexcept {
    return a.inner_ != b.inner_;
  }

  /// Rebinds the adaptor to a different value type.
  ///
  /// \tparam U The new value type.
  template <typename U>
  struct rebind {
    /// The rebound adaptor type.
    using other = CxxAllocatorAdaptor<U, Inner, FallbackToStdAlloc>;
  };
};

/**
 * AllocatorHasTrivialDeallocate
 *
 * Unambiguously inherits std::integral_constant<bool, V> for some bool V.
 *
 * Describes whether a C++ Aallocator has trivial, i.e. no-op, deallocate().
 *
 * Also may be used to describe types which may be used with
 * CxxAllocatorAdaptor.
 */
template <typename Alloc>
struct AllocatorHasTrivialDeallocate : std::false_type {};

template <typename T, class Alloc>
struct AllocatorHasTrivialDeallocate<CxxAllocatorAdaptor<T, Alloc>>
    : AllocatorHasTrivialDeallocate<Alloc> {};

namespace detail {
// note that construct and destroy here are methods, not short names for
// the constructor and destructor
FOLLY_CREATE_MEMBER_INVOKER(AllocatorConstruct_, construct);
FOLLY_CREATE_MEMBER_INVOKER(AllocatorDestroy_, destroy);

template <typename Void, typename Alloc, typename... Args>
struct AllocatorCustomizesConstruct_
    : folly::is_invocable<AllocatorConstruct_, Alloc, Args...> {};

template <typename Alloc, typename... Args>
struct AllocatorCustomizesConstruct_<
    std::void_t<typename Alloc::folly_has_default_object_construct>,
    Alloc,
    Args...>
    : std::negation<typename Alloc::folly_has_default_object_construct> {};

template <typename Void, typename Alloc, typename... Args>
struct AllocatorCustomizesDestroy_
    : folly::is_invocable<AllocatorDestroy_, Alloc, Args...> {};

template <typename Alloc, typename... Args>
struct AllocatorCustomizesDestroy_<
    std::void_t<typename Alloc::folly_has_default_object_destroy>,
    Alloc,
    Args...> : std::negation<typename Alloc::folly_has_default_object_destroy> {
};
} // namespace detail

/**
 * AllocatorHasDefaultObjectConstruct
 *
 * AllocatorHasDefaultObjectConstruct<A, T, Args...> unambiguously
 * inherits std::integral_constant<bool, V>, where V will be true iff
 * the effect of std::allocator_traits<A>::construct(a, p, args...) is
 * the same as new (static_cast<void*>(p)) T(args...).  If true then
 * any optimizations applicable to object construction (relying on
 * std::is_trivially_copyable<T>, for example) can be applied to objects
 * in an allocator-aware container using an allocation of type A.
 *
 * Allocator types can override V by declaring a type alias for
 * folly_has_default_object_construct.  It is helpful to do this if you
 * define a custom allocator type that defines a construct method, but
 * that method doesn't do anything except call placement new.
 */
template <typename Alloc, typename T, typename... Args>
struct AllocatorHasDefaultObjectConstruct
    : std::negation<
          detail::AllocatorCustomizesConstruct_<void, Alloc, T*, Args...>> {};

template <typename Value, typename T, typename... Args>
struct AllocatorHasDefaultObjectConstruct<std::allocator<Value>, T, Args...>
    : std::true_type {};

/**
 * AllocatorHasDefaultObjectDestroy
 *
 * AllocatorHasDefaultObjectDestroy<A, T> unambiguously inherits
 * std::integral_constant<bool, V>, where V will be true iff the effect
 * of std::allocator_traits<A>::destroy(a, p) is the same as p->~T().
 * If true then optimizations applicable to object destruction (relying
 * on std::is_trivially_destructible<T>, for example) can be applied to
 * objects in an allocator-aware container using an allocator of type A.
 *
 * Allocator types can override V by declaring a type alias for
 * folly_has_default_object_destroy.  It is helpful to do this if you
 * define a custom allocator type that defines a destroy method, but that
 * method doesn't do anything except call the object's destructor.
 */
template <typename Alloc, typename T>
struct AllocatorHasDefaultObjectDestroy
    : std::negation<detail::AllocatorCustomizesDestroy_<void, Alloc, T*>> {};

template <typename Value, typename T>
struct AllocatorHasDefaultObjectDestroy<std::allocator<Value>, T>
    : std::true_type {};

} // namespace folly
