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

#include <memory>

#include <folly/SocketAddress.h>
#include <folly/futures/Future.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/io/async/IoUringBase.h>
#include <folly/net/NetworkSocket.h>

/// Facebook Folly library namespace.
namespace folly {

/// The libevent-based event loop that drives asynchronous operations.
class EventBase;
/// The io_uring-based EventBase backend.
class IoUringBackend;

/// Receives notifications about the progress of an io_uring receive operation.
class IoUringRecvCallback {
 public:
  /// Destroys the callback.
  virtual ~IoUringRecvCallback() = default;

  /// Called when data has been received.
  ///
  /// \param data The buffer holding the received data.
  virtual void recvSuccess(std::unique_ptr<IOBuf> data) = 0;
  /// Called when the peer closes the connection.
  virtual void recvEOF() noexcept = 0;
  /// Called when the receive fails.
  ///
  /// \param err The errno-style error code for the failure.
  /// \param exception The exception describing the failure, if any.
  virtual void recvErr(
      int err,
      std::unique_ptr<const AsyncSocketException> exception) noexcept = 0;
};

/// Manages inbound receives over an io_uring-backed socket.
class IoUringRecvHandle : public DelayedDestruction {
 public:
  /// Owning pointer type that respects DelayedDestruction semantics.
  using UniquePtr = std::unique_ptr<IoUringRecvHandle, Destructor>;
  /// A future that resolves to a received buffer.
  using PendingRead = SemiFuture<std::unique_ptr<IOBuf>>;
  /// An optional pending read, empty when no read is outstanding.
  using PendingReadMaybe = Optional<PendingRead>;

  /// Creates a receive handle bound to the given socket.
  ///
  /// \param evb The EventBase that drives the operation.
  /// \param fd The socket to receive from.
  /// \param addr The peer address.
  /// \param callback The callback notified of receive progress.
  /// \returns An owning pointer to the new receive handle.
  static UniquePtr create(
      EventBase* evb,
      NetworkSocket fd,
      const SocketAddress& addr,
      IoUringRecvCallback* callback);

  /// Creates a new receive handle that takes over the state of another.
  ///
  /// \param evb The EventBase that drives the new handle.
  /// \param fd The socket to receive from.
  /// \param addr The peer address.
  /// \param callback The callback notified of receive progress.
  /// \param old The handle whose state is transferred.
  /// \returns An owning pointer to the new receive handle.
  static UniquePtr clone(
      EventBase* evb,
      NetworkSocket fd,
      const SocketAddress& addr,
      IoUringRecvCallback* callback,
      UniquePtr old);

  /// Updates the event flags the handle is registered for.
  ///
  /// \param eventFlags The new set of event flags.
  /// \returns `true` if the registration changed.
  bool update(uint16_t eventFlags);
  /// Submits a receive request to the io_uring ring.
  void submit();
  /// Delivers any reads that have already completed.
  void drainCompletedReads();

  /// Detaches the handle from its current EventBase.
  void detachEventBase();
  /// Cancels any outstanding receive.
  void cancel();

 private:
  explicit IoUringRecvHandle(
      IoUringBackend* backend,
      NetworkSocket fd,
      const SocketAddress& addr,
      IoUringRecvCallback* callback);

  explicit IoUringRecvHandle(
      EventBase* evb,
      IoUringBackend* backend,
      NetworkSocket fd,
      const SocketAddress& addr,
      IoUringRecvCallback* callback,
      UniquePtr other);

  void setPendingRead(EventBase* evb, PendingRead&& future);
  void processPendingRead();
  void appendQueuedData(std::unique_ptr<IOBuf> data);
  void ensureRecvArmed();

  class RecvRequest;
  void onRecvComplete(std::unique_ptr<IOBuf> data);
  void onEnobufs();
  void onRecvEOF();
  void onRecvErr(int err);

  IoUringBackend* backend_;
  IoUringRecvCallback* recvCallback_;
  std::unique_ptr<RecvRequest, DelayedDestruction::Destructor> request_;

  std::unique_ptr<IOBuf> queuedReceivedData_;
  bool readEnabled_{false};
  PendingReadMaybe pendingRead_{folly::none};
};

} // namespace folly
