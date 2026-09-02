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

#include <folly/SocketAddress.h>
#include <folly/io/IOBuf.h>
#include <folly/io/SocketOptionMap.h>
#include <folly/io/async/AsyncSocketException.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/net/NetworkSocket.h>

namespace folly {

/// Abstract asynchronous transport backed by a socket.
class AsyncSocketTransport : public AsyncTransport {
 public:
  /// Owning pointer type that respects the transport's deferred destructor.
  using UniquePtr = std::unique_ptr<AsyncSocketTransport, Destructor>;

  /// Enables or disables Nagle's algorithm on the socket.
  ///
  /// \param noDelay Whether to disable Nagle's algorithm.
  /// \returns Zero on success, or a nonzero error code on failure.
  virtual int setNoDelay(bool noDelay) = 0;
  /// Sets a socket option on the underlying socket.
  ///
  /// \param level The protocol level at which the option resides.
  /// \param optname The option name to set.
  /// \param optval Pointer to the buffer holding the option value.
  /// \param optsize The size of the option value buffer, in bytes.
  /// \returns Zero on success, or a nonzero error code on failure.
  virtual int setSockOpt(
      int level, int optname, const void* optval, socklen_t optsize) = 0;
  /// Provides data to be treated as if it had already been received.
  ///
  /// \param data The buffer of pre-received data.
  virtual void setPreReceivedData(std::unique_ptr<IOBuf> data) = 0;
  /// Caches the local and peer addresses of the socket.
  virtual void cacheAddresses() = 0;

  /// Callback invoked on the outcome of a connection attempt.
  class ConnectCallback {
   public:
    /// Destroys the callback.
    virtual ~ConnectCallback() = default;

    /**
     * connectSuccess() will be invoked when the connection has been
     * successfully established.
     */
    virtual void connectSuccess() noexcept = 0;

    /**
     * connectErr() will be invoked if the connection attempt fails.
     *
     * @param ex        An exception describing the error that occurred.
     */
    virtual void connectErr(const AsyncSocketException& ex) noexcept = 0;

    /**
     * preConnect() will be invoked just before the actual connect happens,
     *              default is no-ops.
     *
     * @param fd      An underneath created socket, use for connection.
     *
     */
    virtual void preConnect(NetworkSocket fd) {}
  };

  /// Returns a wildcard address usable as a default bind target.
  ///
  /// \returns A reference to the wildcard socket address.
  static const folly::SocketAddress& anyAddress();

  /**
   * Options for binding a socket before connecting. The two alternatives are
   * mutually exclusive:
   *  - SocketAddress: bind to a local address before connecting.
   *  - NetworkSocket: use a pre-bound (or pre-created) socket fd. Ownership
   *    of the fd is transferred to the AsyncSocketTransport.
   */
  using BindOptions = std::variant<folly::SocketAddress, NetworkSocket>;

  /// Initiates an asynchronous connection to the given address.
  ///
  /// \param callback The callback invoked when the connection succeeds or fails.
  /// \param address The remote address to connect to.
  /// \param timeout The connection timeout in milliseconds, or zero for none.
  /// \param options Socket options to apply before connecting.
  /// \param bindOptions Local bind options or a pre-bound socket to use.
  /// \param ifName The name of the network interface to bind to, if any.
  virtual void connect(
      ConnectCallback* callback,
      const folly::SocketAddress& address,
      int timeout = 0,
      SocketOptionMap const& options = emptySocketOptionMap,
      const BindOptions& bindOptions = anyAddress(),
      const std::string& ifName = "") noexcept = 0;

  /// Returns whether the peer has hung up the connection.
  ///
  /// \returns True if the peer has hung up, false otherwise.
  virtual bool hangup() const = 0;

  /**
   * If a connect request is in-flight, cancels it and closes the socket
   * immediately. Otherwise, this is a no-op.
   *
   * This does not invoke any connection related callbacks. Call this to
   * prevent any connect callback while cleaning up, etc.
   */
  virtual void cancelConnect() = 0;
  /// Stores the peer's certificate for later retrieval.
  ///
  /// \param cert The peer certificate to cache.
  void setPeerCertificate(
      std::unique_ptr<const AsyncTransportCertificate> cert) {
    peerCertData_ = std::move(cert);
  }

  /// Returns the peer's certificate, or nullptr if none is set.
  ///
  /// \returns The cached peer certificate, or nullptr if none is set.
  const AsyncTransportCertificate* getPeerCertificate() const override {
    return peerCertData_.get();
  }

  /// Releases the cached peer certificate.
  void dropPeerCertificate() noexcept override { peerCertData_.reset(); }

  /// Stores this endpoint's certificate for later retrieval.
  ///
  /// \param cert The self certificate to cache.
  void setSelfCertificate(
      std::unique_ptr<const AsyncTransportCertificate> cert) {
    selfCertData_ = std::move(cert);
  }

  /// Releases the cached self certificate.
  void dropSelfCertificate() noexcept override { selfCertData_.reset(); }

  /// Returns this endpoint's certificate, or nullptr if none is set.
  ///
  /// \returns The cached self certificate, or nullptr if none is set.
  const AsyncTransportCertificate* getSelfCertificate() const override {
    return selfCertData_.get();
  }

  /// Returns the NAPI id associated with the socket.
  ///
  /// \returns The NAPI id, or zero if none is associated.
  int getNapiId() const override;

  /// Returns the underlying network socket handle.
  ///
  /// \returns The underlying network socket.
  virtual NetworkSocket getNetworkSocket() const = 0;
  /// Returns whether TCP Fast Open succeeded for this connection.
  ///
  /// \returns True if TCP Fast Open succeeded, false otherwise.
  virtual bool getTFOSucceeded() const = 0;
  /// Enables TCP Fast Open on the socket.
  virtual void enableTFO() = 0;
  /// Disables transparent TLS handling on the socket.
  virtual void disableTransparentTls() {}

 protected:
  /// Destroys the transport.
  ~AsyncSocketTransport() override = default;

  /// Cached peer certificate; subclasses may populate it on first access.
  mutable std::unique_ptr<const AsyncTransportCertificate> peerCertData_{
      nullptr};
  /// Cached self certificate; subclasses may populate it on first access.
  mutable std::unique_ptr<const AsyncTransportCertificate> selfCertData_{
      nullptr};
};

} // namespace folly
