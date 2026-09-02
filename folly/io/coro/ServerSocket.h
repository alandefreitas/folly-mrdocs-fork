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

#include <optional>

#include <folly/ExceptionWrapper.h>
#include <folly/Expected.h>
#include <folly/SocketAddress.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/coro/Transport.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

/// A coroutine server socket that accepts connections on the same event base
/// as the underlying socket itself.
class ServerSocket {
 public:
  /// Constructs a server socket wrapping an async server socket.
  ///
  /// \param socket The underlying async server socket.
  /// \param bindAddr The address to bind to, if any.
  /// \param listenQueueDepth The depth of the listen queue.
  ServerSocket(
      std::shared_ptr<AsyncServerSocket> socket,
      std::optional<SocketAddress> bindAddr,
      uint32_t listenQueueDepth);

  /// Move constructor.
  ///
  /// \param other The server socket to move from.
  ServerSocket(ServerSocket&& other) = default;

  /// Move assignment operator.
  ///
  /// \param other The server socket to move-assign from.
  /// \returns A reference to this server socket.
  ServerSocket& operator=(ServerSocket&& other) = default;

  /// Destructor.
  ~ServerSocket() = default;

  /// Accepts the next incoming connection.
  ///
  /// \returns A task yielding a transport for the accepted connection.
  Task<std::unique_ptr<Transport>> accept();

  /// Stops accepting new connections.
  void close() noexcept {
    if (socket_) {
      socket_->stopAccepting();
    }
  }

  /// Returns the underlying async server socket.
  ///
  /// \returns A pointer to the underlying async server socket.
  const AsyncServerSocket* getAsyncServerSocket() const {
    return socket_.get();
  }

 private:
  // non-copyable
  ServerSocket(const ServerSocket&) = delete;
  ServerSocket& operator=(const ServerSocket&) = delete;

  std::shared_ptr<AsyncServerSocket> socket_;
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
