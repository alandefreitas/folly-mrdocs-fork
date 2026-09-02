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

#include <memory>
#include <string>
#include <unordered_map>

#include <folly/CppAttributes.h>
#include <folly/Range.h>

/// The Folly library.
namespace folly {

class LogHandler;

/// Interface for factories that construct log handlers from config options.
class LogHandlerFactory {
 public:
  /// A map of configuration option names to values.
  using Options = std::unordered_map<std::string, std::string>;

  /// Destroys the factory.
  virtual ~LogHandlerFactory() = default;

  /**
   * Get the type name of this LogHandlerFactory.
   *
   * The type field in the LogHandlerConfig for all LogHandlers created by this
   * factory should match the type of the LogHandlerFactory.
   *
   * The type of a LogHandlerFactory should never change.  The returned
   * StringPiece should be valid for the lifetime of the LogHandlerFactory.
   *
   * @returns The factory's type name.
   */
  virtual StringPiece getType() const = 0;

  /**
   * Create a new LogHandler.
   *
   * @param options The handler configuration options.
   * @returns The newly created log handler.
   */
  virtual std::shared_ptr<LogHandler> createHandler(const Options& options) = 0;

  /**
   * Update an existing LogHandler with a new configuration.
   *
   * This may create a new LogHandler object, or it may update the existing
   * LogHandler in place.
   *
   * The returned pointer will point to the input handler if it was updated in
   * place, or will point to a new LogHandler if a new one was created.
   *
   * @param existingHandler The handler to update.
   * @param options The new configuration options.
   * @returns The updated handler, or a newly created one.
   */
  virtual std::shared_ptr<LogHandler> updateHandler(
      [[maybe_unused]] const std::shared_ptr<LogHandler>& existingHandler,
      const Options& options) {
    // Subclasses may override this with functionality to update an existing
    // handler in-place.  However, provide a default implementation that simply
    // calls createHandler() to always create a new handler object.
    return createHandler(options);
  }
};

} // namespace folly
