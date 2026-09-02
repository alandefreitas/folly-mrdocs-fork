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

#include <folly/io/async/AsyncSocketException.h>

/// Facebook Folly library namespace.
namespace folly {

/// Categories of SSL failure reported by SSLException.
enum class SSLError {
  CLIENT_RENEGOTIATION, ///< A client tried to renegotiate with this server.
  INVALID_RENEGOTIATION, ///< We attempted to start a renegotiation.
  EARLY_WRITE, ///< Wrote before the SSL connection was established.
  SSL_ERROR, ///< An error related to SSL.
  NETWORK_ERROR, ///< An error related to the network.
  EOF_ERROR, ///< The peer terminated the connection correctly.
};

/// Exception describing an SSL error and its underlying OpenSSL code.
class SSLException : public folly::AsyncSocketException {
 public:
  /// Constructs from a raw OpenSSL error triple.
  /// \param sslError The `SSL_get_error` result.
  /// \param errError The OpenSSL error queue code.
  /// \param sslOperationReturnValue Return value of the failed SSL operation.
  /// \param errno_copy Captured `errno` at the time of the error.
  SSLException(
      int sslError,
      unsigned long errError,
      int sslOperationReturnValue,
      int errno_copy);

  /// Constructs from a high-level SSLError category.
  /// \param error The SSL error category.
  explicit SSLException(SSLError error);

  /// Returns the high-level SSL error category.
  /// \returns The SSL error category.
  SSLError getSSLError() const { return sslError; }

  /// Returns the underlying OpenSSL error code.
  /// \returns The OpenSSL internal error code.
  unsigned long getInternalSSLError() const { return sslInternalErrorCode; }

 private:
  SSLError sslError; ///< High-level SSL error category.
  unsigned long sslInternalErrorCode; ///< Underlying OpenSSL error code.
};
} // namespace folly
