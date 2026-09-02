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

#include <folly/logging/LogCategoryConfig.h>
#include <folly/logging/LogHandlerConfig.h>

/// The Folly library.
namespace folly {

/**
 * LogConfig contains configuration for the LoggerDB.
 *
 * This includes information about the log levels for log categories,
 * as well as what log handlers are configured and which categories they are
 * attached to.
 */
class LogConfig {
 public:
  /// A map of category names to their configuration.
  using CategoryConfigMap = std::unordered_map<std::string, LogCategoryConfig>;
  /// A map of handler names to their configuration.
  using HandlerConfigMap = std::unordered_map<std::string, LogHandlerConfig>;

  /// Construct an empty LogConfig.
  LogConfig() = default;
  /// Construct a LogConfig from handler and category configuration maps.
  ///
  /// \param handlerConfigs The handler configuration settings.
  /// \param catConfigs The category configuration settings.
  explicit LogConfig(
      HandlerConfigMap handlerConfigs, CategoryConfigMap catConfigs)
      : handlerConfigs_{std::move(handlerConfigs)},
        categoryConfigs_{std::move(catConfigs)} {}

  /// Get the per-category configuration settings.
  ///
  /// \returns The map of category names to their configuration.
  const CategoryConfigMap& getCategoryConfigs() const {
    return categoryConfigs_;
  }
  /// Get the per-handler configuration settings.
  ///
  /// \returns The map of handler names to their configuration.
  const HandlerConfigMap& getHandlerConfigs() const { return handlerConfigs_; }

  /// Return true if this config equals another config.
  ///
  /// \param other The configuration to compare against.
  /// \returns `true` if the configurations are equal.
  bool operator==(const LogConfig& other) const;
  /// Return true if this config differs from another config.
  ///
  /// \param other The configuration to compare against.
  /// \returns `true` if the configurations differ.
  bool operator!=(const LogConfig& other) const;

  /**
   * Update this LogConfig object by merging in settings from another
   * LogConfig.
   *
   * All LogHandler settings from the other LogConfig will be inserted into
   * this LogConfig.  If a log handler with the same name was already defined
   * in this LogConfig it will be replaced with the new settings.
   *
   * All LogCategory settings from the other LogConfig will be inserted into
   * this LogConfig.  If a log category with the same name was already defined
   * in this LogConfig, its settings will be updated with settings from the
   * other LogConfig.  However, if the other LogConfig does not define handler
   * settings for the category it will retain its current handler settings.
   *
   * This method allows LogConfig objects to be combined before applying them.
   * Using LogConfig::update() will produce the same results as if
   * LoggerDB::updateConfig() had been called with both configs sequentially.
   * In other words, this operation:
   *
   *   configA.update(configB);
   *   loggerDB.updateConfig(configA);
   *
   * will produce the same results as:
   *
   *   loggerDB.updateConfig(configA);
   *   loggerDB.updateConfig(configA);
   *
   * \param other The configuration whose settings are merged in.
   */
  void update(const LogConfig& other);

 private:
  HandlerConfigMap handlerConfigs_;
  CategoryConfigMap categoryConfigs_;
};

} // namespace folly
