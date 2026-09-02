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

#include <folly/Optional.h>
#include <folly/logging/LogLevel.h>

/// The Folly library.
namespace folly {

/**
 * Configuration for a LogCategory
 */
class LogCategoryConfig {
 public:
  /// Constructs a category configuration with an optional level.
  ///
  /// \param level The log level for the category.
  /// \param inheritParentLevel Whether to inherit a more verbose parent level.
  explicit LogCategoryConfig(
      LogLevel level = kDefaultLogLevel, bool inheritParentLevel = true);
  /// Constructs a category configuration with an explicit handler list.
  ///
  /// \param level The log level for the category.
  /// \param inheritParentLevel Whether to inherit a more verbose parent level.
  /// \param handlers The names of the log handlers to attach.
  LogCategoryConfig(
      LogLevel level,
      bool inheritParentLevel,
      std::vector<std::string> handlers);

  /// Tests two configurations for equality.
  ///
  /// \param other The configuration to compare against.
  /// \returns `true` if the configurations are equivalent.
  bool operator==(const LogCategoryConfig& other) const;
  /// Tests two configurations for inequality.
  ///
  /// \param other The configuration to compare against.
  /// \returns `true` if the configurations differ.
  bool operator!=(const LogCategoryConfig& other) const;

  /**
   * The LogLevel for this category.
   */
  LogLevel level{kDefaultLogLevel};

  /**
   * Whether this category should inherit its effective log level from its
   * parent category, if the parent category has a more verbose log level.
   */
  bool inheritParentLevel{true};

  /**
   * Which messages at this category should propagate to its parent category.
   */
  LogLevel propagateLevelMessagesToParent{LogLevel::MIN_LEVEL};

  /**
   * An optional list of LogHandler names to use for this category.
   *
   * When applying config changes to an existing LogCategory, the existing
   * LogHandler list will be left unchanged if this field is unset.
   */
  Optional<std::vector<std::string>> handlers;
};

} // namespace folly
