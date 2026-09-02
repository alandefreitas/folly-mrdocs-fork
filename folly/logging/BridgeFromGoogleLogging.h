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
#ifndef GLOG_NO_ABBREVIATED_SEVERITIES
#define GLOG_NO_ABBREVIATED_SEVERITIES
#endif

#include <cstdint>

#include <glog/logging.h>

/// The Folly library.
namespace folly {
/// Folly's logging library.
namespace logging {
/** Class to bridge GLOG to folly logging.
 *
 * You only need one instance of this class in your program at a time. While
 * it's in scope it will bridge all GLOG messages to the folly logging
 * pipeline.
 *
 * You probably want this very early in `main` as:
 *   folly::logging::BridgeFromGoogleLogging glogToXlogBridge{};
 */
struct BridgeFromGoogleLogging : ::google::LogSink {
  /// Registers this bridge as a glog log sink.
  BridgeFromGoogleLogging();
  /// Unregisters this bridge from glog.
  ~BridgeFromGoogleLogging() noexcept override;

  // This class registers itself as a glog LogSink on construction and
  // unregisters on destruction, so it must not be copied or moved.
  /// Deleted copy constructor; the bridge is not copyable.
  ///
  /// \param other The bridge that would be copied.
  BridgeFromGoogleLogging(const BridgeFromGoogleLogging& other) = delete;
  /// Deleted move constructor; the bridge is not movable.
  ///
  /// \param other The bridge that would be moved.
  BridgeFromGoogleLogging(BridgeFromGoogleLogging&& other) = delete;
  /// Deleted copy assignment; the bridge is not copyable.
  ///
  /// \param other The bridge that would be assigned from.
  /// \returns A reference to this bridge.
  BridgeFromGoogleLogging& operator=(const BridgeFromGoogleLogging& other) =
      delete;
  /// Deleted move assignment; the bridge is not movable.
  ///
  /// \param other The bridge that would be assigned from.
  /// \returns A reference to this bridge.
  BridgeFromGoogleLogging& operator=(BridgeFromGoogleLogging&& other) = delete;

  /// Inherits the base class `send` overloads.
  using ::google::LogSink::send;

  /// Forwards a glog message (with microseconds) to folly logging.
  ///
  /// \param severity The glog severity level.
  /// \param full_filename The full source filename.
  /// \param base_filename The base source filename.
  /// \param line The source line number.
  /// \param pTime The message timestamp.
  /// \param message The message text.
  /// \param message_len The length of the message text.
  /// \param usecs The microseconds component of the timestamp.
  void send(
      ::google::LogSeverity severity,
      const char* full_filename,
      const char* base_filename,
      int line,
      const struct ::tm* pTime,
      const char* message,
      size_t message_len,
      int32_t usecs);

  /// Forwards a glog message to folly logging.
  ///
  /// \param severity The glog severity level.
  /// \param full_filename The full source filename.
  /// \param base_filename The base source filename.
  /// \param line The source line number.
  /// \param pTime The message timestamp.
  /// \param message The message text.
  /// \param message_len The length of the message text.
  void send(
      ::google::LogSeverity severity,
      const char* full_filename,
      const char* base_filename,
      int line,
      const struct ::tm* pTime,
      const char* message,
      size_t message_len) override;
};
} // namespace logging
} // namespace folly
