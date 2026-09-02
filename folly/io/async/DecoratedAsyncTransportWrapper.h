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

#include <folly/io/async/AsyncTransport.h>

namespace folly {

/**
 * Convenience class so that AsyncTransport can be decorated without
 * having to redefine every single method.
 */
template <class T>
class DecoratedAsyncTransportWrapper : public folly::AsyncTransport {
 public:
  /// Construct the wrapper, taking ownership of the transport to decorate.
  ///
  /// \param transport The transport to wrap.
  explicit DecoratedAsyncTransportWrapper(typename T::UniquePtr transport)
      : transport_(std::move(transport)) {
    if (FOLLY_LIKELY(nullptr != transport_)) {
      transport_->decoratingTransport_ = this;
    }
  }

  /// Return the wrapped transport.
  ///
  /// \returns The wrapped transport.
  const AsyncTransport* getWrappedTransport() const override {
    return transport_.get();
  }

  // folly::AsyncTransport
  /// Return the read callback of the wrapped transport.
  ///
  /// \returns The installed read callback.
  ReadCallback* getReadCallback() const override {
    return transport_->getReadCallback();
  }

  /// Install a read callback on the wrapped transport.
  ///
  /// \param callback The callback to install.
  void setReadCB(folly::AsyncTransport::ReadCallback* callback) override {
    transport_->setReadCB(callback);
  }

  /// Forward a write to the wrapped transport.
  ///
  /// \param callback Callback notified when the write completes or fails.
  /// \param buf Pointer to the data to write.
  /// \param bytes Number of bytes to write.
  /// \param flags Flags controlling how the data is written.
  void write(
      folly::AsyncTransport::WriteCallback* callback,
      const void* buf,
      size_t bytes,
      folly::WriteFlags flags = folly::WriteFlags::NONE) override {
    transport_->write(callback, buf, bytes, flags);
  }

  /// Forward a chained write to the wrapped transport.
  ///
  /// \param callback Callback notified when the write completes or fails.
  /// \param buf Chain of IOBufs to write.
  /// \param flags Flags controlling how the data is written.
  void writeChain(
      folly::AsyncTransport::WriteCallback* callback,
      std::unique_ptr<folly::IOBuf>&& buf,
      folly::WriteFlags flags = folly::WriteFlags::NONE) override {
    transport_->writeChain(callback, std::move(buf), flags);
  }

  /// Forward a vectored write to the wrapped transport.
  ///
  /// \param callback Callback notified when the write completes or fails.
  /// \param vec Array of iovec entries describing the data to write.
  /// \param bytes Number of entries in vec.
  /// \param flags Flags controlling how the data is written.
  void writev(
      folly::AsyncTransport::WriteCallback* callback,
      const iovec* vec,
      size_t bytes,
      folly::WriteFlags flags = folly::WriteFlags::NONE) override {
    transport_->writev(callback, vec, bytes, flags);
  }

  // folly::AsyncSocketBase
  /// Return the EventBase of the wrapped transport.
  ///
  /// \returns The associated EventBase.
  folly::EventBase* getEventBase() const override {
    return transport_->getEventBase();
  }

  // folly::AsyncTransport
  /// Attach the wrapped transport to the given EventBase.
  ///
  /// \param eventBase The EventBase to attach to.
  void attachEventBase(folly::EventBase* eventBase) override {
    transport_->attachEventBase(eventBase);
  }

  /// Close the wrapped transport.
  void close() override { transport_->close(); }

  /// Close the wrapped transport immediately.
  void closeNow() override { transport_->closeNow(); }

  /// Reset the wrapped transport, sending a reset to the peer if possible.
  void closeWithReset() override {
    transport_->closeWithReset();

    // This will likely result in 2 closeNow() calls on the decorated transport,
    // but otherwise it is very easy to miss the derived class's closeNow().
    closeNow();
  }

  /// Return whether the wrapped transport is connecting.
  ///
  /// \returns True if the transport is connecting.
  bool connecting() const override { return transport_->connecting(); }

  /// Detach the wrapped transport from its EventBase.
  void detachEventBase() override { transport_->detachEventBase(); }

  /// Return whether the wrapped transport is in an error state.
  ///
  /// \returns True if an error has occurred.
  bool error() const override { return transport_->error(); }

  /// Return the number of application-level bytes received.
  ///
  /// \returns The number of application-level bytes received.
  size_t getAppBytesReceived() const override {
    return transport_->getAppBytesReceived();
  }

  /// Return the number of application-level bytes written.
  ///
  /// \returns The number of application-level bytes written.
  size_t getAppBytesWritten() const override {
    return transport_->getAppBytesWritten();
  }

  /// Store the local address of the wrapped transport.
  ///
  /// \param address Receives the local address.
  void getLocalAddress(folly::SocketAddress* address) const override {
    return transport_->getLocalAddress(address);
  }

  /// Store the peer address of the wrapped transport.
  ///
  /// \param address Receives the peer address.
  void getPeerAddress(folly::SocketAddress* address) const override {
    return transport_->getPeerAddress(address);
  }

  /// Return the number of raw bytes received.
  ///
  /// \returns The number of raw bytes received.
  size_t getRawBytesReceived() const override {
    return transport_->getRawBytesReceived();
  }

  /// Return the number of raw bytes written.
  ///
  /// \returns The number of raw bytes written.
  size_t getRawBytesWritten() const override {
    return transport_->getRawBytesWritten();
  }

  /// Return the send timeout of the wrapped transport.
  ///
  /// \returns The send timeout, in milliseconds.
  uint32_t getSendTimeout() const override {
    return transport_->getSendTimeout();
  }

