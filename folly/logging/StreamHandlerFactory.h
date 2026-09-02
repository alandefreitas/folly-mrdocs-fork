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

#include <folly/logging/FileWriterFactory.h>
#include <folly/logging/LogHandlerFactory.h>
#include <folly/logging/StandardLogHandlerFactory.h>

/// The Folly library.
namespace folly {

/**
 * StreamHandlerFactory is a LogHandlerFactory that constructs log handlers
 * that write to stdout or stderr.
 *
 * This is quite similar to FileHandlerFactory, but it always writes to an
 * existing open file descriptor rather than opening a new file.  This handler
 * factory is separate from FileHandlerFactory primarily for safety reasons:
 * FileHandlerFactory supports appending to arbitrary files via config
 * parameters, while StreamHandlerFactory does not.
 */
class StreamHandlerFactory : public LogHandlerFactory {
 public:
  /// Returns the configuration type name for this factory.
  ///
  /// \returns The string `"stream"`.
  StringPiece getType() const override { return "stream"; }

  /// Creates a log handler from the given options.
  ///
  /// \param options The handler configuration options.
  /// \returns The newly created log handler.
  std::shared_ptr<LogHandler> createHandler(const Options& options) override;

  /// Builds the log writer used by handlers from this factory.
  class WriterFactory : public StandardLogHandlerFactory::WriterFactory {
   public:
    /// Processes a single configuration option.
    ///
    /// \param name The option name.
    /// \param value The option value.
    /// \returns `true` if the option was recognized and applied.
    bool processOption(StringPiece name, StringPiece value) override;
    /// Creates the configured log writer.
    ///
    /// \returns The newly created log writer.
    std::shared_ptr<LogWriter> createWriter() override;

   private:
    std::string stream_;
    FileWriterFactory fileWriterFactory_;
  };
};

} // namespace folly
