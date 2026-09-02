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

#include <vector>

#include <folly/Expected.h>
#include <folly/Optional.h>
#include <folly/json/dynamic.h>
#include <folly/json_pointer.h>

/// The Folly library namespace.
namespace folly {

/**
 * A parsed JSON Patch document, as described in RFC 6902 "JSON Patch".
 *
 * Implements parsing. Application over data structures must be
 * implemented separately.
 */
class json_patch {
 public:
  /// Reason a JSON patch object failed to parse.
  enum class parse_error_code : uint8_t {
    undefined, ///< No specific error.
    invalid_shape, ///< The patch document has an unexpected shape.
    missing_op, ///< An operation is missing its "op" attribute.
    unknown_op, ///< The "op" attribute names an unknown operation.
    malformed_op, ///< The "op" attribute is malformed.
    missing_path_attr, ///< An operation is missing its "path" attribute.
    malformed_path_attr, ///< The "path" attribute is malformed.
    missing_from_attr, ///< An operation is missing its "from" attribute.
    malformed_from_attr, ///< The "from" attribute is malformed.
    missing_value_attr, ///< An operation is missing its "value" attribute.
    overlapping_pointers, ///< Two pointers in the operation overlap.
  };

  /**
   * Error returned when parsing a JSON patch object fails.
   *
   * Carries the error code along with a pointer to the part of the JSON
   * document that could not be parsed.
   */
  struct parse_error {
    /// One of the parse_error_code values.
    parse_error_code error_code{parse_error_code::undefined};
    /// Pointer to the object that caused the error.
    dynamic const* obj{};
  };

  /// The kind of a single JSON patch operation.
  enum class patch_operation_code : uint8_t {
    invalid = 0, ///< Not a valid operation.
    test, ///< Test that a value at a location equals a given value.
    remove, ///< Remove the value at a location.
    add, ///< Add a value at a location.
    replace, ///< Replace the value at a location.
    move, ///< Move a value from one location to another.
    copy, ///< Copy a value from one location to another.
  };

  /// A single JSON patch operation, whose arguments vary by operation type.
  struct patch_operation {
    /// The kind of operation.
    patch_operation_code op_code{patch_operation_code::invalid};
    /// The target location of the operation.
    json_pointer path;
    /// The source location, for move and copy operations.
    Optional<json_pointer> from;
    /// The operand value, for add, replace and test operations.
    Optional<dynamic> value;
    /// Compares two operations for equality.
    ///
    /// \param lhs The left-hand operation.
    /// \param rhs The right-hand operation.
    /// \returns True if the operations are equal.
    friend bool operator==(
        patch_operation const& lhs, patch_operation const& rhs) {
      return lhs.op_code == rhs.op_code && lhs.path == rhs.path &&
          lhs.from == rhs.from && lhs.value == rhs.value;
    }
    /// Compares two operations for inequality.
    ///
    /// \param lhs The left-hand operation.
    /// \param rhs The right-hand operation.
    /// \returns True if the operations differ.
    friend bool operator!=(
        patch_operation const& lhs, patch_operation const& rhs) {
      return !(lhs == rhs);
    }
  };

  /// Constructs an empty patch.
  json_patch() = default;
  /// Destroys the patch.
  ~json_patch() = default;

  /// Parses a JSON patch document into a json_patch.
  ///
  /// \param obj The JSON patch document to parse.
  /// \returns The parsed patch, or a parse_error on failure.
  static Expected<json_patch, parse_error> try_parse(
      dynamic const& obj) noexcept;

  /// Returns the parsed patch operations.
  ///
  /// \returns The list of operations in application order.
  std::vector<patch_operation> const& ops() const;

  /// Reason applying a patch operation failed.
  enum class patch_application_error_code : uint8_t {
    other, ///< An unspecified error.
    from_not_found, ///< The "from" pointer did not resolve.
    path_not_found, ///< The "path" pointer did not resolve.
    test_failed, ///< A "test" condition failed.
  };

  /// Error returned when applying a patch fails.
  struct patch_application_error {
    /// The kind of application error.
    patch_application_error_code error_code{};
    /// Index of the patch element (in the array) that caused the error.
    size_t index{};
  };

  /**
   * Mutates the supplied object in accordance with the patch operations.
   *
   * Leaves the object in a partially modified state if one of the operations
   * fails.
   *
   * \param obj The object to mutate in place.
   * \returns Unit on success, or a patch_application_error on failure.
   */
  Expected<Unit, patch_application_error> apply(dynamic& obj) const;

 private:
  std::vector<patch_operation> ops_;
};

} // namespace folly
