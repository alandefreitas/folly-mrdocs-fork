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

/**
 * Error enums and exceptions for indicating errors when dealing with IP
 * Addresses. Used in IPAddress, IPAddressV4, and IPAddressV6.
 *
 * @file IPAddressException.h
 */

#pragma once

#include <exception>
#include <string>
#include <utility>

#include <folly/CPortability.h>
#include <folly/detail/IPAddress.h>
#include <folly/lang/Exception.h>

namespace folly {

/**
 * Error codes for non-throwing interface of IPAddress family of functions.
 */
enum class IPAddressFormatError {
  INVALID_IP, ///< The string is not a valid IP address.
  UNSUPPORTED_ADDR_FAMILY, ///< The address family is not supported.
  NULL_SOCKADDR, ///< A null sockaddr was provided.
};

/**
 * Wraps errors from parsing IP/MASK string
 */
enum class CIDRNetworkError {
  INVALID_DEFAULT_CIDR, ///< The default CIDR value is invalid.
  INVALID_IP_SLASH_CIDR, ///< The IP/MASK string is malformed.
  INVALID_IP, ///< The IP portion is not a valid address.
  INVALID_CIDR, ///< The CIDR portion is invalid.
  CIDR_MISMATCH, ///< The CIDR does not match the address family.
};

/**
 * Exception that is thrown when dealing with invalid IP addresses. A subclass
 * of `std::runtime_error`
 */
class FOLLY_EXPORT IPAddressFormatException : public std::runtime_error {
 public:
  /// Inherits the constructors of std::runtime_error.
  using std::runtime_error::runtime_error;
};

/**
 * Exception that is thrown when an IP Address is not of the family expected
 * (ie, expected a V4 but is a V6). A subclass of IPAddressFormatException.
 */
class FOLLY_EXPORT InvalidAddressFamilyException
    : public IPAddressFormatException {
 public:
  /// Constructs the exception from a C-string message.
  /// \param msg The explanatory message.
  explicit InvalidAddressFamilyException(const char* msg)
      : IPAddressFormatException{msg} {}
  /// Constructs the exception from a string message.
  /// \param msg The explanatory message.
  explicit InvalidAddressFamilyException(const std::string& msg) noexcept
      : IPAddressFormatException{msg} {}
  /// Constructs the exception from the offending address family.
  /// \param family The address family that was not expected.
  explicit InvalidAddressFamilyException(sa_family_t family) noexcept
      : InvalidAddressFamilyException(
            "Address family " + detail::familyNameStr(family) +
            " is not AF_INET or AF_INET6") {}
};

} // namespace folly
