//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MrDocs override of libc++'s <__algorithm/unwrap_range.h>.
//
// This directory is placed ahead of the bundled libc++ on the include search
// path (via `includes:` in mrdocs.yml, i.e. `-I`, which precedes the libc++
// `-isystem`), so this file shadows the stock header for every translation
// unit.
//
// The stock header builds its result with class template argument deduction:
//   return pair{a, b};
// When MrDocs' Sema instantiates `__unwrap_range` (Bitcoin reaches it through
// prevector.h's <algorithm>), that deduction is reported as
// "ambiguous deduction for template arguments of 'pair'". The plain clang
// driver accepts the same code, so this only surfaces inside MrDocs.
//
// The only change here is spelling the pair type explicitly instead of relying
// on CTAD, which is semantically identical and sidesteps the ambiguity. The
// rest of the file matches the bundled libc++ 23 header verbatim.
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_UNWRAP_RANGE_H
#define _LIBCPP___ALGORITHM_UNWRAP_RANGE_H

#include <__algorithm/unwrap_iter.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__type_traits/is_same.h>
#include <__utility/declval.h>
#include <__utility/move.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20
template <class _Iter, class _Sent>
_LIBCPP_HIDE_FROM_ABI constexpr auto __unwrap_range(_Iter __first, _Sent __last) {
  if constexpr (is_same_v<_Iter, _Sent>) {
    auto __uf = std::__unwrap_iter(std::move(__first));
    auto __ul = std::__unwrap_iter(std::move(__last));
    return pair<decltype(__uf), decltype(__ul)>(std::move(__uf), std::move(__ul));
  } else if constexpr (random_access_iterator<_Iter> && sized_sentinel_for<_Sent, _Iter>) {
    auto __iter_last = __first + (__last - __first);
    auto __uf = std::__unwrap_iter(std::move(__first));
    auto __ul = std::__unwrap_iter(std::move(__iter_last));
    return pair<decltype(__uf), decltype(__ul)>(std::move(__uf), std::move(__ul));
  } else {
    auto __mf = std::move(__first);
    auto __ml = std::move(__last);
    return pair<decltype(__mf), decltype(__ml)>(std::move(__mf), std::move(__ml));
  }
}

template < class _Sent, class _Iter, class _Unwrapped>
_LIBCPP_HIDE_FROM_ABI constexpr _Iter __rewrap_range(_Iter __orig_iter, _Unwrapped __iter) {
  if constexpr (is_same_v<_Iter, _Sent>)
    return std::__rewrap_iter(std::move(__orig_iter), std::move(__iter));
  else if constexpr (random_access_iterator<_Iter> && sized_sentinel_for<_Sent, _Iter>)
    return std::__rewrap_iter(std::move(__orig_iter), std::move(__iter));
  else
    return __iter;
}
#else  // _LIBCPP_STD_VER >= 20
template <class _Iter, class _Unwrapped = decltype(std::__unwrap_iter(std::declval<_Iter>()))>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR pair<_Unwrapped, _Unwrapped> __unwrap_range(_Iter __first, _Iter __last) {
  return std::make_pair(std::__unwrap_iter(std::move(__first)), std::__unwrap_iter(std::move(__last)));
}

template <class _Iter, class _Unwrapped>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _Iter __rewrap_range(_Iter __orig_iter, _Unwrapped __iter) {
  return std::__rewrap_iter(std::move(__orig_iter), std::move(__iter));
}
#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_UNWRAP_RANGE_H
