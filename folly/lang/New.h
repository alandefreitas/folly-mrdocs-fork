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

#include <new>

#include <folly/CppAttributes.h>
#include <folly/Portability.h>
#include <folly/functional/Invoke.h>

namespace folly {

namespace detail {

#if defined(__cpp_aligned_new)
constexpr auto cpp_aligned_new_ = __cpp_aligned_new >= 201606;
#else
constexpr auto cpp_aligned_new_ = false;
#endif

#if defined(__cpp_sized_deallocation)
constexpr auto cpp_sized_deallocation_ = __cpp_sized_deallocation >= 201309L;
#else
constexpr auto cpp_sized_deallocation_ = false;
#endif

//  https://clang.llvm.org/docs/LanguageExtensions.html#builtin-operator-new-and-builtin-operator-delete

constexpr auto op_new_builtin_ =
    FOLLY_HAS_BUILTIN(__builtin_operator_new) >= 201802L;
constexpr auto op_del_builtin_ =
    FOLLY_HAS_BUILTIN(__builtin_operator_del) >= 201802L;

FOLLY_CREATE_QUAL_INVOKER(op_new_builtin_fn_, __builtin_operator_new);
FOLLY_CREATE_QUAL_INVOKER(op_new_library_fn_, ::operator new);

FOLLY_CREATE_QUAL_INVOKER(op_del_builtin_fn_, __builtin_operator_delete);
FOLLY_CREATE_QUAL_INVOKER(op_del_library_fn_, ::operator delete);

template <bool Usual, bool C = (Usual && op_new_builtin_)>
constexpr conditional_t<C, op_new_builtin_fn_, op_new_library_fn_> op_new_;

template <bool Usual, bool C = (Usual && op_del_builtin_)>
constexpr conditional_t<C, op_del_builtin_fn_, op_del_library_fn_> op_del_;

template <bool Usual, typename... A>
FOLLY_ERASE void do_op_del_sized_(
    void* const p, std::size_t const s, A const... a) {
  if constexpr (detail::cpp_sized_deallocation_) {
    return op_del_<Usual>(p, s, a...);
  } else {
    return op_del_<Usual>(p, a...);
  }
}

} // namespace detail

/// Function object that invokes the global `operator new`.
struct operator_new_fn {
  /// Allocates `s` bytes and returns a pointer to the storage.
  /// @param s The number of bytes to allocate.
  /// @return A pointer to the allocated storage.
  [[nodiscard]] FOLLY_ERASE void* operator()( //
      std::size_t const s) const //
      noexcept(noexcept(::operator new(0))) {
    return detail::op_new_<true>(s);
  }
  /// Allocates `s` bytes without throwing, returning null on failure.
  /// @param s The number of bytes to allocate.
  /// @param nt The nothrow tag selecting the non-throwing overload.
  /// @return A pointer to the allocated storage, or null on failure.
  [[nodiscard]] FOLLY_ERASE void* operator()( //
      std::size_t const s,
      std::nothrow_t const& nt) const noexcept {
    return detail::op_new_<true>(s, nt);
  }
  /// Allocates `s` bytes with the given alignment.
  /// @param s The number of bytes to allocate.
  /// @param a The alignment of the allocated storage.
  /// @return A pointer to the allocated storage.
  [[nodiscard]] FOLLY_ERASE void* operator()( //
      std::size_t const s,
      std::align_val_t const a) const //
      noexcept(noexcept(::operator new(0))) {
    return detail::op_new_<detail::cpp_aligned_new_>(s, a);
  }
  /// Allocates `s` bytes with the given alignment without throwing.
  /// @param s The number of bytes to allocate.
  /// @param a The alignment of the allocated storage.
  /// @param nt The nothrow tag selecting the non-throwing overload.
  /// @return A pointer to the allocated storage, or null on failure.
  [[nodiscard]] FOLLY_ERASE void* operator()( //
      std::size_t const s,
      std::align_val_t const a,
      std::nothrow_t const& nt) const noexcept {
    return detail::op_new_<detail::cpp_aligned_new_>(s, a, nt);
  }
};
/// Invokes the global `operator new`.
inline constexpr operator_new_fn operator_new{};

/// Function object that invokes the global `operator delete`.
struct operator_delete_fn {
  /// Frees the storage pointed to by `p`.
  /// @param p A pointer to the storage to free.
  FOLLY_ERASE void operator()( //
      void* const p) const noexcept {
    return detail::op_del_<true>(p);
  }
  /// Frees the storage pointed to by `p` using its size.
  /// @param p A pointer to the storage to free.
  /// @param s The size in bytes of the storage being freed.
  FOLLY_ERASE void operator()( //
      void* const p,
      std::size_t const s) const noexcept {
    return detail::do_op_del_sized_<true>(p, s);
  }
  /// Frees the storage pointed to by `p` using its alignment.
  /// @param p A pointer to the storage to free.
  /// @param a The alignment of the storage being freed.
  FOLLY_ERASE void operator()( //
      void* const p,
      std::align_val_t const a) const noexcept {
    return detail::op_del_<detail::cpp_aligned_new_>(p, a);
  }
  /// Frees the storage pointed to by `p` using its size and alignment.
  /// @param p A pointer to the storage to free.
  /// @param s The size in bytes of the storage being freed.
  /// @param a The alignment of the storage being freed.
  FOLLY_ERASE void operator()( //
      void* const p,
      std::size_t const s,
      std::align_val_t const a) const noexcept {
    return detail::do_op_del_sized_<detail::cpp_aligned_new_>(p, s, a);
  }
};
/// Invokes the global `operator delete`.
inline constexpr operator_delete_fn operator_delete{};

} // namespace folly
