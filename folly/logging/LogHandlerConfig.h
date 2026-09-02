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
#include <unordered_map>

#include <folly/Optional.h>
#include <folly/Range.h>

/// The Folly library.
namespace folly {

/**
 * Configuration for a LogHandler
 */
class LogHandlerConfig {
 public:
  /// A map of handler option names to their values.
  using Options = std::unordered_map<std::string, std::string>;

  /// Construct an empty LogHandlerConfig with no type set.
  LogHandlerConfig();
  /// Construct a LogHandlerConfig with the given handler type.
  ///
  /// \param type The handler type name.
  explicit LogHandlerConfig(StringPiece type);
  /// Construct a LogHandlerConfig with the given optional handler type.
  ///
  /// \param type The handler type name, or unset.
  explicit LogHandlerConfig(Optional<StringPiece> type);
  /// Construct a LogHandlerConfig with the given type and options.
  ///
  /// \param type The handler type name.
  /// \param opts The handler options.
  LogHandlerConfig(StringPiece type, Options opts);
  /// Construct a LogHandlerConfig with the given optional type and options.
  ///
  /// \param type The handler type name, or unset.
  /// \param opts The handler options.
  LogHandlerConfig(Optional<StringPiece> type, Options opts);

  /**
   * Update this LogHandlerConfig object by merging in settings from another
   * LogConfig.
   *
   * The other LogHandlerConfig must not have a type set.
   *
   * \param other The configuration whose settings are merged in.
   */
  void update(const LogHandlerConfig& other);

  /// Return true if this config equals another config.
  ///
  /// \param other The configuration to compare against.
  /// \returns `true` if the configurations are equal.
  bool operator==(const LogHandlerConfig& other) const;
  /// Return true if this config differs from another config.
  ///
  /// \param other The configuration to compare against.
  /// \returns `true` if the configurations differ.
  bool operator!=(const LogHandlerConfig& other) const;

  /**
   * The handler type name.
   *
   * If this field is unset than this configuration object is intended to be
   * used to update an existing LogHandler object.  This field must always
   * be set in the configuration for all existing LogHandler objects.
   */
  Optional<std::string> type;

  /// The handler-specific options.
  Options options;
};

} // namespace folly
