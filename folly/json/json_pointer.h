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

#include <string>
#include <vector>

#include <folly/CppAttributes.h>
#include <folly/Expected.h>
#include <folly/Range.h>

/// Facebook's open-source library of C++ components.
namespace folly {

/**
 * A parsed JSON Pointer, as described in RFC 6901 "JSON Pointer".
 *
 * Implements parsing. Traversal using the pointer over data structures must be
 * implemented separately.
 */
class json_pointer {
 public:
  /// Reasons a JSON Pointer string may fail to parse.
  enum class parse_error {
    invalid_first_character, ///< The pointer does not start with '/'.
    invalid_escape_sequence, ///< An escape sequence is malformed.
  };

  /// Thrown by parse() when the input string is not a valid JSON Pointer.
  class parse_exception : public std::runtime_error {
    using std::runtime_error::runtime_error;
  };

  /// Constructs an empty pointer that refers to the whole document.
  json_pointer() = default;
  /// Destroys the pointer.
  ~json_pointer() = default;

  /**
   * Parse string into vector of unescaped tokens. Non-throwing version.
   *
   * \param str The JSON Pointer string to parse.
   * \returns The parsed pointer, or a parse_error on failure.
   */
  static Expected<json_pointer, parse_error> try_parse(StringPiece const str);

  /**
   * Parse string into vector of unescaped tokens. Throwing version.
   *
   * \param str The JSON Pointer string to parse.
   * \returns The parsed pointer.
   */
  static json_pointer parse(StringPiece const str);

  /**
   * Return true if this pointer is proper to prefix to another pointer.
   *
   * \param other The pointer to test against.
   * \returns True if this pointer is a proper prefix of other.
   */
  bool is_prefix_of(json_pointer const& other) const noexcept;

  /**
   * Get access to the parsed tokens for applications that want to traverse
   * the pointer.
   *
   * \returns The unescaped tokens of this pointer.
   */
  std::vector<std::string> const& tokens() const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]];

  /**
   * Compare two pointers for equality.
   *
   * \param lhs The left-hand pointer.
   * \param rhs The right-hand pointer.
   * \returns True if both pointers have identical tokens.
   */
  friend bool operator==(json_pointer const& lhs, json_pointer const& rhs) {
    return lhs.tokens_ == rhs.tokens_;
  }

  /**
   * Compare two pointers for inequality.
   *
   * \param lhs The left-hand pointer.
   * \param rhs The right-hand pointer.
   * \returns True if the pointers differ in their tokens.
   */
  friend bool operator!=(json_pointer const& lhs, json_pointer const& rhs) {
    return lhs.tokens_ != rhs.tokens_;
  }

 private:
  explicit json_pointer(std::vector<std::string>) noexcept;

  /*
   * Unescape the specified escape sequences, returns false if incorrect
   */
  static bool unescape(std::string&);

  std::vector<std::string> tokens_;
};

} // namespace folly
