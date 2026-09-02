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

#include <folly/functional/Invoke.h>

/// The Folly library.
namespace folly {
/// Folly's observer library.
namespace observer {
template <typename T>
class Observer;
}

/// Implementation details.
namespace observer_detail {

/// Yields the decayed type unless `T` is a `shared_ptr`, in which case it is empty.
template <typename T>
struct NonSharedPtr {
  /// The decayed non-`shared_ptr` type.
  using type = typename std::decay<T>::type;
};

template <typename T>
struct NonSharedPtr<std::shared_ptr<T>> {};

/// Yields the decayed type unless `T` is an `Observer`, in which case it is empty.
template <typename T>
struct NonObserver {
  /// The decayed non-`Observer` type.
  using type = typename std::decay<T>::type;
};

template <typename T>
struct NonObserver<observer::Observer<T>> {};

/// Extracts the element type from a `shared_ptr`; empty for other types.
template <typename T>
struct UnwrapSharedPtr {};

template <typename T>
struct UnwrapSharedPtr<std::shared_ptr<T>> {
  using type = typename std::decay<T>::type;
};

/// Extracts the value type from an `Observer`; empty for other types.
template <typename T>
struct UnwrapObserver {};

template <typename T>
struct UnwrapObserver<observer::Observer<T>> {
  using type = T;
};

/// The result of invoking `F`, with any `shared_ptr` or `Observer` wrapper removed.
template <typename F>
using ResultOf =
    typename NonObserver<typename NonSharedPtr<invoke_result_t<F>>::type>::type;

/// The result of invoking `F`, with any `shared_ptr` wrapper removed.
template <typename F>
using ResultOfNoObserverUnwrap =
    typename NonSharedPtr<invoke_result_t<F>>::type;

/// The `shared_ptr` element type produced by invoking `F`.
template <typename F>
using ResultOfUnwrapSharedPtr =
    typename UnwrapSharedPtr<invoke_result_t<F>>::type;

/// The `Observer` value type produced by invoking `F`.
template <typename F>
using ResultOfUnwrapObserver =
    typename UnwrapObserver<invoke_result_t<F>>::type;

/// Removes an `Observer` wrapper from a type, if present.
template <typename T>
struct Unwrap {
  /// The unwrapped type.
  using type = T;
};

template <typename T>
struct Unwrap<observer::Observer<T>> {
  using type = T;
};
} // namespace observer_detail
} // namespace folly
