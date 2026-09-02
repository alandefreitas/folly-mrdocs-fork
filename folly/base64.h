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

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <folly/CPortability.h>
#include <folly/Portability.h>
#include <folly/detail/base64_detail/Base64Api.h>
#include <folly/detail/base64_detail/Base64Common.h>
#include <folly/lang/Exception.h>
#include <folly/memory/UninitializedMemoryHacks.h>

namespace folly {

//
// base64 encoding/decoding
//
// There are a few variations of base64 encoding.
//
// We have 2: base64 and base64URL.
//
// base64 uses '+' '/' for encoding 62 and 63 and uses '=' padding symbol.
// (padding symbols are required on decoding)
//
// base64URL uses '-' '_' for encoding 62 and 63 and has no padding.
// Decoding with base64URL will accept both base64 and base64URL encoded data +
// padding is always optional.
//
// SIMD implementation is based on 0x80 blog.
// See details explained in folly/detail/base64_detail/README.md
//

//
// High level API.
// Encoding never fails, except for allocation.
// Decoding will throw base64_decode_error if it fails.
//
// NOTE: the expection does not contain detailed information
//       about the error because keeping track of that is overhead.
//       We can potentially improve error reporting by doing a second
//       pass if we decide that it's benefitial.

/// Exception thrown when base64 decoding fails.
struct base64_decode_error;

/// Encode a string to standard base64.
///
/// @param s The bytes to encode.
/// @return The base64-encoded string.
inline auto base64Encode(std::string_view s) -> std::string;

/// Decode a standard base64 string.
///
/// @param s The base64-encoded bytes to decode.
/// @return The decoded string.
/// @throws base64_decode_error if the input is not valid base64.
inline auto base64Decode(std::string_view s) -> std::string;

/// Encode a string to URL-safe base64.
///
/// @param s The bytes to encode.
/// @return The base64URL-encoded string.
inline auto base64URLEncode(std::string_view s) -> std::string;

/// Decode a URL-safe base64 string.
///
/// @param s The base64URL-encoded bytes to decode.
/// @return The decoded string.
/// @throws base64_decode_error if the input is not valid base64URL.
inline auto base64URLDecode(std::string_view s) -> std::string;

/// Decode a base64 string using PHP-strict rules.
///
/// @param s The base64-encoded bytes to decode.
/// @return The decoded string.
/// @throws base64_decode_error if the input is not valid base64.
inline auto base64PHPStrictDecode(std::string_view s) -> std::string;

// Low level API.
//
// This API does not throw and is constexpr enabled.
//
// Encode returns a pointer past the last the byte written
// Decode returns a struct with `is_success` flag and the pointer `o`
// past the last char written.
//
// NOTE: decode will not stop writing when encountering a failure
//       and can always write up to size.
//
// NOTE: since on C++17 we cannot always adequately determine if
//       the function is running in compile time or not,
//       we provide explicit runime versions too.

/// Compute the encoded size for standard base64.
///
/// @param inSize The number of input bytes to encode.
/// @return The number of bytes the base64 encoding will occupy.
constexpr std::size_t base64EncodedSize(std::size_t inSize) noexcept;

/// Compute the encoded size for URL-safe base64.
///
/// @param inSize The number of input bytes to encode.
/// @return The number of bytes the base64URL encoding will occupy.
constexpr std::size_t base64URLEncodedSize(std::size_t inSize) noexcept;

/// Encode a byte range to standard base64.
///
/// @param f Pointer to the first input byte.
/// @param l Pointer past the last input byte.
/// @param o Pointer to the output buffer.
/// @return A pointer past the last byte written.
inline constexpr char* base64Encode(
    const char* f, const char* l, char* o) noexcept;

/// Encode a byte range to URL-safe base64.
///
/// @param f Pointer to the first input byte.
/// @param l Pointer past the last input byte.
/// @param o Pointer to the output buffer.
/// @return A pointer past the last byte written.
inline constexpr char* base64URLEncode(
    const char* f, const char* l, char* o) noexcept;

/// Encode a byte range to standard base64 at runtime.
///
/// @param f Pointer to the first input byte.
/// @param l Pointer past the last input byte.
/// @param o Pointer to the output buffer.
/// @return A pointer past the last byte written.
inline char* base64EncodeRuntime(
    const char* f, const char* l, char* o) noexcept;

/// Encode a byte range to URL-safe base64 at runtime.
///
/// @param f Pointer to the first input byte.
/// @param l Pointer past the last input byte.
/// @param o Pointer to the output buffer.
/// @return A pointer past the last byte written.
inline char* base64URLEncodeRuntime(
    const char* f, const char* l, char* o) noexcept;

/// Compute the decoded size for a standard base64 byte range.
///
/// @param f Pointer to the first input byte.
/// @param l Pointer past the last input byte.
/// @return The number of bytes the decoded output will occupy.
constexpr std::size_t base64DecodedSize(const char* f, const char* l) noexcept;

/// Compute the decoded size for a standard base64 string.
///
/// @param s The base64-encoded input.
/// @return The number of bytes the decoded output will occupy.
constexpr std::size_t base64DecodedSize(std::string_view s) noexcept;

/// Compute the decoded size for a URL-safe base64 byte range.
///
/// @param f Pointer to the first input byte.
/// @param l Pointer past the last input byte.
/// @return The number of bytes the decoded output will occupy.
constexpr std::size_t base64URLDecodedSize(
    const char* f, const char* l) noexcept;

/// Compute the decoded size for a URL-safe base64 string.
///
/// @param s The base64URL-encoded input.
/// @return The number of bytes the decoded output will occupy.
constexpr std::size_t base64URLDecodedSize(std::string_view s) noexcept;

/// Compute the output buffer size required by PHP-strict decoding of a range.
///
/// @param f Pointer to the first input byte.
/// @param l Pointer past the last input byte.
/// @return The number of bytes the decoded output may occupy.
constexpr std::size_t base64PHPStrictDecodeRequiredOutputSize(
    const char* f, const char* l) noexcept;

/// Compute the output buffer size required by PHP-strict decoding of a string.
///
/// @param s The base64-encoded input.
/// @return The number of bytes the decoded output may occupy.
constexpr std::size_t base64PHPStrictDecodeRequiredOutputSize(
    std::string_view s) noexcept;

/// Result of a low-level base64 decode operation.
struct base64_decode_result {
  bool is_success; ///< True if decoding succeeded.
  char* o; ///< Pointer past the last byte written.
};

/// Decode a standard base64 byte range into an output buffer.
///
/// @param f Pointer to the first input byte.
/// @param l Pointer past the last input byte.
/// @param o Pointer to the output buffer.
/// @return The decode result with success flag and output end pointer.
inline constexpr base64_decode_result base64Decode(
    const char* f, const char* l, char* o) noexcept;

/// Decode a standard base64 string into an output buffer.
///
/// @param s The base64-encoded input.
/// @param o Pointer to the output buffer.
/// @return The decode result with success flag and output end pointer.
inline constexpr base64_decode_result base64Decode(
    std::string_view s, char* o) noexcept;

/// Decode a URL-safe base64 byte range into an output buffer.
///
/// @param f Pointer to the first input byte.
/// @param l Pointer past the last input byte.
/// @param o Pointer to the output buffer.
/// @return The decode result with success flag and output end pointer.
inline constexpr base64_decode_result base64URLDecode(
    const char* f, const char* l, char* o) noexcept;

/// Decode a URL-safe base64 string into an output buffer.
///
/// @param s The base64URL-encoded input.
/// @param o Pointer to the output buffer.
/// @return The decode result with success flag and output end pointer.
inline constexpr base64_decode_result base64URLDecode(
    std::string_view s, char* o) noexcept;

/// Decode a standard base64 byte range into an output buffer at runtime.
///
/// @param f Pointer to the first input byte.
/// @param l Pointer past the last input byte.
/// @param o Pointer to the output buffer.
/// @return The decode result with success flag and output end pointer.
inline base64_decode_result base64DecodeRuntime(
    const char* f, const char* l, char* o) noexcept;

/// Decode a standard base64 string into an output buffer at runtime.
///
/// @param s The base64-encoded input.
/// @param o Pointer to the output buffer.
/// @return The decode result with success flag and output end pointer.
inline base64_decode_result base64DecodeRuntime(
    std::string_view s, char* o) noexcept;

/// Decode a URL-safe base64 byte range into an output buffer at runtime.
///
/// @param f Pointer to the first input byte.
/// @param l Pointer past the last input byte.
/// @param o Pointer to the output buffer.
/// @return The decode result with success flag and output end pointer.
inline base64_decode_result base64URLDecodeRuntime(
    const char* f, const char* l, char* o) noexcept;

/// Decode a URL-safe base64 string into an output buffer at runtime.
///
/// @param s The base64URL-encoded input.
/// @param o Pointer to the output buffer.
/// @return The decode result with success flag and output end pointer.
inline base64_decode_result base64URLDecodeRuntime(
    std::string_view s, char* o) noexcept;

/// Decode a base64 byte range using PHP-strict rules into an output buffer.
///
/// @param f Pointer to the first input byte.
/// @param l Pointer past the last input byte.
/// @param o Pointer to the output buffer.
/// @return The decode result with success flag and output end pointer.
inline constexpr base64_decode_result base64PHPStrictDecode(
    const char* f, const char* l, char* o) noexcept;

/// Decode a base64 string using PHP-strict rules into an output buffer.
///
/// @param s The base64-encoded input.
/// @param o Pointer to the output buffer.
/// @return The decode result with success flag and output end pointer.
inline constexpr base64_decode_result base64PHPStrictDecode(
    std::string_view s, char* o) noexcept;

// -----------------------------------------------------------------
// implementation

/// Exception thrown when base64 decoding fails.
struct base64_decode_error : std::runtime_error {
  /// Inherit the constructors of std::runtime_error.
  using std::runtime_error::runtime_error;
};

constexpr std::size_t base64EncodedSize(std::size_t inSize) noexcept {
  return detail::base64_detail::base64EncodedSize(inSize);
}

constexpr std::size_t base64URLEncodedSize(std::size_t inSize) noexcept {
  return detail::base64_detail::base64URLEncodedSize(inSize);
}

inline constexpr char* base64Encode(
    const char* f, const char* l, char* o) noexcept {
  return detail::base64_detail::base64Encode(f, l, o);
}

inline constexpr char* base64URLEncode(
    const char* f, const char* l, char* o) noexcept {
  return detail::base64_detail::base64URLEncode(f, l, o);
}

inline char* base64EncodeRuntime(
    const char* f, const char* l, char* o) noexcept {
  return detail::base64_detail::base64EncodeRuntime(f, l, o);
}

inline char* base64URLEncodeRuntime(
    const char* f, const char* l, char* o) noexcept {
  return detail::base64_detail::base64URLEncodeRuntime(f, l, o);
}

inline std::string base64Encode(std::string_view s) {
  std::string res;
  std::size_t resSize = folly::base64EncodedSize(s.size());
  folly::resizeWithoutInitialization(res, resSize);
  folly::base64EncodeRuntime(s.data(), s.data() + s.size(), res.data());
  return res;
}

inline std::string base64URLEncode(std::string_view s) {
  std::string res;
  std::size_t resSize = folly::base64URLEncodedSize(s.size());
  folly::resizeWithoutInitialization(res, resSize);
  folly::base64URLEncodeRuntime(s.data(), s.data() + s.size(), res.data());
  return res;
}

constexpr std::size_t base64DecodedSize(const char* f, const char* l) noexcept {
  return detail::base64_detail::base64DecodedSize(f, l);
}

constexpr std::size_t base64DecodedSize(std::string_view s) noexcept {
  return folly::base64DecodedSize(s.data(), s.data() + s.size());
}

constexpr std::size_t base64URLDecodedSize(
    const char* f, const char* l) noexcept {
  return detail::base64_detail::base64URLDecodedSize(f, l);
}

constexpr std::size_t base64URLDecodedSize(std::string_view s) noexcept {
  return folly::base64URLDecodedSize(s.data(), s.data() + s.size());
}

constexpr std::size_t base64PHPStrictDecodeRequiredOutputSize(
    const char* f, const char* l) noexcept {
  return detail::base64_detail::base64PHPStrictDecodeRequiredOutputSize(f, l);
}

constexpr std::size_t base64PHPStrictDecodeRequiredOutputSize(
    std::string_view s) noexcept {
  return folly::base64PHPStrictDecodeRequiredOutputSize(
      s.data(), s.data() + s.size());
}

inline constexpr base64_decode_result base64Decode(
    const char* f, const char* l, char* o) noexcept {
  auto detailResult = detail::base64_detail::base64Decode(f, l, o);
  return {detailResult.isSuccess, detailResult.o};
}

inline constexpr base64_decode_result base64Decode(
    std::string_view s, char* o) noexcept {
  return folly::base64Decode(s.data(), s.data() + s.size(), o);
}

inline constexpr base64_decode_result base64URLDecode(
    const char* f, const char* l, char* o) noexcept {
  auto detailResult = detail::base64_detail::base64URLDecode(f, l, o);
  return {detailResult.isSuccess, detailResult.o};
}

inline constexpr base64_decode_result base64URLDecode(
    std::string_view s, char* o) noexcept {
  return folly::base64URLDecode(s.data(), s.data() + s.size(), o);
}

inline base64_decode_result base64DecodeRuntime(
    const char* f, const char* l, char* o) noexcept {
  auto detailResult = detail::base64_detail::base64DecodeRuntime(f, l, o);
  return {detailResult.isSuccess, detailResult.o};
}

inline base64_decode_result base64DecodeRuntime(
    std::string_view s, char* o) noexcept {
  return folly::base64DecodeRuntime(s.data(), s.data() + s.size(), o);
}

inline base64_decode_result base64URLDecodeRuntime(
    const char* f, const char* l, char* o) noexcept {
  auto detailResult = detail::base64_detail::base64URLDecodeRuntime(f, l, o);
  return {detailResult.isSuccess, detailResult.o};
}

inline base64_decode_result base64URLDecodeRuntime(
    std::string_view s, char* o) noexcept {
  return folly::base64URLDecodeRuntime(s.data(), s.data() + s.size(), o);
}

inline constexpr base64_decode_result base64PHPStrictDecode(
    const char* f, const char* l, char* o) noexcept {
  auto detailResult = detail::base64_detail::base64PHPStrictDecode(f, l, o);
  return {detailResult.isSuccess, detailResult.o};
}

inline constexpr base64_decode_result base64PHPStrictDecode(
    std::string_view s, char* o) noexcept {
  return folly::base64PHPStrictDecode(s.data(), s.data() + s.size(), o);
}

// NOTE: for resizeWithoutInitialization we don't need to declare the macros,
//       since we are using char which is already included by default.
inline std::string base64Decode(std::string_view s) {
  std::string res;
  std::size_t resSize = folly::base64DecodedSize(s);
  folly::resizeWithoutInitialization(res, resSize);

  if (!folly::base64DecodeRuntime(s, res.data()).is_success) {
    folly::throw_exception<base64_decode_error>("Base64 Decoding failed");
  }
  return res;
}

inline std::string base64URLDecode(std::string_view s) {
  std::string res;
  std::size_t resSize = folly::base64URLDecodedSize(s);
  folly::resizeWithoutInitialization(res, resSize);

  if (!folly::base64URLDecodeRuntime(s, res.data()).is_success) {
    folly::throw_exception<base64_decode_error>("Base64URL Decoding failed");
  }
  return res;
}

inline std::string base64PHPStrictDecode(std::string_view s) {
  std::string res;
  folly::resizeWithoutInitialization(
      res, base64PHPStrictDecodeRequiredOutputSize(s));

  if (s.empty()) {
    return res;
  }

  base64_decode_result decoded = folly::base64PHPStrictDecode(s, res.data());

  if (!decoded.is_success) {
    folly::throw_exception<base64_decode_error>("Base64PHP Decoding failed");
  }

  res.resize(decoded.o - res.data());
  res.shrink_to_fit();
  return res;
}

} // namespace folly
