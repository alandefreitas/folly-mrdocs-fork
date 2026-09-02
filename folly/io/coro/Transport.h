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

#include <folly/Range.h>
#include <folly/SocketAddress.h>
#include <folly/coro/Task.h>
#include <folly/io/IOBufQueue.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncSocketException.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
/// Coroutine-based asynchronous I/O primitives.
namespace coro {

/// Abstract interface for a coroutine-based socket transport.
class TransportIf {
 public:
  /// Type used to report socket errors.
  using ErrorCode = AsyncSocketException::AsyncSocketExceptionType;
  /// Destroys the transport.
  virtual ~TransportIf() = default;
  /// Returns the event base driving this transport.
  ///
  /// \returns The event base driving this transport.
  virtual EventBase* getEventBase() noexcept = 0;
  /// Reads up to `buf.size()` bytes into `buf`, waiting at most `timeout`.
  ///
  /// \param buf Destination buffer for the read bytes.
  /// \param timeout Maximum time to wait for data.
  /// \returns The number of bytes read.
  virtual Task<size_t> read(
      MutableByteRange buf, std::chrono::milliseconds timeout) = 0;
  /// Reads up to `buflen` bytes into `buf`, waiting at most `timeout`.
  ///
  /// \param buf Destination buffer for the read bytes.
  /// \param buflen Size of the destination buffer.
  /// \param timeout Maximum time to wait for data.
  /// \returns The number of bytes read.
  Task<size_t> read(
      void* buf, size_t buflen, std::chrono::milliseconds timeout) {
    return read(MutableByteRange((unsigned char*)buf, buflen), timeout);
  }
  /// Reads data into an IOBufQueue, waiting at most `timeout`.
  ///
  /// \param buf Queue that receives the read bytes.
  /// \param minReadSize Minimum number of bytes to allocate for the read.
  /// \param newAllocationSize Size of newly allocated buffers.
  /// \param timeout Maximum time to wait for data.
  /// \returns The number of bytes read.
  virtual Task<size_t> read(
      IOBufQueue& buf,
      size_t minReadSize,
      size_t newAllocationSize,
      std::chrono::milliseconds timeout) = 0;

  /// Result of a write, reporting how many bytes were written.
  struct WriteInfo {
    /// Number of bytes written before completion or error.
    size_t bytesWritten{0};
  };

  /// Writes the bytes in `buf`, waiting at most `timeout`.
  ///
  /// \param buf Bytes to write.
  /// \param timeout Maximum time to wait; zero means no timeout.
  /// \param writeFlags Flags controlling the write.
  /// \param writeInfo Optional out-parameter reporting bytes written.
  /// \returns A task that completes when the write finishes.
  virtual Task<Unit> write(
      ByteRange buf,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(0),
      folly::WriteFlags writeFlags = folly::WriteFlags::NONE,
      WriteInfo* writeInfo = nullptr) = 0;
  /// Writes the bytes queued in `ioBufQueue`, waiting at most `timeout`.
  ///
  /// \param ioBufQueue Queue of bytes to write.
  /// \param timeout Maximum time to wait; zero means no timeout.
  /// \param writeFlags Flags controlling the write.
  /// \param writeInfo Optional out-parameter reporting bytes written.
  /// \returns A task that completes when the write finishes.
  virtual Task<Unit> write(
      IOBufQueue& ioBufQueue,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(0),
      folly::WriteFlags writeFlags = folly::WriteFlags::NONE,
      WriteInfo* writeInfo = nullptr) = 0;

  /// Returns the local address of the socket.
  ///
  /// \returns The local address of the socket.
  virtual SocketAddress getLocalAddress() const noexcept = 0;

  /// Returns the remote address of the socket.
  ///
  /// \returns The remote address of the socket.
  virtual SocketAddress getPeerAddress() const noexcept = 0;
  /// Closes the transport.
  virtual void close() = 0;
  /// Shuts down the write side of the transport.
  virtual void shutdownWrite() = 0;
  /// Closes the transport by sending a reset (RST).
  virtual void closeWithReset() = 0;
  /// Returns the underlying async transport.
  ///
  /// \returns The underlying async transport.
  virtual folly::AsyncTransport* getTransport() const = 0;
  /// Returns the peer's certificate, if any.
  ///
  /// \returns The peer's certificate, or `nullptr` if none is available.
  virtual const AsyncTransportCertificate* getPeerCertificate() const = 0;
  /// Detaches the transport from its event base.
  virtual void detachEventBase() { LOG(FATAL) << "not implemented"; }
  /// Attaches the transport to the given event base.
  ///
  /// \param eventBase Event base to attach the transport to.
  virtual void attachEventBase(folly::EventBase* eventBase) {
    LOG(FATAL) << "not implemented";
  }
};

/// Coroutine transport backed by an AsyncTransport socket.
class Transport : public TransportIf {
 public:
  /// Constructs a transport wrapping `transport` on `eventBase`.
  ///
  /// \param eventBase Event base that drives this transport.
  /// \param transport Async transport socket to wrap.
  Transport(
      folly::EventBase* eventBase, folly::AsyncTransport::UniquePtr transport)
      : eventBase_(eventBase), transport_(std::move(transport)) {}

