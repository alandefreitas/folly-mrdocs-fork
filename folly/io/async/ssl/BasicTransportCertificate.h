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

#include <folly/io/async/ssl/OpenSSLTransportCertificate.h>

/// Facebook Folly library namespace.
namespace folly {
/// SSL and TLS utilities.
namespace ssl {

/// In-memory transport certificate holding an identity and an optional X509.
class BasicTransportCertificate : public folly::OpenSSLTransportCertificate {
 public:
  /// Creates a basic transport cert from an existing one.
  ///
  /// \param cert Source certificate to copy the identity and X509 from.
  /// \returns A new certificate, or nullptr if \p cert is null.
  static std::unique_ptr<BasicTransportCertificate> create(
      const folly::AsyncTransportCertificate* cert) {
    if (!cert) {
      return nullptr;
    }
    return std::make_unique<BasicTransportCertificate>(
        cert->getIdentity(), OpenSSLTransportCertificate::tryExtractX509(cert));
  }

  /// Constructs the certificate from an identity and an X509.
  /// \param identity Identity string associated with the certificate.
  /// \param x509 Owned X509 handle, or null if unavailable.
  BasicTransportCertificate(
      std::string identity, folly::ssl::X509UniquePtr x509)
      : identity_(std::move(identity)), x509_(std::move(x509)) {}

  /// Returns the identity associated with the certificate.
  /// \returns The identity string.
  std::string getIdentity() const override { return identity_; }

  /// Returns a new owning reference to the X509, or null if none is held.
  /// \returns An owning X509 handle, or null if the certificate holds none.
  folly::ssl::X509UniquePtr getX509() const override {
    if (!x509_) {
      return nullptr;
    }
    auto x509raw = x509_.get();
    X509_up_ref(x509raw);
    return folly::ssl::X509UniquePtr(x509raw);
  }

  /// Releases the held X509, leaving the certificate without one.
  void dropX509() { x509_.reset(); }

 private:
  std::string identity_; ///< Identity associated with the certificate.
  folly::ssl::X509UniquePtr x509_; ///< Optional owned X509 handle.
};

} // namespace ssl
} // namespace folly
