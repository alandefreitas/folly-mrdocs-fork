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

#include <functional>

#include <folly/Range.h>
#include <folly/SocketAddress.h>
#include <folly/coro/Task.h>
#include <folly/io/IOBufQueue.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncSocketException.h>
#include <folly/io/coro/TransportCallbackBase.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

//
// Handle connect for AsyncSocketTransport
//

/// Handles the connect operation for an AsyncSocketTransport.
class ConnectCallback
    : public TransportCallbackBase,
      public folly::AsyncSocketTransport::ConnectCallback {
 public:
  /// Constructs a connect callback for `socket`.
  ///
  /// \param socket Socket to perform the connect on.
  explicit ConnectCallback(folly::AsyncSocketTransport& socket)
      : TransportCallbackBase(socket), socket_(socket) {}

 private:
  void cancel() noexcept override { socket_.cancelConnect(); }

  void connectSuccess() noexcept override { post(); }

  void connectErr(const folly::AsyncSocketException& ex) noexcept override {
    storeException(ex);
    post();
  }
  folly::AsyncSocketTransport& socket_;
};

//
// Handle data read for AsyncTransport
//

/// Handles data reads for an AsyncTransport.
class ReadCallback
    : public TransportCallbackBase,
      public folly::AsyncTransport::ReadCallback,
      public folly::HHWheelTimer::Callback {
 public:
  /// Constructs a read callback that reads into a fixed byte range.
  ///
  /// The transport is passed in so the callback can clear the socket's
  /// callback pointer, preventing multiple callbacks in one event-loop run.
  ///
  /// \param timer Timer used to schedule the read timeout.
  /// \param transport Transport to read from.
  /// \param buf Destination buffer for the read bytes.
  /// \param timeout Maximum time to wait for data.
  ReadCallback(
      folly::HHWheelTimer& timer,
      folly::AsyncTransport& transport,
      folly::MutableByteRange buf,
      std::chrono::milliseconds timeout)
      : TransportCallbackBase(transport), buf_{buf}, timeout_(timeout) {
    scheduleTimeout(timer);
  }

  /// Constructs a read callback that reads into an IOBufQueue.
  ///
  /// \param timer Timer used to schedule the read timeout.
  /// \param transport Transport to read from.
  /// \param readBuf Queue that receives the read bytes.
  /// \param minReadSize Minimum number of bytes to allocate for the read.
  /// \param newAllocationSize Size of newly allocated buffers.
  /// \param timeout Maximum time to wait for data.
  ReadCallback(
      folly::HHWheelTimer& timer,
      folly::AsyncTransport& transport,
      folly::IOBufQueue* readBuf,
      size_t minReadSize,
      size_t newAllocationSize,
      std::chrono::milliseconds timeout)
      : TransportCallbackBase(transport),
        readBuf_(readBuf),
        minReadSize_(minReadSize),
        newAllocationSize_(newAllocationSize),
        timeout_(timeout) {
    scheduleTimeout(timer);
  }

  /// Schedules the read timeout on `timer` if a timeout is configured.
  ///
  /// \param timer Timer used to schedule the timeout.
  void scheduleTimeout(folly::HHWheelTimer& timer) {
    if (timeout_.count() > 0) {
      timer.scheduleTimeout(this, timeout_);
    }
  }

  /// Number of bytes read during the operation.
  size_t length{0};
  /// Whether end-of-file was reached.
  bool eof{false};

 private:
  // the read buffer we store to hand off to callback - obtained from user
  folly::MutableByteRange buf_;
  folly::IOBufQueue* readBuf_{nullptr};
  size_t minReadSize_{0};
  size_t newAllocationSize_{0};
  // initial timeout configured on ReadCallback
  std::chrono::milliseconds timeout_;

  void cancel() noexcept override {
    transport_.setReadCB(nullptr);
    cancelTimeout();
  }

  //
  // ReadCallback methods
  //
  bool isBufferMovable() noexcept override { return readBuf_; }

  void readBufferAvailable(
      std::unique_ptr<folly::IOBuf> readBuf) noexcept override {
    CHECK(readBuf_);
    readBuf_->append(std::move(readBuf));
    post();
  }

  // this is called right before readDataAvailable(), always
  // in the same sequence
  void getReadBuffer(void** buf, size_t* len) override {
    if (readBuf_) {
      auto rbuf = readBuf_->preallocate(minReadSize_, newAllocationSize_);
      *buf = rbuf.first;
      *len = rbuf.second;
    } else {
      VLOG(5) << "getReadBuffer, size: " << buf_.size();
      *buf = buf_.begin() + length;
      *len = buf_.size() - length;
    }
  }

  // once we get actual data, uninstall callback and clear timeout
  void readDataAvailable(size_t len) noexcept override {
    VLOG(5) << "readDataAvailable: " << len << " bytes";
    length += len;
    if (readBuf_) {
      readBuf_->postallocate(len);
    } else if (length == buf_.size()) {
      transport_.setReadCB(nullptr);
      cancelTimeout();
    }
    post();
  }

  void readEOF() noexcept override {
    VLOG(5) << "readEOF()";
    // disable callbacks
    transport_.setReadCB(nullptr);
    cancelTimeout();
    eof = true;
    post();
  }

  void readErr(const folly::AsyncSocketException& ex) noexcept override {
    VLOG(5) << "readErr()";
    // disable callbacks
    transport_.setReadCB(nullptr);
    cancelTimeout();
    storeException(ex);
    post();
  }

  //
  // AsyncTimeout method
  //

  void timeoutExpired() noexcept override {
    VLOG(5) << "timeoutExpired()";

    using Error = folly::AsyncSocketException::AsyncSocketExceptionType;

    // uninstall read callback. it takes another read to bring it back.
    transport_.setReadCB(nullptr);
    // If the timeout fires but this ReadCallback did get some data, ignore it.
    // post() has already happend from readDataAvailable.
    if (length == 0) {
      error_ = folly::make_exception_wrapper<folly::AsyncSocketException>(
          Error::TIMED_OUT, "Timed out waiting for data", errno);
      post();
    }
  }
};

//
// Handle data write for AsyncTransport
//

/// Handles data writes for an AsyncTransport.
class WriteCallback
    : public TransportCallbackBase,
      public folly::AsyncTransport::WriteCallback {
 public:
  /// Constructs a write callback for `transport`.
  ///
  /// \param transport Transport to write to.
  explicit WriteCallback(folly::AsyncTransport& transport)
      : TransportCallbackBase(transport) {}
  /// Destroys the write callback.
  ~WriteCallback() override = default;

  /// Number of bytes written.
  size_t bytesWritten{0};
  /// Error reported by the write, if any.
  std::optional<folly::AsyncSocketException> error;

 private:
  void cancel() noexcept override { transport_.closeWithReset(); }
  //
  // Methods of WriteCallback
  //

  void writeSuccess() noexcept override {
    VLOG(5) << "writeSuccess";
    post();
  }

  void writeErr(
      size_t bytes, const folly::AsyncSocketException& ex) noexcept override {
    VLOG(5) << "writeErr, wrote " << bytesWritten << " bytes";
    bytesWritten = bytes;
    error = ex;
    post();
  }
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
