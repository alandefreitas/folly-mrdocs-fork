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

#include <compare>
#include <iosfwd>

#include <folly/Conv.h>
#include <folly/CppAttributes.h>
#include <folly/Expected.h>
#include <folly/Range.h>
#include <folly/Unit.h>
#include <folly/lang/Bits.h>

namespace folly {

/// Forward declaration of the IPv6 address type used by createMulticast().
class IPAddressV6;

/// Error codes reported when parsing or constructing a MacAddress fails.
enum class MacAddressFormatError {
  Invalid, ///< The input is not a valid MAC address.
};

/**
 * MacAddress represents an IEEE 802 MAC address.
 */
class MacAddress {
 public:
  /// Number of bytes in a MAC address.
  static constexpr size_t SIZE = 6;
  /// The broadcast MAC address (all bytes set to 0xFF).
  static const MacAddress BROADCAST;
  /// The zero MAC address (all bytes set to 0x00).
  static const MacAddress ZERO;

  /**
   * Construct a zero-initialized MacAddress.
   */
  MacAddress() { memset(&bytes_, 0, 8); }

  /**
   * Parse a MacAddress from a human-readable string.
   * The string must contain 6 one- or two-digit hexadecimal
   * numbers, separated by dashes or colons.
   * Examples: 00:02:C9:C8:F9:68 or 0-2-c9-c8-f9-68
   *
   * \param str The human-readable string to parse.
   */
  explicit MacAddress(StringPiece str);

  /// Try to parse a MacAddress from a human-readable string.
  ///
  /// \param value The human-readable string to parse.
  /// \returns The parsed MAC address, or a format error on failure.
  static Expected<MacAddress, MacAddressFormatError> tryFromString(
      StringPiece value) {
    MacAddress ret;
    auto ok = ret.trySetFromString(value);
    if (!ok) {
      return makeUnexpected(ok.error());
    }
    return ret;
  }
  /// Parse a MacAddress from a human-readable string, throwing on failure.
  ///
  /// \param value The human-readable string to parse.
  /// \returns The parsed MAC address.
  static MacAddress fromString(StringPiece value) {
    MacAddress ret;
    ret.setFromString(value);
    return ret;
  }

  /**
   * Construct a MAC address from its 6-byte binary value
   *
   * \param value The 6-byte binary value.
   * \returns The parsed MAC address, or a format error on failure.
   */
  static Expected<MacAddress, MacAddressFormatError> tryFromBinary(
      ByteRange value) {
    MacAddress ret;
    auto ok = ret.trySetFromBinary(value);
    if (!ok) {
      return makeUnexpected(ok.error());
    }
    return ret;
  }
  /// Construct a MAC address from its 6-byte binary value, throwing on failure.
  ///
  /// \param value The 6-byte binary value.
  /// \returns The parsed MAC address.
  static MacAddress fromBinary(ByteRange value) {
    MacAddress ret;
    ret.setFromBinary(value);
    return ret;
  }

  /**
   * Construct a MacAddress from a uint64_t in network byte order.
   *
   * The first two bytes are ignored, and the MAC address is taken from the
   * latter 6 bytes.
   *
   * This is a static method rather than a constructor to avoid confusion
   * between host and network byte order constructors.
   *
   * \param value The address as a uint64_t in network byte order.
   * \returns The MAC address built from `value`.
   */
  static MacAddress fromNBO(uint64_t value) { return MacAddress(value); }

  /**
   * Construct a MacAddress from a uint64_t in host byte order.
   *
   * The most significant two bytes are ignored, and the MAC address is taken
   * from the least significant 6 bytes.
   *
   * This is a static method rather than a constructor to avoid confusion
   * between host and network byte order constructors.
   *
   * \param value The address as a uint64_t in host byte order.
   * \returns The MAC address built from `value`.
   */
  static MacAddress fromHBO(uint64_t value) {
    return MacAddress(Endian::big(value));
  }

  /**
   * Construct the multicast MacAddress for the specified multicast IPv6
   * address.
   *
   * \param addr The multicast IPv6 address.
   * \returns The multicast MAC address for `addr`.
   */
  static MacAddress createMulticast(IPAddressV6 addr);

  /**
   * Get a pointer to the MAC address' binary value.
   *
   * The returned value points to internal storage inside the MacAddress
   * object.  It is only valid as long as the MacAddress, and its contents may
   * change if the MacAddress is updated.
   *
   * \returns A pointer to the 6-byte binary value.
   */
  const uint8_t* bytes() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return bytes_ + 2;
  }

  /**
   * Return the address as a uint64_t, in network byte order.
   *
   * The first two bytes will be 0, and the subsequent 6 bytes will contain
   * the address in network byte order.
   *
   * \returns The address as a uint64_t in network byte order.
   */
  uint64_t u64NBO() const { return packedBytes(); }

  /**
   * Return the address as a uint64_t, in host byte order.
   *
   * The two most significant bytes will be 0, and the remaining 6 bytes will
   * contain the address.  The most significant of these 6 bytes will contain
   * the first byte that appear on the wire, and the least significant byte
   * will contain the last byte.
   *
   * \returns The address as a uint64_t in host byte order.
   */
  uint64_t u64HBO() const {
    // Endian::big() does what we want here, even though we are converting
    // from big-endian to host byte order.  This swaps if and only if
    // the host byte order is little endian.
    return Endian::big(packedBytes());
  }

