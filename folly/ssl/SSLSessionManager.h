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

#include <variant>

#include <folly/portability/OpenSSL.h>
#include <folly/ssl/OpenSSLPtrTypes.h>
#include <folly/ssl/SSLSession.h>

namespace folly {

/// Configures SSL connections and holds shared SSL context state.
class SSLContext;

namespace ssl {

namespace detail {

class OpenSSLSession;

} // namespace detail

/**
 * A class that manages one SSL session.
 *
 * Only intended to temporarily handle both raw session and
 * abstract session types, until session APIs are merged in
 * in AsyncSSLSocket. Afterwards, it will only manage an
 * abstract session.
 */
class SSLSessionManager {
 public:
  /// Constructs an empty session manager.
  SSLSessionManager();

  /// Destroys the session manager.
  ~SSLSessionManager() = default;

  /// Stores an abstract SSL session.
  ///
  /// \param session The session to store.
  void setSession(std::shared_ptr<folly::ssl::SSLSession> session);

  /// Returns the stored abstract SSL session.
  ///
  /// \returns The stored session, or nullptr if none is set.
  std::shared_ptr<folly::ssl::SSLSession> getSession() const;

  /// Stores a raw OpenSSL session.
  ///
  /// \param session The raw session to store.
  void setRawSession(folly::ssl::SSLSessionUniquePtr session);

  /// Returns the stored raw OpenSSL session.
  ///
  /// \returns The stored raw session, or nullptr if none is set.
  folly::ssl::SSLSessionUniquePtr getRawSession() const;

  /**
   * Add SSLSessionManager instance to the ex data of ssl.
   * Needs to be called for SSLSessionManager::getFromSSL to return
   * a non-null pointer.
   *
   * \param ssl The SSL connection to attach to.
   */
  void attachToSSL(SSL* ssl);

  /**
   * Get pointer to a SSLSessionManager instance that was added to
   * the ex data of ssl through attachToSSL()
   *
   * \param ssl The SSL connection to query.
   * \returns The attached session manager, or nullptr if none.
   */
  static SSLSessionManager* getFromSSL(const SSL* ssl);

 private:
  friend class folly::SSLContext;

  /**
   * Called by SSLContext when a new session is negotiated for the
   * SSL connection that SSLSessionManager is attached to.
   */
  void onNewSession(folly::ssl::SSLSessionUniquePtr session);

  /**
   * The SSL session. Which type the variant contains depends on the
   * session API that is used.
   */
  std::variant<
      folly::ssl::SSLSessionUniquePtr,
      std::shared_ptr<folly::ssl::detail::OpenSSLSession>>
      session_;
};

} // namespace ssl
} // namespace folly
