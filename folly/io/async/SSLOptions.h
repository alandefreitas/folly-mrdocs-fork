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

#include <folly/container/Array.h>
#include <folly/io/async/SSLContext.h>

/// Facebook Folly library namespace.
namespace folly {
/// SSL and TLS utilities.
namespace ssl {

/// Implementation details for the SSL options helpers.
namespace ssl_options_detail {
/// Logs an exception at DFATAL severity.
/// \param ex The exception to log.
void logDfatal(std::exception const& ex);
} // namespace ssl_options_detail

/// Recommended SSL options that stay compatible with older peers.
struct SSLOptionsCompatibility {
  /**
   * The group list for this options configuration.
   *
   * @return The recommended named groups (key-exchange curves).
   **/
  static constexpr auto groups() {
    return folly::make_array("X25519", "P-256", "P-384");
  }

  /**
   * The cipher list recommended for this options configuration.
   *
   * @return The recommended TLS 1.2 cipher list.
   */
  static constexpr auto ciphers() {
    return folly::make_array(
        "ECDHE-ECDSA-AES128-GCM-SHA256",
        "ECDHE-RSA-AES128-GCM-SHA256",
        "ECDHE-ECDSA-AES256-GCM-SHA384",
        "ECDHE-RSA-AES256-GCM-SHA384",
        "ECDHE-ECDSA-AES256-SHA",
        "ECDHE-RSA-AES256-SHA",
        "ECDHE-ECDSA-AES128-SHA",
        "ECDHE-RSA-AES128-SHA",
        "ECDHE-RSA-AES256-SHA384",
        "AES128-GCM-SHA256",
        "AES256-SHA",
        "AES128-SHA");
  }

  /**
   * The TLS 1.3 ciphersuites recommended for this options configuration.
   *
   * @return The recommended TLS 1.3 ciphersuites.
   */
  static constexpr auto ciphersuites() {
    return folly::make_array(
        "TLS_AES_128_GCM_SHA256",
        "TLS_AES_256_GCM_SHA384",
        "TLS_CHACHA20_POLY1305_SHA256");
  }

  /**
   * The list of signature algorithms recommended for this options
   * configuration.
   *
   * @return The recommended signature algorithms.
   */
  static constexpr auto sigalgs() {
    return folly::make_array(
        "rsa_pss_pss_sha512",
        "rsa_pss_rsae_sha512",
        "RSA+SHA512",
        "ECDSA+SHA512",
        "rsa_pss_pss_sha384",
        "rsa_pss_rsae_sha384",
        "RSA+SHA384",
        "ECDSA+SHA384",
        "rsa_pss_pss_sha256",
        "rsa_pss_rsae_sha256",
        "RSA+SHA256",
        "ECDSA+SHA256",
        "RSA+SHA1",
        "ECDSA+SHA1");
  }

  /**
   * Set common parameters on a client SSL context, for example,
   * ciphers, signature algorithms, verification options, and client EC curves.
   * @param ctx The SSL Context to which to apply the options.
   */
  static void setClientOptions(SSLContext& ctx);
};

/**
 * SSLServerOptionsCompatibility contains algorithms that are not recommended
 * for modern servers, but are included to maintain comaptibility with
 * very old clients.
 */
struct SSLServerOptionsCompatibility {
  /**
   * The group list for this options configuration.
   *
   * @return The recommended named groups (key-exchange curves).
   **/
  static constexpr auto groups() {
    return folly::make_array("X25519", "P-256", "P-384");
  }
  /**
   * The list of ciphers recommended for server use.
   *
   * @return The recommended server cipher list.
   */
  static constexpr auto ciphers() {
    return folly::make_array(
        "ECDHE-ECDSA-AES128-GCM-SHA256",
        "ECDHE-ECDSA-AES256-GCM-SHA384",
        "ECDHE-ECDSA-AES128-SHA",
        "ECDHE-ECDSA-AES256-SHA",
        "ECDHE-RSA-AES128-GCM-SHA256",
        "ECDHE-RSA-AES256-GCM-SHA384",
        "ECDHE-RSA-AES128-SHA",
        "ECDHE-RSA-AES256-SHA",
        "AES128-GCM-SHA256",
        "AES256-GCM-SHA384",
        "AES128-SHA",
        "AES256-SHA");
  }

