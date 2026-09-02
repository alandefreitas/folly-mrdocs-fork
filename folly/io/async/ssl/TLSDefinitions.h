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

#include <map>
#include <vector>

#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>

namespace folly {
/// SSL/TLS support types, handshake definitions, and OpenSSL helpers.
namespace ssl {

/// TLS extension type codes, per the IANA tls-extensiontype-values registry.
enum class TLSExtension : uint16_t {
  SERVER_NAME = 0, ///< server_name (SNI).
  MAX_FRAGMENT_LENGTH = 1, ///< max_fragment_length.
  CLIENT_CERTIFICATE_URL = 2, ///< client_certificate_url.
  TRUSTED_CA_KEYS = 3, ///< trusted_ca_keys.
  TRUNCATED_HMAC = 4, ///< truncated_hmac.
  STATUS_REQUEST = 5, ///< status_request (OCSP stapling).
  USER_MAPPING = 6, ///< user_mapping.
  CLIENT_AUTHZ = 7, ///< client_authz.
  SERVER_AUTHZ = 8, ///< server_authz.
  CERT_TYPE = 9, ///< cert_type.
  SUPPORTED_GROUPS = 10, ///< supported_groups (elliptic curves).
  EC_POINT_FORMATS = 11, ///< ec_point_formats.
  SRP = 12, ///< srp.
  SIGNATURE_ALGORITHMS = 13, ///< signature_algorithms.
  USE_SRTP = 14, ///< use_srtp.
  HEARTBEAT = 15, ///< heartbeat.
  APPLICATION_LAYER_PROTOCOL_NEGOTIATION = 16, ///< ALPN.
  STATUS_REQUEST_V2 = 17, ///< status_request_v2.
  SIGNED_CERTIFICATE_TIMESTAMP = 18, ///< signed_certificate_timestamp (SCT).
  CLIENT_CERTIFICATE_TYPE = 19, ///< client_certificate_type.
  SERVER_CERTIFICATE_TYPE = 20, ///< server_certificate_type.
  PADDING = 21, ///< padding.
  ENCRYPT_THEN_MAC = 22, ///< encrypt_then_mac.
  EXTENDED_MASTER_SECRET = 23, ///< extended_master_secret.
  SESSION_TICKET = 35, ///< session_ticket.
  SUPPORTED_VERSIONS = 43, ///< supported_versions.
  TLS_CACHED_INFO_FB = 60001, ///< Facebook-specific, not IANA assigned yet.
  RENEGOTIATION_INFO = 65281 ///< renegotiation_info.
};

/// TLS signature-scheme hash algorithm codes (IANA tls-parameters).
enum class HashAlgorithm : uint8_t {
  NONE = 0, ///< No hash.
  MD5 = 1, ///< MD5.
  SHA1 = 2, ///< SHA-1.
  SHA224 = 3, ///< SHA-224.
  SHA256 = 4, ///< SHA-256.
  SHA384 = 5, ///< SHA-384.
  SHA512 = 6 ///< SHA-512.
};

/// TLS signature-scheme signature algorithm codes (IANA tls-parameters).
enum class SignatureAlgorithm : uint8_t {
  ANONYMOUS = 0, ///< Anonymous (no signature).
  RSA = 1, ///< RSA.
  DSA = 2, ///< DSA.
  ECDSA = 3 ///< ECDSA.
};

/// TLS server-name entry type.
enum class NameType : uint8_t {
  HOST_NAME = 0, ///< DNS host name.
};

/// Parsed fields extracted from a TLS ClientHello message.
struct ClientHelloInfo {
  folly::IOBufQueue clientHelloBuf_; ///< Raw ClientHello bytes.
  uint8_t clientHelloMajorVersion_; ///< Legacy major protocol version.
  uint8_t clientHelloMinorVersion_; ///< Legacy minor protocol version.
  std::vector<uint16_t> clientHelloCipherSuites_; ///< Offered cipher suites.
  std::vector<uint8_t>
      clientHelloCompressionMethods_; ///< Offered compression methods.
  std::vector<TLSExtension> clientHelloExtensions_; ///< Offered extensions.
  std::vector<std::pair<HashAlgorithm, SignatureAlgorithm>>
      clientHelloSigAlgs_; ///< Offered signature algorithms.
  std::vector<uint16_t>
      clientHelloSupportedVersions_; ///< Offered supported_versions.

  /// Server name requested via SNI (only HOST_NAME is tracked).
  std::string clientHelloSNIHostname_;
  std::vector<std::string> clientAlpns_; ///< ALPN protocols offered.
};

} // namespace ssl
} // namespace folly
