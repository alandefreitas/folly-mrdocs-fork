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

#include <type_traits>
#include <utility>

#include <folly/functional/Invoke.h>

/// The Folly library.
namespace folly {
/// Implementation details.
namespace dptr_detail {

/// Determine the result type of applying a visitor of type V on pointers of
/// all types in Types..., asserting that the type is the same for all types
/// in Types...
template <typename V, typename... Types>
struct VisitorResult {
  /// The result of applying the visitor to a pointer of type T.
  template <typename T>
  using res = invoke_result_t<V, T*>;
  /// The common result type across all visited types.
  using type = std::common_type_t<res<Types>...>;
  static_assert((std::is_same_v<type, res<Types>> && ...));
};

/// Determine the result type of applying a visitor of type V on const pointers
/// of all types in Types..., asserting that the type is the same for all types
/// in Types...
template <typename V, typename... Types>
struct ConstVisitorResult {
  /// The result of applying the visitor to a const pointer of type T.
  template <typename T>
  using res = invoke_result_t<V, const T*>;
  /// The common result type across all visited types.
  using type = std::common_type_t<res<Types>...>;
  static_assert((std::is_same_v<type, res<Types>> && ...));
};

/// Dispatches a visitor to a pointer whose type is selected at runtime.
template <typename... Types>
struct ApplyVisitor {
  /// Applies the visitor to the pointer cast to T*.
  ///
  /// \param visitor The visitor to invoke.
  /// \param ptr The pointer to cast and visit.
  /// \returns The result of invoking the visitor.
  template <typename V, typename T, typename R>
  static R one(V& visitor, void* ptr) {
    return visitor(static_cast<T*>(ptr));
  }

  /// Applies the visitor to the pointer using the runtime type index.
  ///
  /// \param runtimeIndex The one-based index selecting the pointer type.
  /// \param visitor The visitor to invoke.
  /// \param ptr The pointer to visit.
  /// \returns The result of invoking the visitor.
  template <typename V, typename R = _t<VisitorResult<V&, Types...>>>
  R operator()(size_t runtimeIndex, V& visitor, void* ptr) const {
    using F = R(V&, void*);
    constexpr F* f[] = {nullptr, &one<V, Types, R>...};
    return f[runtimeIndex](visitor, ptr);
  }
};

/// Dispatches a visitor to a const pointer whose type is selected at runtime.
template <typename... Types>
struct ApplyConstVisitor {
  /// Applies the visitor to the pointer cast to const T*.
  ///
  /// \param visitor The visitor to invoke.
  /// \param ptr The pointer to cast and visit.
  /// \returns The result of invoking the visitor.
  template <typename V, typename T, typename R>
  static R one(V& visitor, void* ptr) {
    return visitor(static_cast<const T*>(ptr));
  }

  /// Applies the visitor to the pointer using the runtime type index.
  ///
  /// \param runtimeIndex The one-based index selecting the pointer type.
  /// \param visitor The visitor to invoke.
  /// \param ptr The pointer to visit.
  /// \returns The result of invoking the visitor.
  template <typename V, typename R = _t<ConstVisitorResult<V&, Types...>>>
  R operator()(size_t runtimeIndex, V& visitor, void* ptr) const {
    using F = R(V&, void*);
    constexpr F* f[] = {nullptr, &one<V, Types, R>...};
    return f[runtimeIndex](visitor, ptr);
  }
};

} // namespace dptr_detail
} // namespace folly