  /**
   * The TLS 1.3 ciphersuites recommended for this options configuration.
   *
   * @return The recommended TLS 1.3 ciphersuites.
   */
  static constexpr auto ciphersuites() {
    return folly::make_array(
        "TLS_AES_128_GCM_SHA256",
        "TLS_AES_256_GCM_SHA384",
        "TLS_CHACHA20_POLY1305_SHA256");
  }
};

/**
 * SSLOptions2021 contains options that any new client or server from 2021
 * onwards should be using.
 *
 * It contains:
 *   * AEAD only ciphers with ephemeral key exchanges. (No support for RSA key
 *     encapsulation)
 *   * Signature algorithms that do not include insecure digests (such as SHA1)
 *
 **/
struct SSLOptions2021 {
  /// The cipher list recommended for this options configuration.
  /// \returns The recommended AEAD cipher list for 2021-era peers.
  static constexpr auto ciphers() {
    return folly::make_array(
        "ECDHE-ECDSA-AES128-GCM-SHA256",
        "ECDHE-RSA-AES128-GCM-SHA256",
        "ECDHE-ECDSA-AES256-GCM-SHA384",
        "ECDHE-RSA-AES256-GCM-SHA384",
        "ECDHE-ECDSA-CHACHA20-POLY1305",
        "ECDHE-RSA-CHACHA20-POLY1305");
  }

  /// The TLS 1.3 ciphersuites recommended for this options configuration.
  /// \returns The recommended TLS 1.3 ciphersuites.
  static constexpr auto ciphersuites() {
    return folly::make_array(
        "TLS_AES_128_GCM_SHA256",
        "TLS_AES_256_GCM_SHA384",
        "TLS_CHACHA20_POLY1305_SHA256");
  }

  /// The signature algorithms recommended for this options configuration.
  /// \returns The recommended signature algorithms (no insecure digests).
  static constexpr auto sigalgs() {
    return folly::make_array(
        "rsa_pss_pss_sha512",
        "rsa_pss_rsae_sha512",
        "RSA+SHA512",
        "ECDSA+SHA512",
        "rsa_pss_pss_sha384",
        "rsa_pss_rsae_sha384",
        "RSA+SHA384",
        "ECDSA+SHA384",
        "rsa_pss_pss_sha256",
        "rsa_pss_rsae_sha256",
        "RSA+SHA256",
        "ECDSA+SHA256");
  }
};

/// Default option set for clients and general use.
using SSLCommonOptions = SSLOptionsCompatibility;
/// Default option set for servers.
using SSLServerOptions = SSLOptions2021;

/**
 * Set the cipher suite of ctx to that in TSSLOptions, and print any runtime
 * error it catches.
 * @param ctx The SSLContext to apply the desired SSL options to.
 */
template <typename TSSLOptions>
void setCipherSuites(SSLContext& ctx) {
  try {
    std::string ciphersuites;
    folly::join(':', TSSLOptions::ciphersuites(), ciphersuites);
    ctx.setCiphersuitesOrThrow(std::move(ciphersuites));
    ctx.setCipherList(TSSLOptions::ciphers());
  } catch (std::runtime_error const& e) {
    ssl_options_detail::logDfatal(e);
  }
}

/**
 * Set the groups of ctx to that in TSSLOptions, and print any runtime
 * error it catches.
 * @param ctx The SSLContext to apply the desired groups to.
 */
template <typename TSSLOptions>
void setGroups(SSLContext& ctx) {
  try {
    const auto& groups_arr = TSSLOptions::groups();
    ctx.setSupportedGroups(
        std::vector<std::string>(groups_arr.begin(), groups_arr.end()));
  } catch (std::runtime_error const& e) {
    ssl_options_detail::logDfatal(e);
  }
}

/**
 * Set the cipher suite of ctx to the passed in  cipherList,
 * and print any runtime error it catches.
 * @param ctx The SSLContext to apply the desired SSL options to.
 * @param cipherList the list of ciphersuites to set
 */
template <typename Container>
void setCipherSuites(SSLContext& ctx, const Container& cipherList) {
  try {
    ctx.setCipherList(cipherList);
  } catch (std::runtime_error const& e) {
    ssl_options_detail::logDfatal(e);
  }
}

/**
 * Set the signature algorithm list of ctx to that in TSSLOptions, and print
 * any runtime errors it catche.
 * @param ctx The SSLContext to apply the desired SSL options to.
 */
template <typename TSSLOptions>
void setSignatureAlgorithms(SSLContext& ctx) {
  try {
    ctx.setSignatureAlgorithms(TSSLOptions::sigalgs());
  } catch (std::runtime_error const& e) {
    ssl_options_detail::logDfatal(e);
  }
}

} // namespace ssl
} // namespace folly
