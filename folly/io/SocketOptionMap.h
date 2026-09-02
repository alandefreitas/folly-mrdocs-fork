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

#include <folly/io/SocketOptionValue.h>
#include <folly/net/NetworkSocket.h>
#include <folly/portability/Sockets.h>

namespace folly {

/**
 * Uniquely identifies a handle to a socket option value. Each
 * combination of level and option name corresponds to one socket
 * option value.
 */
class SocketOptionKey {
 public:
  /// When a socket option should be applied relative to bind().
  enum class ApplyPos {
    POST_BIND = 0, ///< Apply the option after the socket is bound.
    PRE_BIND = 1 ///< Apply the option before the socket is bound.
  };

  /// Orders keys by level, then by option name.
  ///
  /// \param lhs The left-hand key to compare.
  /// \param rhs The right-hand key to compare.
  /// \returns `true` if `lhs` orders before `rhs`.
  friend bool operator<(
      const SocketOptionKey& lhs, const SocketOptionKey& rhs) {
    if (lhs.level == rhs.level) {
      return lhs.optname < rhs.optname;
    }
    return lhs.level < rhs.level;
  }

  /// Compares two keys by level and option name.
  ///
  /// \param lhs The left-hand key to compare.
  /// \param rhs The right-hand key to compare.
  /// \returns `true` if both keys have the same level and option name.
  friend bool operator==(
      const SocketOptionKey& lhs, const SocketOptionKey& rhs) {
    return lhs.level == rhs.level && lhs.optname == rhs.optname;
  }

  /// Applies this option with the given value to the socket.
  ///
  /// \param fd The socket to apply the option to.
  /// \param val The value to set for the option.
  /// \returns Zero on success, or a non-zero error code on failure.
  int apply(NetworkSocket fd, const SocketOptionValue& val) const;

  int level; ///< Protocol level of the option (e.g. SOL_SOCKET).
  int optname; ///< Option name within the level.
  ApplyPos applyPos_{ApplyPos::POST_BIND}; ///< When to apply the option.
};

/// Maps socket option keys to their values.
using SocketOptionMap = std::map<SocketOptionKey, SocketOptionValue>;

/// An empty socket option map.
extern const SocketOptionMap emptySocketOptionMap;

/// Maps socket option keys to integer control-message values.
using SocketCmsgMap = std::map<SocketOptionKey, int>;
/// Maps socket option keys to string control-message values.
using SocketNontrivialCmsgMap = std::map<SocketOptionKey, std::string>;

/// Applies the given socket options to a socket at the given position.
///
/// \param fd The socket to apply the options to.
/// \param options The socket options to apply.
/// \param pos Whether to apply options before or after bind().
/// \returns Zero on success, or a non-zero error code on failure.
int applySocketOptions(
    NetworkSocket fd,
    const SocketOptionMap& options,
    SocketOptionKey::ApplyPos pos);

/// Returns the subset of options that apply to the given family and position.
///
/// \param options The socket options to filter.
/// \param family The address family to match against (e.g. AF_INET).
/// \param pos Whether to select options applied before or after bind().
/// \returns The subset of options matching the given family and position.
SocketOptionMap validateSocketOptions(
    const SocketOptionMap& options,
    sa_family_t family,
    SocketOptionKey::ApplyPos pos);

} // namespace folly
