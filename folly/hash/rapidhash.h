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

#include <folly/external/rapidhash/rapidhash.h>

namespace folly {
namespace hash {

// The values returned by Hash, Hash32, and Hash64 are only guaranteed to
// be the same within the same process.  Fingerpring32 and Fingerprint64
// are fixed algorithms that always give the same result.

/// Hash a byte buffer with rapidhash, `uint64_t rapidhash(const char* key, size_t len)`.
using external::rapidhash;

/// Hash a byte buffer with rapidhash and an explicit seed, `uint64_t rapidhash_with_seed(const char* key, size_t len, uint64_t seed)`.
using external::rapidhash_with_seed;

/// Hash a byte buffer with the rapidhashMicro variant, `uint64_t rapidhashMicro(const char* key, size_t len)`.
using external::rapidhashMicro;

/// Hash a byte buffer with rapidhashMicro and an explicit seed, `uint64_t rapidhashMicro_with_seed(const char* key, size_t len, uint64_t seed)`.
using external::rapidhashMicro_with_seed;

/// Hash a byte buffer with the rapidhashNano variant, `uint64_t rapidhashNano(const char* key, size_t len)`.
using external::rapidhashNano;

/// Hash a byte buffer with rapidhashNano and an explicit seed, `uint64_t rapidhashNano_with_seed(const char* key, size_t len, uint64_t seed)`.
using external::rapidhashNano_with_seed;

} // namespace hash
} // namespace folly
