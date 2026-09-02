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

#include <folly/Portability.h>
#include <folly/lang/Hint.h>

namespace folly {

/**
 * Ensure that a value is computed even after optimization.
 *
 * Call doNotOptimizeAway(var) to ensure that var will be computed even
 * post-optimization.  Use it for variables that are computed during
 * benchmarking but otherwise are useless. The compiler tends to do a
 * good job at eliminating unused variables, and this function fools it
 * into thinking var is in fact needed.
 *
 * \param datum The value to keep from being optimized away.
 */

template <class T>
FOLLY_ALWAYS_INLINE void doNotOptimizeAway(const T& datum) {
  compiler_must_not_elide(datum);
}

/**
 * Hide a value from the optimizer so it cannot shape the following code.
 *
 * Call makeUnpredictable(var) when you don't want the optimizer to use
 * its knowledge of var to shape the following code.  This is useful
 * when constant propagation or power reduction is possible during your
 * benchmark but not in real use cases.
 *
 * \param datum The value to make opaque to the optimizer.
 */
template <typename T>
FOLLY_ALWAYS_INLINE void makeUnpredictable(T& datum) {
  compiler_must_not_predict(datum);
}

namespace detail {
size_t bm_llc_size_fallback();
}

/// Calculates the size of the LLC (Last Level Cache, typically L3 on x86-64).
///
/// \returns The size of the last level cache, in bytes.
size_t bm_llc_size();

/// Evict cache lines by writing to a large block of memory.
///
/// Write to a large block of memory and evict whatever cache lines might
/// currently be resident in any levels of cache.
///
/// \param value Number of bytes to write over in order to evict the cache.
void bm_llc_evict(size_t value);

} // namespace folly
