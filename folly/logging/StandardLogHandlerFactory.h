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

#include <folly/Range.h>

/// The Folly library.
namespace folly {

class LogWriter;
class LogFormatter;
class StandardLogHandler;

/**
 * StandardLogHandlerFactory contains helper methods for LogHandlerFactory
 * implementations that create StandardLogHandler objects.
 *
 * StandardLogHandlerFactory does not derive from LogHandlerFactory itself.
 */
class StandardLogHandlerFactory {
 public:
  /// A map of configuration option names to values.
  using Options = std::unordered_map<std::string, std::string>;

  /// Interface for objects that consume configuration options.
  class OptionProcessor {
   public:
    /// Destroys the option processor.
    virtual ~OptionProcessor() {}

    /**
     * Process an option.
     *
     * This should return true if the option was processed successfully,
     * or false if this is an unknown option name.  It should throw an
     * exception if the option name is known but there is a problem with the
     * value.
     *
     * @param name The option name.
     * @param value The option value.
     * @returns `true` if the option was recognized and applied.
     */
    virtual bool processOption(StringPiece name, StringPiece value) = 0;
  };

  /// Interface for factories that build a LogFormatter from options.
  class FormatterFactory : public OptionProcessor {
   public:
    /// Creates the configured formatter.
    ///
    /// \param logWriter The writer the formatter will feed.
    /// \returns The newly created formatter.
    virtual std::shared_ptr<LogFormatter> createFormatter(
        const std::shared_ptr<LogWriter>& logWriter) = 0;
  };

  /// Interface for factories that build a LogWriter from options.
  class WriterFactory : public OptionProcessor {
   public:
    /// Creates the configured writer.
    ///
    /// \returns The newly created writer.
    virtual std::shared_ptr<LogWriter> createWriter() = 0;
  };

  /// Creates a standard log handler using a writer factory.
  ///
  /// \param type The handler type name.
  /// \param writerFactory The factory that builds the writer.
  /// \param options The handler configuration options.
  /// \returns The newly created handler.
  static std::shared_ptr<StandardLogHandler> createHandler(
      StringPiece type, WriterFactory* writerFactory, const Options& options);

  /// Creates a standard log handler using writer and formatter factories.
  ///
  /// \param type The handler type name.
  /// \param writerFactory The factory that builds the writer.
  /// \param formatterFactory The factory that builds the formatter.
  /// \param options The handler configuration options.
  /// \returns The newly created handler.
  static std::shared_ptr<StandardLogHandler> createHandler(
      StringPiece type,
      WriterFactory* writerFactory,
      FormatterFactory* formatterFactory,
      const Options& options);
};

} // namespace folly
