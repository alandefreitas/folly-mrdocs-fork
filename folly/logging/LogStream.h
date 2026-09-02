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

#include <ostream>

#include <folly/logging/LogCategory.h>
#include <folly/logging/LogMessage.h>

/// The Folly library.
namespace folly {

/**
 * A std::streambuf implementation for use by LogStream
 */
class LogStreamBuffer : public std::streambuf {
 public:
  /// Constructs an empty buffer without reserving storage.
  LogStreamBuffer() {
    // We intentionally do not reserve any string buffer space initially,
    // since this will not be needed for XLOG() and XLOGF() statements
    // that do not use the streaming API.  (e.g., XLOG(INFO, "test ", 1234) )
  }

  /// Tests whether the buffer is empty.
  ///
  /// \returns `true` if no characters have been buffered.
  bool empty() const { return str_.empty(); }

  /// Extracts the buffered string, leaving the buffer empty.
  ///
  /// \returns The accumulated string.
  std::string extractString() {
    str_.resize(pptr() - (&str_.front()));
    return std::move(str_);
  }

  /// Handles buffer overflow by growing the underlying string.
  ///
  /// \param ch The character that caused the overflow.
  /// \returns The written character, or end-of-file on failure.
  int_type overflow(int_type ch) override;

 private:
  enum : size_t { kInitialCapacity = 256 };
  std::string str_;
};

class LogStreamProcessor;

/**
 * A std::ostream implementation for use by the logging macros.
 *
 * All-in-all this is pretty similar to std::stringstream, but lets us
 * destructively extract an rvalue-reference to the underlying string.
 */
class LogStream : public std::ostream {
 public:
  // Explicitly declare the default constructor and destructor, but only
  // define them in the .cpp file.  This prevents them from being inlined at
  // each FB_LOG() or XLOG() statement.  Inlining them just causes extra code
  // bloat, with minimal benefit--for debug log statements these never even get
  // called in the common case where the log statement is disabled.
  /// Constructs a stream bound to the given processor.
  ///
  /// \param processor The processor that receives the streamed message.
  explicit LogStream(LogStreamProcessor* processor);
  /// Destroys the stream.
  ~LogStream() override;

  /// Tests whether the stream has buffered any output.
  ///
  /// \returns `true` if no characters have been streamed.
  bool empty() const { return buffer_.empty(); }

  /// Extracts the buffered string, leaving the stream empty.
  ///
  /// \returns The accumulated string.
  std::string extractString() { return buffer_.extractString(); }

  /// Returns the processor bound to this stream.
  ///
  /// \returns The associated log stream processor.
  LogStreamProcessor* getProcessor() const { return processor_; }

 private:
  LogStreamBuffer buffer_;
  LogStreamProcessor* const processor_;
};
} // namespace folly