  /**
   * Return a human-readable representation of the MAC address.
   *
   * \returns The MAC address formatted as a string.
   */
  std::string toString() const;

  /**
   * Update the current MacAddress object from a human-readable string.
   *
   * \param value The human-readable string to parse.
   * \returns Unit on success, or a format error on failure.
   */
  Expected<Unit, MacAddressFormatError> trySetFromString(StringPiece value);
  /// Update the current MacAddress object from a human-readable string, throwing on failure.
  ///
  /// \param value The human-readable string to parse.
  void setFromString(StringPiece value);
  /// Update the current MacAddress object from a human-readable string.
  ///
  /// \param str The human-readable string to parse.
  void parse(StringPiece str) { setFromString(str); }

  /**
   * Update the current MacAddress object from a 6-byte binary representation.
   *
   * \param value The 6-byte binary representation to parse.
   * \returns Unit on success, or a format error on failure.
   */
  Expected<Unit, MacAddressFormatError> trySetFromBinary(ByteRange value);
  /// Update the current MacAddress object from a 6-byte binary representation, throwing on failure.
  ///
  /// \param value The 6-byte binary representation to parse.
  void setFromBinary(ByteRange value);

  /// Return true if this is the broadcast MAC address.
  ///
  /// \returns true if this is the broadcast address.
  bool isBroadcast() const { return *this == BROADCAST; }
  /// Return true if this is a multicast MAC address.
  ///
  /// \returns true if this is a multicast address.
  bool isMulticast() const { return getByte(0) & 0x1; }
  /// Return true if this is a unicast MAC address.
  ///
  /// \returns true if this is a unicast address.
  bool isUnicast() const { return !isMulticast(); }

  /**
   * Return true if this MAC address is locally administered.
   *
   * Locally administered addresses are assigned by the local network
   * administrator, and are not guaranteed to be globally unique.  (It is
   * similar to IPv4's private address space.)
   *
   * Note that isLocallyAdministered() will return true for the broadcast
   * address, since it has the locally administered bit set.
   *
   * \returns true if this address is locally administered.
   */
  bool isLocallyAdministered() const { return getByte(0) & 0x2; }

  // Comparison operators.

  /// Return true if the two MAC addresses are equal.
  ///
  /// \param other The MAC address to compare against.
  /// \returns true if the addresses are equal.
  bool operator==(const MacAddress& other) const {
    // All constructors and modifying methods make sure padding is 0,
    // so we don't need to mask these bytes out when comparing here.
    return packedBytes() == other.packedBytes();
  }

  /// Return true if the two MAC addresses are not equal.
  ///
  /// \param other The MAC address to compare against.
  /// \returns true if the addresses differ.
  bool operator!=(const MacAddress& other) const { return !(*this == other); }

  /// Order two MAC addresses by their host-byte-order value.
  ///
  /// \param other The MAC address to compare against.
  /// \returns The ordering of this address relative to `other`.
  auto operator<=>(const MacAddress& other) const noexcept {
    return u64HBO() <=> other.u64HBO();
  }

 private:
  explicit MacAddress(uint64_t valueNBO) {
    memcpy(&bytes_, &valueNBO, 8);
    // Set the pad bytes to 0.
    // This allows us to easily compare two MacAddresses,
    // without having to worry about differences in the padding.
    bytes_[0] = 0;
    bytes_[1] = 0;
  }

  template <typename OnError>
  Expected<Unit, MacAddressFormatError> setFromString(
      StringPiece value, OnError err);

  template <typename OnError>
  Expected<Unit, MacAddressFormatError> setFromBinary(
      ByteRange value, OnError err);

  /* We store the 6 bytes starting at bytes_[2] (most significant)
     through bytes_[7] (least).
     bytes_[0] and bytes_[1] are always equal to 0 to simplify comparisons.
  */
  unsigned char bytes_[8];

  inline uint64_t getByte(size_t index) const { return bytes_[index + 2]; }

  uint64_t packedBytes() const {
    uint64_t u64;
    memcpy(&u64, bytes_, 8);
    return u64;
  }
};

/** Define toAppend() so to<string> will work
 *
 * \param address The MAC address to append.
 * \param result The string to append the address to.
 */
template <class Tgt>
typename std::enable_if<IsSomeString<Tgt>::value>::type toAppend(
    MacAddress address, Tgt* result) {
  toAppend(address.toString(), result);
}

/// Write a human-readable representation of the MAC address to a stream.
///
/// \param os The output stream to write to.
/// \param address The MAC address to write.
/// \returns The output stream `os`.
std::ostream& operator<<(std::ostream& os, MacAddress address);

} // namespace folly

/// Standard library namespace, reopened to specialize std::hash for MacAddress.
namespace std {

/// Specialization of std::hash for folly::MacAddress.
template <>
struct hash<folly::MacAddress> {
  /// Return a hash value for the given MAC address.
  ///
  /// \param address The MAC address to hash.
  /// \returns A hash value for `address`.
  size_t operator()(const folly::MacAddress& address) const {
    return std::hash<uint64_t>()(address.u64HBO());
  }
};

} // namespace std
