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

#include <cstddef>

#include <folly/Range.h>

namespace folly {
namespace symbolizer {

/**
 * Async-signal-safe line reader.
 */
class LineReader {
 public:
  /**
   * Create a line reader that reads into a user-provided buffer (of size
   * bufSize).
   *
   * \param fd The file descriptor to read from.
   * \param buf The user-provided buffer to read into.
   * \param bufSize The size of buf, in bytes.
   */
  LineReader(int fd, char* buf, size_t bufSize);

  /// Deleted copy constructor; LineReader is non-copyable.
  LineReader(const LineReader& other) = delete;
  /// Deleted copy assignment; LineReader is non-copyable.
  LineReader& operator=(const LineReader& other) = delete;

  /// Result of a read attempt.
  enum State {
    kReading, ///< A line was read successfully.
    kEof, ///< The end of the file was reached.
    kError, ///< A read error was encountered.
  };
  /**
   * Read the next line from the file.
   *
   * If the line is at most bufSize characters long, including the trailing
   * newline, it will be returned (including the trailing newline).
   *
   * If the line is longer than bufSize, we return the first bufSize bytes
   * (which won't include a trailing newline) and then continue from that
   * point onwards.
   *
   * The lines returned are not null-terminated.
   *
   * Returns kReading with a valid line, kEof if at end of file, or kError
   * if a read error was encountered.
   *
   * Example:
   *   bufSize = 10
   *   input has "hello world\n"
   *   The first call returns "hello worl"
   *   The second call returns "d\n"
   *
   * \param line Set to the next line read from the file.
   * \returns kReading with a valid line, kEof at end of file, or kError on a
   * read error.
   */
  State readLine(StringPiece& line);

 private:
  int const fd_;
  char* const buf_;
  char* const bufEnd_;

  // buf_ <= bol_ <= eol_ <= end_ <= bufEnd_
  //
  // [buf_, end_): current buffer contents (read from file)
  //
  // [buf_, bol_): free (already processed, can be discarded)
  // [bol_, eol_): current line, including \n if it exists, eol_ points
  //               1 character past the \n
  // [eol_, end_): read, unprocessed
  // [end_, bufEnd_): free

  char* bol_;
  char* eol_;
  char* end_;
  State state_;
};
} // namespace symbolizer
} // namespace folly
