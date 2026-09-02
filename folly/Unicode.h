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

// Some utility routines relating to Unicode.

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include <folly/lang/Exception.h>

namespace folly {

/// Exception thrown when Unicode decoding or encoding fails.
class FOLLY_EXPORT unicode_error : public std::runtime_error {
 public:
  /// Inherits the constructors of std::runtime_error.
  using std::runtime_error::runtime_error;
};

//  Unicode code points are split into 17 planes.
//
//  The Basic Multilingual Plane covers code points in [0-0xFFFF] but reserves
//  two invalid ranges:
//  - High surrogates: [0xD800-0xDBFF].
//  - Low surrogates: [0xDC00-0xDFFF].
//
//  UTF-16 code units are 2 bytes wide and are represented here with char16_t.
//  Unicode code points are represented in UTF-16 across either 1-2 code units:
//  - Valid BMP code points [0x0000-0xD7FF] + [0xE000-0xFFFF] are encoded
//    directly as 1 code unit.
//  - Code points larger than BMP (>0xFFFF) are encoded as 2 code units, with
//    values respectively in the high surrogates and low surrogates ranges.
//
//  JSON text permits the inclusion of Unicode escape sequences within quoted
//  strings:
//  - Valid BMP code points are encoded as \xXXXX, where XXXX are the base-16
//    digits of the code point.
//  - Code points larger than BMP are encoded as \uHHHH\uLLLL, where HHHH and
//    LLLL are respectively the base-16 digits of the high and low surrogates of
//    the UTF-16 encoding of the code point.

/// Returns whether a UTF-16 code unit lies in the Basic Multilingual Plane.
/// \param c The UTF-16 code unit to test.
/// \returns True when the code unit is a BMP code point.
inline bool utf16_code_unit_is_bmp(char16_t const c) {
  return c < 0xd800 || c >= 0xe000;
}
/// Returns whether a UTF-16 code unit is a high surrogate.
/// \param c The UTF-16 code unit to test.
/// \returns True when the code unit is in the high-surrogate range.
inline bool utf16_code_unit_is_high_surrogate(char16_t const c) {
  return c >= 0xd800 && c < 0xdc00;
}
/// Returns whether a UTF-16 code unit is a low surrogate.
/// \param c The UTF-16 code unit to test.
/// \returns True when the code unit is in the low-surrogate range.
inline bool utf16_code_unit_is_low_surrogate(char16_t const c) {
  return c >= 0xdc00 && c < 0xe000;
}
/// Combines a UTF-16 surrogate pair into a single code point.
/// \param high The high surrogate code unit.
/// \param low The low surrogate code unit.
/// \returns The decoded Unicode code point.
inline char32_t unicode_code_point_from_utf16_surrogate_pair(
    char16_t const high, char16_t const low) {
  if (!utf16_code_unit_is_high_surrogate(high)) {
    throw_exception<unicode_error>("invalid high surrogate");
  }
  if (!utf16_code_unit_is_low_surrogate(low)) {
    throw_exception<unicode_error>("invalid low surrogate");
  }
  return 0x10000 + ((char32_t(high) & 0x3ff) << 10) + (char32_t(low) & 0x3ff);
}

//////////////////////////////////////////////////////////////////////

/// A UTF-8 encoding of a single Unicode code point.
struct unicode_code_point_utf8 {
  uint32_t size; ///< Number of valid bytes in data.
  uint8_t data[4]; ///< The encoded UTF-8 bytes.
};
/// Encodes a single Unicode code point into a UTF-8 byte sequence.
/// \param cp The code point to encode.
/// \returns The UTF-8 encoding of the code point.
unicode_code_point_utf8 unicode_code_point_to_utf8(char32_t cp);

/**
 * Encode a single Unicode code point into a UTF-8 byte sequence.
 *
 * Result is undefined if `cp' is an invalid code point.
 *
 * \param cp The code point to encode.
 * \param out The string that the UTF-8 bytes are appended to.
 */
void appendCodePointToUtf8(char32_t cp, std::string& out);
/// Encodes a single Unicode code point as a UTF-8 string.
/// \param cp The code point to encode.
/// \returns The UTF-8 encoding of the code point.
std::string codePointToUtf8(char32_t cp);

/**
 * Decode a single Unicode code point from UTF-8 byte sequence.
 *
 * \param p Reference to the read cursor; advanced past the decoded bytes.
 * \param e Pointer to the end of the byte sequence.
 * \param skipOnError Whether to skip invalid bytes instead of throwing.
 * \returns The decoded Unicode code point.
 */
char32_t utf8ToCodePoint(
    const unsigned char*& p, const unsigned char* const e, bool skipOnError);

//////////////////////////////////////////////////////////////////////

} // namespace folly