  /// Move-constructs a transport.
  ///
  /// \param other Transport to move from.
  Transport(Transport&& other) = default;
  /// Move-assigns a transport.
  ///
  /// \param other Transport to move from.
  /// \returns A reference to this transport.
  Transport& operator=(Transport&& other) = default;

  /// Establishes a TCP connection and returns a Transport wrapping the socket.
  ///
  /// \param evb Event base that drives the new connection.
  /// \param destAddr Address to connect to.
  /// \param connectTimeout Maximum time to wait for the connection.
  /// \param options Socket options applied to the connection.
  /// \param bindAddr Local address to bind before connecting.
  /// \param ifName Name of the interface to bind to.
  /// \returns A task producing the connected Transport.
  static Task<Transport> newConnectedSocket(
      EventBase* evb,
      const SocketAddress& destAddr,
      std::chrono::milliseconds connectTimeout,
      const SocketOptionMap& options = emptySocketOptionMap,
      const SocketAddress& bindAddr = AsyncSocketTransport::anyAddress(),
      const std::string& ifName = "");
  /// Returns the event base driving this transport.
  ///
  /// \returns The event base driving this transport.
  virtual EventBase* getEventBase() noexcept override { return eventBase_; }

  /// Reads up to `buf.size()` bytes into `buf`, waiting at most `timeout`.
  ///
  /// \param buf Destination buffer for the read bytes.
  /// \param timeout Maximum time to wait for data.
  /// \returns The number of bytes read.
  Task<size_t> read(
      MutableByteRange buf, std::chrono::milliseconds timeout) override;
  /// Reads data into an IOBufQueue, waiting at most `timeout`.
  ///
  /// \param buf Queue that receives the read bytes.
  /// \param minReadSize Minimum number of bytes to allocate for the read.
  /// \param newAllocationSize Size of newly allocated buffers.
  /// \param timeout Maximum time to wait for data.
  /// \returns The number of bytes read.
  Task<size_t> read(
      IOBufQueue& buf,
      size_t minReadSize,
      size_t newAllocationSize,
      std::chrono::milliseconds timeout) override;

  /// Writes the bytes in `buf`, waiting at most `timeout`.
  ///
  /// \param buf Bytes to write.
  /// \param timeout Maximum time to wait; zero means no timeout.
  /// \param writeFlags Flags controlling the write.
  /// \param writeInfo Optional out-parameter reporting bytes written.
  /// \returns A task that completes when the write finishes.
  Task<Unit> write(
      ByteRange buf,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(0),
      folly::WriteFlags writeFlags = folly::WriteFlags::NONE,
      WriteInfo* writeInfo = nullptr) override;
  /// Writes the bytes queued in `ioBufQueue`, waiting at most `timeout`.
  ///
  /// \param ioBufQueue Queue of bytes to write.
  /// \param timeout Maximum time to wait; zero means no timeout.
  /// \param writeFlags Flags controlling the write.
  /// \param writeInfo Optional out-parameter reporting bytes written.
  /// \returns A task that completes when the write finishes.
  Task<folly::Unit> write(
      IOBufQueue& ioBufQueue,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(0),
      folly::WriteFlags writeFlags = folly::WriteFlags::NONE,
      WriteInfo* writeInfo = nullptr) override;

  /// Returns the underlying async transport.
  ///
  /// \returns The underlying async transport.
  AsyncTransport* getTransport() const override { return transport_.get(); }

  /// Returns the local address of the socket.
  ///
  /// \returns The local address of the socket.
  SocketAddress getLocalAddress() const noexcept override {
    SocketAddress addr;
    transport_->getLocalAddress(&addr);
    return addr;
  }

  /// Returns the remote address of the socket.
  ///
  /// \returns The remote address of the socket.
  SocketAddress getPeerAddress() const noexcept override {
    SocketAddress addr;
    transport_->getPeerAddress(&addr);
    return addr;
  }

  /// Shuts down the write side of the transport.
  void shutdownWrite() noexcept override {
    if (transport_) {
      transport_->shutdownWrite();
    }
  }

  /// Closes the transport.
  void close() noexcept override {
    if (transport_) {
      transport_->close();
    }
  }

  /// Closes the transport by sending a reset (RST).
  void closeWithReset() noexcept override {
    if (transport_) {
      transport_->closeWithReset();
    }
  }

  /// Returns the peer's certificate, if any.
  ///
  /// \returns The peer's certificate, or `nullptr` if none is available.
  const AsyncTransportCertificate* getPeerCertificate() const override {
    return transport_->getPeerCertificate();
  }

 protected:
  /// Event base driving this transport.
  EventBase* eventBase_;
  /// Underlying async transport socket.
  AsyncTransport::UniquePtr transport_;

 private:
  // non-copyable
  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;

  bool deferredReadEOF_{false};
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
