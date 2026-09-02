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

#include <folly/File.h>
#include <folly/Range.h>
#include <folly/logging/LogWriter.h>

/// The Folly library.
namespace folly {

/**
 * A LogWriter implementation that immediately writes to a file descriptor when
 * it is invoked.
 *
 * The downside of this class is that logging I/O occurs directly in your
 * normal program threads, so that logging I/O may block or slow down normal
 * processing.
 *
 * However, one benefit of this class is that log messages are written out
 * immediately, so if your program crashes, all log messages generated before
 * the crash will have already been written, and no messages will be lost.
 */
class ImmediateFileWriter : public LogWriter {
 public:
  /**
   * Construct an ImmediateFileWriter that appends to the file at the specified
   * path.
   *
   * @param path The path of the file to append to.
   */
  explicit ImmediateFileWriter(folly::StringPiece path);

  /**
   * Construct an ImmediateFileWriter that writes to the specified File object.
   *
   * @param file The file to write to.
   */
  explicit ImmediateFileWriter(folly::File&& file);

  /// Inherits the base class writeMessage() overloads.
  using LogWriter::writeMessage;
  /// Writes a serialized message immediately to the output file.
  ///
  /// \param buffer The serialized message bytes to write.
  /// \param flags A bitwise-ORed set of LogWriter::Flags values.
  void writeMessage(folly::StringPiece buffer, uint32_t flags = 0) override;
  /// Flushes any buffered output to the file.
  void flush() override;

  /**
   * Returns true if the output steam is a tty.
   *
   * @returns `true` if the output file refers to a terminal.
   */
  bool ttyOutput() const override { return isatty(file_.fd()); }

  /**
   * Get the output file.
   *
   * @returns The file this writer writes to.
   */
  const folly::File& getFile() const { return file_; }

 private:
  ImmediateFileWriter(ImmediateFileWriter const&) = delete;
  ImmediateFileWriter& operator=(ImmediateFileWriter const&) = delete;

  folly::File file_;
};
} // namespace folly
