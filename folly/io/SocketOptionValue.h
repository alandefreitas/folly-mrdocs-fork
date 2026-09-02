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
#include <variant>

namespace folly {

/**
 * Variant container for socket option values: integer or string.
 * Implicit ctor/compares with int for backward compatibility.
 */
class SocketOptionValue {
 public:
  /// Constructs a value holding an integer.
  ///
  /// \param val The integer to store.
  /* implicit */ SocketOptionValue(int val) : val_(val) {}
  /// Constructs a value holding a string.
  ///
  /// \param val The string to store.
  /* implicit */ SocketOptionValue(const std::string& val) : val_(val) {}
  /// Constructs a value holding the integer zero.
  SocketOptionValue() : val_(0) {}

  /// Returns true if the container holds an int.
  ///
  /// \returns True when the value currently holds an int.
  bool hasInt() const;
  /// Returns the int value; requires hasInt() to be true.
  ///
  /// \returns The stored integer.
  int asInt() const;

  /// Returns true if the container holds a string.
  ///
  /// \returns True when the value currently holds a string.
  bool hasString() const;
  /// Returns the string value; requires hasString() to be true.
  ///
  /// \returns A reference to the stored string.
  const std::string& asString() const;

  /// Returns the held string, or the integer converted to a string.
  ///
  /// \returns The string form of the stored value.
  std::string toString() const;

  /// Compares two values for equality.
  ///
  /// \param lhs The left-hand value.
  /// \param rhs The right-hand value.
  /// \returns True when both hold the same alternative and value.
  friend bool operator==(
      const SocketOptionValue& lhs, const SocketOptionValue& rhs);
  /// Compares the value against an int.
  ///
  /// \param lhs The value to compare.
  /// \param rhs The integer to compare against.
  /// \returns True when the value holds an int equal to `rhs`.
  friend bool operator==(const SocketOptionValue& lhs, int rhs);
  /// Compares the value against a string.
  ///
  /// \param lhs The value to compare.
  /// \param rhs The string to compare against.
  /// \returns True when the value holds a string equal to `rhs`.
  friend bool operator==(const SocketOptionValue& lhs, const std::string& rhs);

  /// Compares two values for inequality.
  ///
  /// \param lhs The left-hand value.
  /// \param rhs The right-hand value.
  /// \returns True when the values differ.
  friend bool operator!=(
      const SocketOptionValue& lhs, const SocketOptionValue& rhs);
  /// Compares the value against an int for inequality.
  ///
  /// \param lhs The value to compare.
  /// \param rhs The integer to compare against.
  /// \returns True when the value does not hold an int equal to `rhs`.
  friend bool operator!=(const SocketOptionValue& lhs, int rhs);
  /// Compares the value against a string for inequality.
  ///
  /// \param lhs The value to compare.
  /// \param rhs The string to compare against.
  /// \returns True when the value does not hold a string equal to `rhs`.
  friend bool operator!=(const SocketOptionValue& lhs, const std::string& rhs);

 private:
  std::variant<int, std::string> val_;
};

/// Appends the string form of a value to the given string.
///
/// \param val The value to format.
/// \param result The string that receives the appended text.
void toAppend(const SocketOptionValue& val, std::string* result);

/// Writes the string form of a value to an output stream.
///
/// \param os The output stream to write to.
/// \param val The value to format.
/// \returns A reference to `os`.
std::ostream& operator<<(std::ostream& os, const SocketOptionValue& val);

} // namespace folly