  /// Return whether the wrapped transport is open and ready.
  ///
  /// \returns True if the transport is good.
  bool good() const override { return transport_->good(); }

  /// Return whether the wrapped transport can be detached.
  ///
  /// \returns True if the transport can be detached.
  bool isDetachable() const override { return transport_->isDetachable(); }

  /// Return whether end-of-record tracking is enabled.
  ///
  /// \returns True if end-of-record tracking is enabled.
  bool isEorTrackingEnabled() const override {
    return transport_->isEorTrackingEnabled();
  }

  /// Return whether the wrapped transport is readable.
  ///
  /// \returns True if the transport is readable.
  bool readable() const override { return transport_->readable(); }

  /// Return whether the wrapped transport is writable.
  ///
  /// \returns True if the transport is writable.
  bool writable() const override { return transport_->writable(); }

  /// Enable or disable end-of-record tracking on the wrapped transport.
  ///
  /// \param track True to enable end-of-record tracking.
  void setEorTracking(bool track) override {
    return transport_->setEorTracking(track);
  }

  /// Set the send timeout on the wrapped transport.
  ///
  /// \param timeoutInMs Timeout in milliseconds, or 0 to disable.
  void setSendTimeout(uint32_t timeoutInMs) override {
    transport_->setSendTimeout(timeoutInMs);
  }

  /// Half-shutdown the write side of the wrapped transport.
  void shutdownWrite() override { transport_->shutdownWrite(); }

  /// Immediately half-shutdown the write side of the wrapped transport.
  void shutdownWriteNow() override { transport_->shutdownWriteNow(); }

  /// Return the application protocol of the wrapped transport.
  ///
  /// \returns The application protocol name.
  std::string getApplicationProtocol() const noexcept override {
    return transport_->getApplicationProtocol();
  }

  /// Return the security protocol of the wrapped transport.
  ///
  /// \returns The security protocol name.
  std::string getSecurityProtocol() const override {
    return transport_->getSecurityProtocol();
  }

  /// Produce exported keying material from the wrapped transport.
  ///
  /// \param label Label binding the exported keying material to a context.
  /// \param context Optional context mixed into the exported keying material.
  /// \param length Number of bytes of keying material to produce.
  /// \returns The exported keying material, or nullptr if unsupported.
  std::unique_ptr<IOBuf> getExportedKeyingMaterial(
      folly::StringPiece label,
      std::unique_ptr<IOBuf> context,
      uint16_t length) const override {
    return transport_->getExportedKeyingMaterial(
        label, std::move(context), length);
  }

  /// Return whether the wrapped transport has replay protection.
  ///
  /// \returns True if the transport is replay safe.
  bool isReplaySafe() const override { return transport_->isReplaySafe(); }

  /// Set the replay-safety callback on the wrapped transport.
  ///
  /// \param callback Callback invoked when the transport becomes replay safe.
  void setReplaySafetyCallback(
      folly::AsyncTransport::ReplaySafetyCallback* callback) override {
    transport_->setReplaySafetyCallback(callback);
  }

  /// Return the peer certificate of the wrapped transport.
  ///
  /// \returns The peer certificate, or nullptr if none.
  const AsyncTransportCertificate* getPeerCertificate() const override {
    return transport_->getPeerCertificate();
  }

  /// Drop the cached peer certificate of the wrapped transport.
  void dropPeerCertificate() noexcept override {
    transport_->dropPeerCertificate();
  }

  /// Return the self certificate of the wrapped transport.
  ///
  /// \returns The self certificate, or nullptr if none.
  const AsyncTransportCertificate* getSelfCertificate() const override {
    return transport_->getSelfCertificate();
  }

  /// Drop the cached self certificate of the wrapped transport.
  void dropSelfCertificate() noexcept override {
    transport_->dropSelfCertificate();
  }

  /// Enable or disable zero-copy writes on the wrapped transport.
  ///
  /// \param enable True to enable zero-copy writes.
  /// \returns True if the setting was applied.
  bool setZeroCopy(bool enable) override {
    return transport_->setZeroCopy(enable);
  }

  /// Return whether zero-copy writes are enabled.
  ///
  /// \returns True if zero-copy writes are enabled.
  bool getZeroCopy() const override { return transport_->getZeroCopy(); }

  /// Set the zero-copy enable predicate on the wrapped transport.
  ///
  /// \param func Predicate invoked to decide when a write uses zero-copy.
  void setZeroCopyEnableFunc(ZeroCopyEnableFunc func) override {
    transport_->setZeroCopyEnableFunc(func);
  }

  /// Set the zero-copy write threshold on the wrapped transport.
  ///
  /// \param threshold Minimum write size, in bytes, to use zero-copy writes.
  void setZeroCopyEnableThreshold(size_t threshold) override {
    transport_->setZeroCopyEnableThreshold(threshold);
  }

  /// Exchange the wrapped transport with the given one.
  ///
  /// \param transport Transport to install in place of the wrapped one.
  /// \returns The previously wrapped transport.
  AsyncTransport::UniquePtr tryExchangeWrappedTransport(
      AsyncTransport::UniquePtr& transport) override {
    if (transport_) {
      transport_->decoratingTransport_ = nullptr;
    }
    if (transport) {
      transport->decoratingTransport_ = this;
    }
    return std::exchange(transport_, std::move(transport));
  }

  /// Destroy this wrapper and its wrapped transport.
  void destroy() override {
    if (transport_) {
      transport_->decoratingTransport_ = nullptr;
    }
    folly::AsyncTransport::destroy();
  }

 protected:
  /// Destroy the wrapper.
  ~DecoratedAsyncTransportWrapper() override {}

  typename T::UniquePtr transport_; ///< The wrapped transport.
};

} // namespace folly
