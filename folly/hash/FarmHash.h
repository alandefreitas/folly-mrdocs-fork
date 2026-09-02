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

#include <folly/external/farmhash/farmhash.h>

namespace folly {
namespace hash {
/// FarmHash hashing and fingerprinting functions imported from the bundled
/// external implementation.
///
/// The values returned by Hash, Hash32, and Hash64 are only guaranteed to
/// be the same within the same process. Fingerprint32 and Fingerprint64
/// are fixed algorithms that always give the same result.
namespace farmhash {

/// Hash a byte buffer to a `std::size_t`, `std::size_t Hash(char const*, std::size_t)`.
using external::farmhash::Hash;

/// Hash a byte buffer to a 32-bit value, `uint32_t Hash32(char const*, std::size_t)`.
using external::farmhash::Hash32;

/// Hash a byte buffer to a 64-bit value, `uint64_t Hash64(char const*, std::size_t)`.
using external::farmhash::Hash64;

/// Fingerprint a byte buffer to a stable 32-bit value, `uint32_t Fingerprint32(char const*, std::size_t)`.
using external::farmhash::Fingerprint32;

/// Fingerprint a byte buffer to a stable 64-bit value, `uint64_t Fingerprint64(char const*, std::size_t)`.
using external::farmhash::Fingerprint64;

} // namespace farmhash
} // namespace hash
} // namespace folly
