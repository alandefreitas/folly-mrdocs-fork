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

#include <stdexcept>
#include <string>

#include <folly/CPortability.h>
#include <folly/Range.h>

namespace folly {

/// Exception thrown by folly asynchronous socket operations.
class FOLLY_EXPORT AsyncSocketException : public std::runtime_error {
 public:
  /// Categories of asynchronous socket failures.
  enum AsyncSocketExceptionType {
    UNKNOWN = 0, ///< Unknown or unclassified error.
    NOT_OPEN = 1, ///< The socket is not open.
    ALREADY_OPEN = 2, ///< The socket is already open.
    TIMED_OUT = 3, ///< The operation timed out.
    END_OF_FILE = 4, ///< The peer closed the connection.
    INTERRUPTED = 5, ///< The operation was interrupted.
    BAD_ARGS = 6, ///< Invalid arguments were supplied.
    CORRUPTED_DATA = 7, ///< The received data is corrupted.
    INTERNAL_ERROR = 8, ///< An internal error occurred.
    NOT_SUPPORTED = 9, ///< The operation is not supported.
    INVALID_STATE = 10, ///< The socket is in an invalid state for the operation.
    SSL_ERROR = 12, ///< An SSL/TLS error occurred.
    COULD_NOT_BIND = 13, ///< The socket could not be bound to an address.
    // SASL_HANDSHAKE_TIMEOUT = 14, // no longer used
    NETWORK_ERROR = 15, ///< A network-level error occurred.
    EARLY_DATA_REJECTED = 16, ///< TLS early data was rejected by the peer.
    CANCELED = 17, ///< The operation was canceled.
  };

  /// Asserts (in debug builds) the errno value to be nonnegative to help with
  /// linux io-uring, which passes errno values as negative numbers. But permits
  /// a special negative number which does not map to any actual errno errors.
  ///
  /// \param type The category of the failure.
  /// \param message A human-readable description of the failure.
  /// \param errnoCopy A copy of the errno value associated with the failure.
  AsyncSocketException(
      AsyncSocketExceptionType type,
      const std::string& message,
      int errnoCopy = 0)
      : std::runtime_error(getMessage(type, message, errnoCopy)),
        type_(type),
        errno_(errnoCopy) {
    assert(errnoCopy >= 0 || errnoCopy == INT_MIN);
  }

  /// Returns the category of this exception.
  ///
  /// \returns The category of this exception.
  AsyncSocketExceptionType getType() const noexcept { return type_; }

  /// Returns the errno value associated with this exception.
  ///
  /// \returns The errno value associated with this exception.
  int getErrno() const noexcept { return errno_; }

 protected:
  /// Returns the string of exception type.
  ///
  /// \param type The exception category to describe.
  /// \returns A string naming the exception type.
  static folly::StringPiece getExceptionTypeString(
      AsyncSocketExceptionType type);

  /// Returns a message based on the input.
  ///
  /// \param type The category of the failure.
  /// \param message A human-readable description of the failure.
  /// \param errnoCopy A copy of the errno value associated with the failure.
  /// \returns The composed exception message.
  static std::string getMessage(
      AsyncSocketExceptionType type, const std::string& message, int errnoCopy);

  /// Error code
  AsyncSocketExceptionType type_;

  /// A copy of the errno.
  int errno_;
};

} // namespace folly
