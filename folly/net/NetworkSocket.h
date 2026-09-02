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

#include <ostream>

#include <folly/net/detail/SocketFileDescriptorMap.h>
#include <folly/portability/Windows.h>

/// The Folly library namespace.
namespace folly {
/**
 * NetworkSocket is just a very thin wrapper around either a file descriptor or
 * a SOCKET depending on platform, along with a couple of helper methods
 * for explicitly converting to/from file descriptors, even on Windows.
 *
 * @struct folly::NetworkSocket
 */
struct NetworkSocket {
#ifdef _WIN32
  using native_handle_type = SOCKET;
  static constexpr native_handle_type invalid_handle_value = INVALID_SOCKET;
#else
  /// The platform-specific native socket handle type.
  using native_handle_type = int;
  /// The sentinel value representing an invalid socket handle.
  static constexpr native_handle_type invalid_handle_value = -1;
#endif

  /// The underlying native socket handle.
  native_handle_type data;

  /// Constructs a NetworkSocket holding the invalid handle value.
  constexpr NetworkSocket() : data(invalid_handle_value) {}
  /// Constructs a NetworkSocket wrapping the given native handle.
  ///
  /// \param d The native socket handle to wrap.
  constexpr explicit NetworkSocket(native_handle_type d) : data(d) {}

  /// Deleted to prevent constructing from a handle of an unintended type.
  ///
  /// \param arg The rejected argument.
  /// \returns Never returns; this overload is deleted.
  template <typename T>
  static NetworkSocket fromFd(T arg) = delete;
  /**
   * Return underlying NetworkSocket handle associated with the file descriptor.
   *
   * @param fd The file descriptor
   *
   * @return Underlying platform specific NetworkSocket handle for the file
   * descriptor
   */
  static NetworkSocket fromFd(int fd) {
    return NetworkSocket(
        netops::detail::SocketFileDescriptorMap::fdToSocket(fd));
  }

  /**
   * Return the file descriptor associated with this NetworkSocket.
   *
   * @return The file descriptor associated with this NetworkSocket
   */
  int toFd() const {
    return netops::detail::SocketFileDescriptorMap::socketToFd(data);
  }

  /// Compares two NetworkSockets for equality.
  ///
  /// \param a The first socket.
  /// \param b The second socket.
  /// \returns true if both sockets wrap the same handle.
  friend constexpr bool operator==(
      const NetworkSocket& a, const NetworkSocket& b) noexcept {
    return a.data == b.data;
  }

  /// Compares two NetworkSockets for inequality.
  ///
  /// \param a The first socket.
  /// \param b The second socket.
  /// \returns true if the sockets wrap different handles.
  friend constexpr bool operator!=(
      const NetworkSocket& a, const NetworkSocket& b) noexcept {
    return !(a == b);
  }
};

/// Writes a textual representation of a NetworkSocket to a stream.
///
/// \param os The output stream to write to.
/// \param addr The NetworkSocket to format.
/// \returns The output stream.
template <class CharT, class Traits>
inline std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os, const NetworkSocket& addr) {
  os << "folly::NetworkSocket(" << addr.data << ")";
  return os;
}
} // namespace folly

/// Standard library customization points for folly::NetworkSocket.
namespace std {
/// Hash specialization for folly::NetworkSocket.
template <>
struct hash<folly::NetworkSocket> {
  /// Computes a hash value for a NetworkSocket.
  ///
  /// \param s The socket to hash.
  /// \returns The hash of the socket's underlying handle.
  size_t operator()(const folly::NetworkSocket& s) const noexcept {
    return std::hash<folly::NetworkSocket::native_handle_type>()(s.data);
  }
};
} // namespace std
