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
/**
 * Helper functions to create std::arrays.
 *
 * @file container/Array.h
 * @refcode folly/docs/examples/folly/container/Array.cpp
 */

#pragma once

#include <array>
#include <type_traits>
#include <utility>

#include <folly/CPortability.h>
#include <folly/Traits.h>
#include <folly/Utility.h>

namespace folly {

/// Implementation helpers for make_array and make_array_with.
namespace array_detail {
/// Trait that is true when `T` is a std::reference_wrapper.
template <class T>
using is_ref_wrapper = is_instantiation_of<std::reference_wrapper, T>;

/// Trait that is true when the decayed `T` is not a std::reference_wrapper.
template <typename T>
using not_ref_wrapper =
    std::negation<is_ref_wrapper<typename std::decay<T>::type>>;

/// Resolves the element type of the array produced by make_array.
template <typename D, typename...>
struct return_type_helper {
  /// The resolved array element type.
  using type = D;
};
template <typename... TList>
struct return_type_helper<void, TList...> {
  static_assert(
      std::conjunction_v<not_ref_wrapper<TList>...>,
      "TList cannot contain reference_wrappers when D is void");
  using type = typename std::common_type<TList...>::type;
};

/// The std::array type make_array returns for the given element types.
template <typename D, typename... TList>
using return_type = std::
    array<typename return_type_helper<D, TList...>::type, sizeof...(TList)>;
} // namespace array_detail

/// Constructs a std::array with the given argument list.
///
/// @param t  The values to be put in the array.
/// \returns A std::array holding the forwarded arguments.
template <typename D = void, typename... TList>
constexpr array_detail::return_type<D, TList...> make_array(TList&&... t) {
  using value_type =
      typename array_detail::return_type_helper<D, TList...>::type;
  return {{static_cast<value_type>(std::forward<TList>(t))...}};
}

namespace array_detail {
/// Builds a std::array by invoking `make` for each index in the sequence.
///
/// \param make The generator invoked as make(i) for each index.
/// \param seq The index sequence enumerating the array positions.
/// \returns A std::array whose i-th element is make(i).
template <typename MakeItem, std::size_t... Index>
FOLLY_ERASE constexpr auto make_array_with_(
    MakeItem const& make, std::index_sequence<Index...> seq) {
  return std::array<decltype(make(0)), sizeof...(Index)>{{make(Index)...}};
}
} // namespace array_detail

/// Generates a std::array<..., Size> with elements m(i) for i in [0, Size).
///
/// @tparam Size  The size of the array
/// @param make  The generator that makes the array elements. ret[i] = make(i)
/// \returns A std::array whose i-th element is make(i) for i in [0, Size).
template <std::size_t Size, typename MakeItem>
constexpr auto make_array_with(MakeItem const& make) {
  return array_detail::make_array_with_(make, std::make_index_sequence<Size>{});
}

} // namespace folly
