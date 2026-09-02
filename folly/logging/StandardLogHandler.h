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

#include <folly/File.h>
#include <folly/Range.h>
#include <folly/logging/LogHandler.h>
#include <folly/logging/LogHandlerConfig.h>

/// The Folly library.
namespace folly {

class LogFormatter;
class LogWriter;

/**
 * StandardLogHandler is a LogHandler implementation that uses a LogFormatter
 * class to serialize the LogMessage into a string, and then gives it to a
 * LogWriter object.
 *
 * This basically is a simple glue class that helps chain together
 * configurable LogFormatter and LogWriter objects.
 *
 * StandardLogHandler also supports ignoring messages less than a specific
 * LogLevel.  By default it processes all messages.
 */
class StandardLogHandler : public LogHandler {
 public:
  /// Constructs a handler from a formatter and writer.
  ///
  /// \param config The handler configuration.
  /// \param formatter The formatter that serializes messages.
  /// \param writer The writer that outputs serialized messages.
  /// \param syncLevel The level at or above which messages are written synchronously.
  StandardLogHandler(
      LogHandlerConfig config,
      std::shared_ptr<LogFormatter> formatter,
      std::shared_ptr<LogWriter> writer,
      LogLevel syncLevel = LogLevel::MAX_LEVEL);
  /// Destroys the handler.
  ~StandardLogHandler() override;

  /**
   * Get the LogFormatter used by this handler.
   *
   * @returns The formatter used to serialize messages.
   */
  const std::shared_ptr<LogFormatter>& getFormatter() const {
    return formatter_;
  }

  /**
   * Get the LogWriter used by this handler.
   *
   * @returns The writer used to output serialized messages.
   */
  const std::shared_ptr<LogWriter>& getWriter() const { return writer_; }

  /**
   * Get the handler's current LogLevel.
   *
   * Messages less than this LogLevel will be ignored.  This defaults to
   * LogLevel::NONE when the handler is constructed.
   *
   * @returns The current log level.
   */
  LogLevel getLevel() const { return level_.load(std::memory_order_acquire); }

  /**
   * Set the handler's current LogLevel.
   *
   * Messages less than this LogLevel will be ignored.
   *
   * @param level The new minimum log level to process.
   */
  void setLevel(LogLevel level) {
    return level_.store(level, std::memory_order_release);
  }

  /// Formats and writes a log message.
  ///
  /// \param message The message to handle.
  /// \param handlerCategory The category currently handling the message.
  void handleMessage(
      const LogMessage& message, const LogCategory* handlerCategory) override;

  /// Flushes any buffered output.
  void flush() override;

  /// Returns the handler's current configuration.
  ///
  /// \returns The handler configuration.
  LogHandlerConfig getConfig() const override;

 private:
  std::atomic<LogLevel> level_{LogLevel::NONE};
  std::atomic<LogLevel> syncLevel_{LogLevel::MAX_LEVEL};

  // The following variables are const, and cannot be modified after the
  // log handler is constructed.  This allows us to access them without
  // locking when handling a message.  To change these values, create a new
  // StandardLogHandler object and replace the old handler with the new one in
  // the LoggerDB.

  const std::shared_ptr<LogFormatter> formatter_;
  const std::shared_ptr<LogWriter> writer_;
  const LogHandlerConfig config_;
};
} // namespace folly
