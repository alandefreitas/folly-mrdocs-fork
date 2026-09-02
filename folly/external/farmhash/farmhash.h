// Copyright (c) 2014 Google, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// FarmHash, by Geoff Pike

//
// http://code.google.com/p/farmhash/
//
// This file provides a few functions for hashing strings and other
// data.  All of them are high-quality functions in the sense that
// they do well on standard tests such as Austin Appleby's SMHasher.
// They're also fast.  FarmHash is the successor to CityHash.
//
// Functions in the FarmHash family are not suitable for cryptography.
//
// WARNING: This code has been only lightly tested on big-endian platforms!
// It is known to work well on little-endian platforms that have a small penalty
// for unaligned reads, such as current Intel and AMD moderate-to-high-end CPUs.
// It should work on all 32-bit and 64-bit platforms that allow unaligned reads;
// bug reports are welcome.
//
// By the way, for some hash functions, given strings a and b, the hash
// of a+b is easily derived from the hashes of a and b.  This property
// doesn't hold for any hash functions in this file.

// clang-format off

#pragma once

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>   // for memcpy and memset
#include <utility>

#include <folly/portability/Config.h>

/// Folly library namespace.
namespace folly {
/// Vendored third-party code adapted for use in Folly.
namespace external {
/// FarmHash family of hash and fingerprint functions.
namespace farmhash {

#if FOLLY_HAVE_INT128_T
/// Unsigned 128-bit integer type used for 128-bit hashes.
using uint128_t = unsigned __int128;

/// Returns the low 64 bits of a 128-bit value.
///
/// \param x The 128-bit value.
/// \returns The low 64 bits of x.
inline uint64_t Uint128Low64(const uint128_t x) {
  return static_cast<uint64_t>(x);
}
/// Returns the high 64 bits of a 128-bit value.
///
/// \param x The 128-bit value.
/// \returns The high 64 bits of x.
inline uint64_t Uint128High64(const uint128_t x) {
  return static_cast<uint64_t>(x >> 64);
}
/// Builds a 128-bit value from its low and high 64-bit halves.
///
/// \param lo The low 64 bits.
/// \param hi The high 64 bits.
/// \returns The 128-bit value formed from lo and hi.
inline uint128_t Uint128(uint64_t lo, uint64_t hi) {
  return lo + (((uint128_t)hi) << 64);
}
#else
typedef std::pair<uint64_t, uint64_t> uint128_t;
inline uint64_t Uint128Low64(const uint128_t x) { return x.first; }
inline uint64_t Uint128High64(const uint128_t x) { return x.second; }
inline uint128_t Uint128(uint64_t lo, uint64_t hi) { return uint128_t(lo, hi); }
#endif


// BASIC STRING HASHING

/// Hashes a byte array to a size_t value.
///
/// May change from time to time, may differ on different platforms, may differ
/// depending on NDEBUG.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \returns The hash of the array.
size_t Hash(const char* s, size_t len);

/// Hashes a byte array to a 32-bit value.
///
/// Most useful in 32-bit binaries. May change from time to time, may differ on
/// different platforms, may differ depending on NDEBUG.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \returns The 32-bit hash of the array.
uint32_t Hash32(const char* s, size_t len);

/// Hashes a byte array to a 32-bit value, mixing in a 32-bit seed.
///
/// May change from time to time, may differ on different platforms, may differ
/// depending on NDEBUG.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed 32-bit seed mixed into the result.
/// \returns The 32-bit hash of the array.
uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed);

/// Hashes a byte array to a 64-bit value.
///
/// May change from time to time, may differ on different platforms, may differ
/// depending on NDEBUG.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \returns The 64-bit hash of the array.
uint64_t Hash64(const char* s, size_t len);

/// Hashes a byte array to a 64-bit value, mixing in a 64-bit seed.
///
/// May change from time to time, may differ on different platforms, may differ
/// depending on NDEBUG.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed 64-bit seed mixed into the result.
/// \returns The 64-bit hash of the array.
uint64_t Hash64WithSeed(const char* s, size_t len, uint64_t seed);

/// Hashes a byte array to a 64-bit value, mixing in two 64-bit seeds.
///
/// May change from time to time, may differ on different platforms, may differ
/// depending on NDEBUG.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed0 First 64-bit seed mixed into the result.
/// \param seed1 Second 64-bit seed mixed into the result.
/// \returns The 64-bit hash of the array.
uint64_t Hash64WithSeeds(const char* s, size_t len,
                       uint64_t seed0, uint64_t seed1);

/// Hashes a byte array to a 128-bit value.
///
/// May change from time to time, may differ on different platforms, may differ
/// depending on NDEBUG.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \returns The 128-bit hash of the array.
uint128_t Hash128(const char* s, size_t len);

/// Hashes a byte array to a 128-bit value, mixing in a 128-bit seed.
///
/// May change from time to time, may differ on different platforms, may differ
/// depending on NDEBUG.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed 128-bit seed mixed into the result.
/// \returns The 128-bit hash of the array.
uint128_t Hash128WithSeed(const char* s, size_t len, uint128_t seed);

// BASIC NON-STRING HASHING

/// Hashes 128 input bits down to 64 bits of output.
///
/// This is intended to be a reasonably good hash function. May change from time
/// to time, may differ on different platforms, may differ depending on NDEBUG.
///
/// \param x The 128-bit value to hash.
/// \returns The 64-bit hash of x.
inline uint64_t Hash128to64(uint128_t x) {
  // Murmur-inspired hashing.
  const uint64_t kMul = 0x9ddfea08eb382d69ULL;
  uint64_t a = (Uint128Low64(x) ^ Uint128High64(x)) * kMul;
  a ^= (a >> 47);
  uint64_t b = (Uint128High64(x) ^ a) * kMul;
  b ^= (b >> 47);
  b *= kMul;
  return b;
}

// FINGERPRINTING (i.e., good, portable, forever-fixed hash functions)

/// Fingerprints a byte array to a 32-bit value.
///
/// Most useful in 32-bit binaries.
///
/// \param s Pointer to the byte array to fingerprint.
/// \param len Length of the array, in bytes.
/// \returns The 32-bit fingerprint of the array.
uint32_t Fingerprint32(const char* s, size_t len);

/// Fingerprints a byte array to a 64-bit value.
///
/// \param s Pointer to the byte array to fingerprint.
/// \param len Length of the array, in bytes.
/// \returns The 64-bit fingerprint of the array.
uint64_t Fingerprint64(const char* s, size_t len);

/// Fingerprints a byte array to a 128-bit value.
///
/// \param s Pointer to the byte array to fingerprint.
/// \param len Length of the array, in bytes.
/// \returns The 128-bit fingerprint of the array.
uint128_t Fingerprint128(const char* s, size_t len);

/// Fingerprints a 128-bit value to a 64-bit value.
///
/// This is intended to be a good fingerprinting primitive.
///
/// \param x The 128-bit value to fingerprint.
/// \returns The 64-bit fingerprint of x.
inline uint64_t Fingerprint(uint128_t x) {
  // Murmur-inspired hashing.
  const uint64_t kMul = 0x9ddfea08eb382d69ULL;
  uint64_t a = (Uint128Low64(x) ^ Uint128High64(x)) * kMul;
  a ^= (a >> 47);
  uint64_t b = (Uint128High64(x) ^ a) * kMul;
  b ^= (b >> 44);
  b *= kMul;
  b ^= (b >> 41);
  b *= kMul;
  return b;
}

/// Fingerprints a 64-bit value to a 64-bit value.
///
/// This is intended to be a good fingerprinting primitive.
///
/// \param x The 64-bit value to fingerprint.
/// \returns The 64-bit fingerprint of x.
inline uint64_t Fingerprint(uint64_t x) {
  // Murmur-inspired hashing.
  const uint64_t kMul = 0x9ddfea08eb382d69ULL;
  uint64_t b = x * kMul;
  b ^= (b >> 44);
  b *= kMul;
  b ^= (b >> 41);
  b *= kMul;
  return b;
}

// Convenience functions to hash or fingerprint C++ strings.
// These require that Str::data() return a pointer to the first char
// (as a const char*) and that Str::length() return the string's length;
// they work with std::string, for example.

/// Hashes a string's bytes to a size_t value.
///
/// May change from time to time, may differ on different platforms, may differ
/// depending on NDEBUG.
///
/// \tparam Str String type whose data() and length() give the bytes to hash.
/// \param s String to hash.
/// \returns The hash of the string.
template <typename Str>
inline size_t Hash(const Str& s) {
  assert(sizeof(s[0]) == 1);
  return Hash(s.data(), s.length());
}

/// Hashes a string's bytes to a 32-bit value.
///
/// Most useful in 32-bit binaries. May change from time to time, may differ on
/// different platforms, may differ depending on NDEBUG.
///
/// \tparam Str String type whose data() and length() give the bytes to hash.
/// \param s String to hash.
/// \returns The 32-bit hash of the string.
template <typename Str>
inline uint32_t Hash32(const Str& s) {
  assert(sizeof(s[0]) == 1);
  return Hash32(s.data(), s.length());
}

/// Hashes a string's bytes to a 32-bit value, mixing in a 32-bit seed.
///
/// May change from time to time, may differ on different platforms, may differ
/// depending on NDEBUG.
///
/// \tparam Str String type whose data() and length() give the bytes to hash.
/// \param s String to hash.
/// \param seed 32-bit seed mixed into the result.
/// \returns The 32-bit hash of the string.
template <typename Str>
inline uint32_t Hash32WithSeed(const Str& s, uint32_t seed) {
  assert(sizeof(s[0]) == 1);
  return Hash32WithSeed(s.data(), s.length(), seed);
}

/// Hashes a string's bytes to a 64-bit value.
///
/// May change from time to time, may differ on different platforms, may differ
/// depending on NDEBUG.
///
/// \tparam Str String type whose data() and length() give the bytes to hash.
/// \param s String to hash.
/// \returns The 64-bit hash of the string.
template <typename Str>
inline uint64_t Hash64(const Str& s) {
  assert(sizeof(s[0]) == 1);
  return Hash64(s.data(), s.length());
}

/// Hashes a string's bytes to a 64-bit value, mixing in a 64-bit seed.
///
/// May change from time to time, may differ on different platforms, may differ
/// depending on NDEBUG.
///
/// \tparam Str String type whose data() and length() give the bytes to hash.
/// \param s String to hash.
/// \param seed 64-bit seed mixed into the result.
/// \returns The 64-bit hash of the string.
template <typename Str>
inline uint64_t Hash64WithSeed(const Str& s, uint64_t seed) {
  assert(sizeof(s[0]) == 1);
  return Hash64WithSeed(s.data(), s.length(), seed);
}

/// Hashes a string's bytes to a 64-bit value, mixing in two 64-bit seeds.
///
/// May change from time to time, may differ on different platforms, may differ
/// depending on NDEBUG.
///
/// \tparam Str String type whose data() and length() give the bytes to hash.
/// \param s String to hash.
/// \param seed0 First 64-bit seed mixed into the result.
/// \param seed1 Second 64-bit seed mixed into the result.
/// \returns The 64-bit hash of the string.
template <typename Str>
inline uint64_t Hash64WithSeeds(const Str& s, uint64_t seed0, uint64_t seed1) {
  assert(sizeof(s[0]) == 1);
  return Hash64WithSeeds(s.data(), s.length(), seed0, seed1);
}

/// Hashes a string's bytes to a 128-bit value.
///
/// May change from time to time, may differ on different platforms, may differ
/// depending on NDEBUG.
///
/// \tparam Str String type whose data() and length() give the bytes to hash.
/// \param s String to hash.
/// \returns The 128-bit hash of the string.
template <typename Str>
inline uint128_t Hash128(const Str& s) {
  assert(sizeof(s[0]) == 1);
  return Hash128(s.data(), s.length());
}

/// Hashes a string's bytes to a 128-bit value, mixing in a 128-bit seed.
///
/// May change from time to time, may differ on different platforms, may differ
/// depending on NDEBUG.
///
/// \tparam Str String type whose data() and length() give the bytes to hash.
/// \param s String to hash.
/// \param seed 128-bit seed mixed into the result.
/// \returns The 128-bit hash of the string.
template <typename Str>
inline uint128_t Hash128WithSeed(const Str& s, uint128_t seed) {
  assert(sizeof(s[0]) == 1);
  return Hash128(s.data(), s.length(), seed);
}

// FINGERPRINTING (i.e., good, portable, forever-fixed hash functions)

/// Fingerprints a string's bytes to a 32-bit value.
///
/// Most useful in 32-bit binaries.
///
/// \tparam Str String type whose data() and length() give the bytes to fingerprint.
/// \param s String to fingerprint.
/// \returns The 32-bit fingerprint of the string.
template <typename Str>
inline uint32_t Fingerprint32(const Str& s) {
  assert(sizeof(s[0]) == 1);
  return Fingerprint32(s.data(), s.length());
}

/// Fingerprints a string's bytes to a 64-bit value.
///
/// \tparam Str String type whose data() and length() give the bytes to fingerprint.
/// \param s String to fingerprint.
/// \returns The 64-bit fingerprint of the string.
template <typename Str>
inline uint64_t Fingerprint64(const Str& s) {
  assert(sizeof(s[0]) == 1);
  return Fingerprint64(s.data(), s.length());
}

/// Fingerprints a string's bytes to a 128-bit value.
///
/// \tparam Str String type whose data() and length() give the bytes to fingerprint.
/// \param s String to fingerprint.
/// \returns The 128-bit fingerprint of the string.
template <typename Str>
inline uint128_t Fingerprint128(const Str& s) {
  assert(sizeof(s[0]) == 1);
  return Fingerprint128(s.data(), s.length());
}

//// internal variants

namespace test {
/// When set, hash functions return zero if the build is misconfigured.
extern bool returnZeroIfMisconfigured;
}

/// FarmHash "na" variant.
namespace farmhashna {
/// Hashes a byte array to a 64-bit value.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \returns The 64-bit hash of the array.
uint64_t Hash64(const char* s, size_t len);
/// Hashes a byte array to a 64-bit value, mixing in a 64-bit seed.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed 64-bit seed mixed into the result.
/// \returns The 64-bit hash of the array.
uint64_t Hash64WithSeed(const char* s, size_t len, uint64_t seed);
/// Hashes a byte array to a 64-bit value, mixing in two 64-bit seeds.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed0 First 64-bit seed mixed into the result.
/// \param seed1 Second 64-bit seed mixed into the result.
/// \returns The 64-bit hash of the array.
uint64_t
Hash64WithSeeds(const char* s, size_t len, uint64_t seed0, uint64_t seed1);
} // namespace farmhashna
/// FarmHash "uo" variant.
namespace farmhashuo {
/// Hashes a byte array to a 64-bit value, mixing in a 64-bit seed.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed 64-bit seed mixed into the result.
/// \returns The 64-bit hash of the array.
uint64_t Hash64WithSeed(const char* s, size_t len, uint64_t seed);
/// Hashes a byte array to a 64-bit value.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \returns The 64-bit hash of the array.
uint64_t Hash64(const char* s, size_t len);
} // namespace farmhashuo
/// FarmHash "xo" variant.
namespace farmhashxo {
/// Hashes a byte array to a 64-bit value.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \returns The 64-bit hash of the array.
uint64_t Hash64(const char* s, size_t len);
/// Hashes a byte array to a 64-bit value, mixing in two 64-bit seeds.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed0 First 64-bit seed mixed into the result.
/// \param seed1 Second 64-bit seed mixed into the result.
/// \returns The 64-bit hash of the array.
uint64_t
Hash64WithSeeds(const char* s, size_t len, uint64_t seed0, uint64_t seed1);
/// Hashes a byte array to a 64-bit value, mixing in a 64-bit seed.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed 64-bit seed mixed into the result.
/// \returns The 64-bit hash of the array.
uint64_t Hash64WithSeed(const char* s, size_t len, uint64_t seed);
} // namespace farmhashxo
/// FarmHash "te" variant.
namespace farmhashte {
/// Hashes a byte array to a 64-bit value.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \returns The 64-bit hash of the array.
uint64_t Hash64(const char* s, size_t len);
/// Hashes a byte array to a 64-bit value, mixing in a 64-bit seed.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed 64-bit seed mixed into the result.
/// \returns The 64-bit hash of the array.
uint64_t Hash64WithSeed(const char* s, size_t len, uint64_t seed);
uint64_t Hash64(const char* s, size_t len);
uint64_t Hash64WithSeed(const char* s, size_t len, uint64_t seed);
/// Hashes a byte array to a 64-bit value, mixing in two 64-bit seeds.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed0 First 64-bit seed mixed into the result.
/// \param seed1 Second 64-bit seed mixed into the result.
/// \returns The 64-bit hash of the array.
uint64_t
Hash64WithSeeds(const char* s, size_t len, uint64_t seed0, uint64_t seed1);
} // namespace farmhashte
/// FarmHash "nt" variant.
namespace farmhashnt {
/// Hashes a byte array to a 32-bit value.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \returns The 32-bit hash of the array.
uint32_t Hash32(const char* s, size_t len);
/// Hashes a byte array to a 32-bit value, mixing in a 32-bit seed.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed 32-bit seed mixed into the result.
/// \returns The 32-bit hash of the array.
uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed);
uint32_t Hash32(const char* s, size_t len);
uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed);
} // namespace farmhashnt
/// FarmHash "mk" variant.
namespace farmhashmk {
/// Hashes a byte array to a 32-bit value.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \returns The 32-bit hash of the array.
uint32_t Hash32(const char* s, size_t len);
/// Hashes a byte array to a 32-bit value, mixing in a 32-bit seed.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed 32-bit seed mixed into the result.
/// \returns The 32-bit hash of the array.
uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed);
} // namespace farmhashmk
/// FarmHash "su" variant.
namespace farmhashsu {
/// Hashes a byte array to a 32-bit value.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \returns The 32-bit hash of the array.
uint32_t Hash32(const char* s, size_t len);
/// Hashes a byte array to a 32-bit value, mixing in a 32-bit seed.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed 32-bit seed mixed into the result.
/// \returns The 32-bit hash of the array.
uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed);
uint32_t Hash32(const char* s, size_t len);
uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed);
} // namespace farmhashsu
/// FarmHash "sa" variant.
namespace farmhashsa {
/// Hashes a byte array to a 32-bit value.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \returns The 32-bit hash of the array.
uint32_t Hash32(const char* s, size_t len);
/// Hashes a byte array to a 32-bit value, mixing in a 32-bit seed.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed 32-bit seed mixed into the result.
/// \returns The 32-bit hash of the array.
uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed);
uint32_t Hash32(const char* s, size_t len);
uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed);
} // namespace farmhashsa
/// FarmHash "cc" variant, derived from CityHash.
namespace farmhashcc {
/// Hashes a byte array to a 32-bit value.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \returns The 32-bit hash of the array.
uint32_t Hash32(const char* s, size_t len);
/// Hashes a byte array to a 32-bit value, mixing in a 32-bit seed.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed 32-bit seed mixed into the result.
/// \returns The 32-bit hash of the array.
uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed);
/// Hashes a byte array to a 128-bit value using CityHash, mixing in a 128-bit seed.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed 128-bit seed mixed into the result.
/// \returns The 128-bit hash of the array.
uint128_t CityHash128WithSeed(const char* s, size_t len, uint128_t seed);
/// Fingerprints a byte array to a 128-bit value.
///
/// \param s Pointer to the byte array to fingerprint.
/// \param len Length of the array, in bytes.
/// \returns The 128-bit fingerprint of the array.
uint128_t Fingerprint128(const char* s, size_t len);
uint32_t Hash32(const char* s, size_t len);
uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed);
/// Hashes a byte array to a 64-bit value.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \returns The 64-bit hash of the array.
uint64_t Hash64(const char* s, size_t len);
/// Hashes a byte array to a size_t value.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \returns The hash of the array.
size_t Hash(const char* s, size_t len);
/// Hashes a byte array to a 64-bit value, mixing in a 64-bit seed.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed 64-bit seed mixed into the result.
/// \returns The 64-bit hash of the array.
uint64_t Hash64WithSeed(const char* s, size_t len, uint64_t seed);
/// Hashes a byte array to a 64-bit value, mixing in two 64-bit seeds.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed0 First 64-bit seed mixed into the result.
/// \param seed1 Second 64-bit seed mixed into the result.
/// \returns The 64-bit hash of the array.
uint64_t
Hash64WithSeeds(const char* s, size_t len, uint64_t seed0, uint64_t seed1);
/// Hashes a byte array to a 128-bit value.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \returns The 128-bit hash of the array.
uint128_t Hash128(const char* s, size_t len);
/// Hashes a byte array to a 128-bit value, mixing in a 128-bit seed.
///
/// \param s Pointer to the byte array to hash.
/// \param len Length of the array, in bytes.
/// \param seed 128-bit seed mixed into the result.
/// \returns The 128-bit hash of the array.
uint128_t Hash128WithSeed(const char* s, size_t len, uint128_t seed);
/// Fingerprints a byte array to a 32-bit value.
///
/// \param s Pointer to the byte array to fingerprint.
/// \param len Length of the array, in bytes.
/// \returns The 32-bit fingerprint of the array.
uint32_t Fingerprint32(const char* s, size_t len);
/// Fingerprints a byte array to a 64-bit value.
///
/// \param s Pointer to the byte array to fingerprint.
/// \param len Length of the array, in bytes.
/// \returns The 64-bit fingerprint of the array.
uint64_t Fingerprint64(const char* s, size_t len);
uint128_t Fingerprint128(const char* s, size_t len);
} // namespace farmhashcc

} // namespace farmhash
} // namespace external
} // namespace folly
