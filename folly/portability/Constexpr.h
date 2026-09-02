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

#include <folly/CPortability.h>
#include <folly/Portability.h>

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace folly {

namespace detail {

#if FOLLY_HAS_FEATURE(cxx_constexpr_string_builtins) || \
    FOLLY_HAS_BUILTIN(__builtin_strlen) || defined(_MSC_VER)
#define FOLLY_DETAIL_STRLEN __builtin_strlen
#else
#define FOLLY_DETAIL_STRLEN ::std::strlen
#endif

#if FOLLY_HAS_FEATURE(cxx_constexpr_string_builtins) || \
    FOLLY_HAS_BUILTIN(__builtin_strcmp)
#define FOLLY_DETAIL_STRCMP __builtin_strcmp
#else
#define FOLLY_DETAIL_STRCMP ::std::strcmp
#endif

// This overload is preferred if Char is char and if FOLLY_DETAIL_STRLEN
// yields a compile-time constant.
template <
    typename Char,
    std::size_t = FOLLY_DETAIL_STRLEN(static_cast<const Char*>(""))>
constexpr std::size_t constexpr_strlen_internal(const Char* s, int) noexcept {
  return FOLLY_DETAIL_STRLEN(s);
}
template <typename Char>
constexpr std::size_t constexpr_strlen_internal(
    const Char* s, unsigned) noexcept {
  std::size_t ret = 0;
  while (*s++) {
    ++ret;
  }
  return ret;
}

template <typename Char>
constexpr std::size_t constexpr_strlen_fallback(const Char* s) noexcept {
  return constexpr_strlen_internal(s, 0u);
}

static_assert(
    constexpr_strlen_fallback("123456789") == 9,
    "Someone appears to have broken constexpr_strlen...");

// This overload is preferred if Char is char and if FOLLY_DETAIL_STRCMP
// yields a compile-time constant.
template <
    typename Char,
    int = FOLLY_DETAIL_STRCMP(static_cast<const Char*>(""), "")>
constexpr int constexpr_strcmp_internal(
    const Char* s1, const Char* s2, int) noexcept {
  return FOLLY_DETAIL_STRCMP(s1, s2);
}
template <typename Char>
constexpr int constexpr_strcmp_internal(
    const Char* s1, const Char* s2, unsigned) noexcept {
  while (*s1 && *s1 == *s2) {
    ++s1, ++s2;
  }
  // NOTE: `int(*s1 - *s2)` may cause signed arithmetics overflow which is UB.
  return int(*s2 < *s1) - int(*s1 < *s2);
}

template <typename Char>
constexpr int constexpr_strcmp_fallback(
    const Char* s1, const Char* s2) noexcept {
  return constexpr_strcmp_internal(s1, s2, 0u);
}

#undef FOLLY_DETAIL_STRCMP
#undef FOLLY_DETAIL_STRLEN

} // namespace detail

/// Computes the length of a null-terminated string at compile time.
///
/// \param s The null-terminated string.
/// \returns The number of characters preceding the terminating null.
template <typename Char>
constexpr std::size_t constexpr_strlen(const Char* s) noexcept {
#if __GNUC_PREREQ(11, 0)
  return detail::constexpr_strlen_internal(s, 0u);
#else
  return detail::constexpr_strlen_internal(s, 0);
#endif
}

/// Compares two null-terminated strings at compile time.
///
/// \param s1 The first null-terminated string.
/// \param s2 The second null-terminated string.
/// \returns A negative, zero, or positive value if `s1` orders before, equal
/// to, or after `s2`.
template <typename Char>
constexpr int constexpr_strcmp(const Char* s1, const Char* s2) noexcept {
  return detail::constexpr_strcmp_internal(s1, s2, 0);
}

namespace detail {

template <typename V>
struct is_constant_evaluated_or_constinit_ {
  V value;
  FOLLY_ERASE consteval /* implicit */
      is_constant_evaluated_or_constinit_(V const v) noexcept(noexcept(V(v)))
      : value{v} {}
};

} // namespace detail

/// Reports whether evaluation occurs in a constant context, with a default.
///
/// Similar in spirit to `std::is_constant_evaluated` (C++20), but takes an
/// argument used as the default return value when the code cannot tell whether
/// it is in a constant context.
///
/// \param def The value returned when the constant context cannot be detected.
/// \returns True if evaluated in a constant context, otherwise `def`.
constexpr bool is_constant_evaluated_or(
    detail::is_constant_evaluated_or_constinit_<bool> const def) noexcept {
  (void)def; // silence unused variable warning
  return std::is_constant_evaluated();
}

} // namespace folly
