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

#include <memory>

#include <folly/container/HeterogeneousAccess-fwd.h>

namespace folly {
namespace f14 {
/// Default hasher for F14 containers keyed on `T`.
template <typename T>
using DefaultHasher = HeterogeneousAccessHash<T>;

/// Default key-equality comparator for F14 containers keyed on `T`.
template <typename T>
using DefaultKeyEqual = HeterogeneousAccessEqualTo<T>;

/// Default allocator for F14 container elements of type `T`.
template <typename T>
using DefaultAlloc = std::allocator<T>;
} // namespace f14
} // namespace folly
