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

#include <utility>
#include <vector>

#include <folly/futures/Future.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/io/async/IoUringBase.h>
#include <folly/io/async/WriteCallbackWithState.h>
#include <folly/net/NetworkSocket.h>

/// Facebook Folly library namespace.
namespace folly {

/// Receives notifications about the progress of an io_uring send operation.
class IoUringSendCallback {
 public:
  /// Destroys the callback.
  virtual ~IoUringSendCallback() = default;

  /// Called when a send completes only part of the requested bytes.
  ///
  /// \param bytesWritten The number of bytes written so far.
  virtual void sendPartial(size_t bytesWritten) = 0;
  /// Called when the send completes successfully.
  ///
  /// \param bytesWritten The total number of bytes written.
  virtual void sendDone(size_t bytesWritten) = 0;
  /// Called when the send fails.
  ///
  /// \param err The errno-style error code for the failure.
  virtual void sendErr(int err) = 0;
  /// Releases ownership of a buffer that was queued for sending.
  ///
  /// \param buf The buffer being released.
  /// \param callback The callback to notify once the buffer is released.
  virtual void releaseIOBuf(
      std::unique_ptr<IOBuf> buf,
      AsyncWriter::ReleaseIOBufCallback* callback) = 0;
  /// Detaches a buffer from the send operation without releasing it.
  ///
  /// \param buf The buffer to detach.
  virtual void detachIOBuf(const IOBuf& buf) = 0;
};

/// The libevent-based event loop that drives asynchronous operations.
class EventBase;
/// The io_uring-based EventBase backend.
class IoUringBackend;

/// Manages a single outbound send over an io_uring-backed socket.
class IoUringSendHandle : public DelayedDestruction {
 public:
  /// Owning pointer type that respects DelayedDestruction semantics.
  using UniquePtr = std::unique_ptr<IoUringSendHandle, Destructor>;
  /// A list of per-buffer send results paired with their flags.
  using VecResFlags = std::vector<std::pair<int, uint32_t>>;

  /// Creates a send handle bound to the given socket.
  ///
  /// \param evb The EventBase that drives the operation.
  /// \param fd The socket to send on.
  /// \param addr The destination address.
  /// \param callback The callback notified of send progress.
  /// \returns An owning pointer to the new send handle.
  static UniquePtr create(
      EventBase* evb,
      NetworkSocket fd,
      const SocketAddress& addr,
      IoUringSendCallback* callback);

  /// Creates a new send handle that takes over the state of another.
  ///
  /// \param evb The EventBase that drives the new handle.
  /// \param other The handle whose state is transferred.
  /// \returns An owning pointer to the new send handle.
  static UniquePtr clone(EventBase* evb, IoUringSendHandle::UniquePtr other);
  /// Detaches the handle from its current EventBase.
  void detachEventBase();

  /// Updates the event flags the handle is registered for.
  ///
  /// \param eventFlags The new set of event flags.
  /// \returns `true` if the registration changed.
  bool update(uint16_t eventFlags);
  /// Queues a buffer to be sent on the socket.
  ///
  /// \param callback The write callback and its associated state.
  /// \param iov The array of buffers to write.
  /// \param iovCount The number of entries in `iov`.
  /// \param partialWritten The number of bytes already written from a prior partial send.
  /// \param bytesWritten The total number of bytes written so far.
  /// \param data The buffer that owns the memory referenced by `iov`.
  /// \param flags The write flags controlling the send.
  void write(
      WriteCallbackWithState callback,
      const struct iovec* iov,
      size_t iovCount,
      size_t partialWritten,
      size_t bytesWritten,
      std::unique_ptr<IOBuf> data,
      WriteFlags flags);

  /// Fails the current write with the given exception.
  ///
  /// \param ex The exception describing the failure.
  void failWrite(const AsyncSocketException& ex);
  /// Fails all pending writes with the given exception.
  ///
  /// \param ex The exception describing the failure.
  void failAllWrites(const AsyncSocketException& ex);

  /// Reports whether there are no pending send requests.
  ///
  /// \returns `true` if no send request is queued.
  bool empty() { return requestHead_ == nullptr; }

 private:
  explicit IoUringSendHandle(
      EventBase* evb,
      IoUringBackend* backend,
      NetworkSocket fd,
      const SocketAddress& addr,
      IoUringSendCallback* callback);

  explicit IoUringSendHandle(
      EventBase* evb, IoUringBackend* backend, UniquePtr other);

  void trySubmit();

  class SendRequest;
  void onSendStarted();
  void onSendPartial(size_t bytesWritten);
  void onSendComplete(size_t bytesWritten);
  void onSendErr(int err);
  void onReleaseIOBuf(
      std::unique_ptr<IOBuf> data, AsyncWriter::ReleaseIOBufCallback* callback);

  EventBase* evb_;
  IoUringBackend* backend_;
  NetworkSocket fd_;
  SocketAddress addr_;
  IoUringSendCallback* sendCallback_;

  SendRequest* requestHead_{nullptr};
  SendRequest* requestTail_{nullptr};

  bool sendEnabled_{false};
  Optional<SemiFuture<VecResFlags>> detachedFuture_;
};

} // namespace folly
