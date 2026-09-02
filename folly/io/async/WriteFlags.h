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

#include <cstdint>

/// Facebook Folly library namespace.
namespace folly {

/// Flags given by the application for write* calls.
enum class WriteFlags : uint32_t {
  NONE = 0x00, ///< No flags set.
  /// Whether to delay the output until a subsequent non-corked write.
  ///
  /// (Note: may not be supported in all subclasses or on all platforms.)
  CORK = 0x01,
  /// Set MSG_EOR flag when writing the last byte of the buffer to the socket.
  ///
  /// EOR tracking may need to be enabled to ensure that the MSG_EOR flag is only
  /// set when the final byte is being written.
  ///
  ///  - If the MSG_EOR flag is set, it is marked in the corresponding
  ///    tcp_skb_cb; this can be useful when debugging.
  ///  - The kernel uses it to decide whether socket buffers can be collapsed
  ///    together (see tcp_skb_can_collapse_to).
  EOR = 0x02,
  /// Indicates that only the write side of socket should be shutdown.
  WRITE_SHUTDOWN = 0x04,
  /// Use msg zerocopy if allowed.
  WRITE_MSG_ZEROCOPY = 0x08,
  /// Request timestamp when entire buffer transmitted by the NIC.
  ///
  /// How timestamping is performed is implementation specific and may rely on
  /// software or hardware timestamps.
  TIMESTAMP_TX = 0x10,
  /// Request timestamp when entire buffer ACKed by remote endpoint.
  ///
  /// How timestamping is performed is implementation specific and may rely on
  /// software or hardware timestamps.
  TIMESTAMP_ACK = 0x20,
  /// Request timestamp when entire buffer has entered packet scheduler.
  TIMESTAMP_SCHED = 0x40,
  /// Request timestamp when entire buffer has been written to system socket.
  TIMESTAMP_WRITE = 0x80,
};

/// Computes the union of two flag sets.
/// \param a First flag set.
/// \param b Second flag set.
/// \returns The bitwise union of \p a and \p b.
constexpr WriteFlags operator|(WriteFlags a, WriteFlags b) {
  return static_cast<WriteFlags>(
      static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/// Unions \p b into \p a in place.
/// \param a Flag set to update.
/// \param b Flags to add.
/// \returns A reference to \p a after the union.
constexpr WriteFlags& operator|=(WriteFlags& a, WriteFlags b) {
  a = a | b;
  return a;
}

/// Computes the intersection of two flag sets.
/// \param a First flag set.
/// \param b Second flag set.
/// \returns The bitwise intersection of \p a and \p b.
constexpr WriteFlags operator&(WriteFlags a, WriteFlags b) {
  return static_cast<WriteFlags>(
      static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/// Intersects \p a with \p b in place.
/// \param a Flag set to update.
/// \param b Flags to keep.
/// \returns A reference to \p a after the intersection.
constexpr WriteFlags& operator&=(WriteFlags& a, WriteFlags b) {
  a = a & b;
  return a;
}

/// Computes the complement of a flag set.
/// \param a Flag set to negate.
/// \returns The bitwise complement of \p a.
constexpr WriteFlags operator~(WriteFlags a) {
  return static_cast<WriteFlags>(~static_cast<uint32_t>(a));
}

/// Clears the flags of \p b from \p a.
/// \param a Source flag set.
/// \param b Flags to remove.
/// \returns \p a with the flags in \p b cleared.
constexpr WriteFlags unSet(WriteFlags a, WriteFlags b) {
  return a & ~b;
}

/// Tests whether all flags of \p b are set in \p a.
/// \param a Flag set to test.
/// \param b Flags to look for.
/// \returns True if every flag in \p b is set in \p a.
constexpr bool isSet(WriteFlags a, WriteFlags b) {
  return (a & b) == b;
}

/**
 * Write flags that are related to timestamping.
 */
constexpr WriteFlags kWriteFlagsForTimestamping = WriteFlags::TIMESTAMP_SCHED |
    WriteFlags::TIMESTAMP_TX | WriteFlags::TIMESTAMP_ACK;

} // namespace folly
