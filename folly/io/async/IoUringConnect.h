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

#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/IoUringBase.h>
#include <folly/net/NetworkSocket.h>

/// Facebook Folly library namespace.
namespace folly {

/// Receives notifications about the outcome of an io_uring connect operation.
class IoUringConnectCallback {
 public:
  /// Destroys the callback.
  virtual ~IoUringConnectCallback() = default;

  /// Called when the connection is established.
  virtual void connectSuccess() = 0;
  /// Called when the connection attempt times out.
  virtual void connectTimeout() = 0;
};

/// The libevent-based event loop that drives asynchronous operations.
class EventBase;
/// The io_uring-based EventBase backend.
class IoUringBackend;

/// Performs an asynchronous connect over an io_uring-backed socket.
class IoUringConnectHandle : public IoSqeBase, public AsyncTimeout {
 private:
  struct NotPubliclyConstructible {};

 public:
  /// Owning pointer type for a connect handle.
  using UniquePtr = std::unique_ptr<IoUringConnectHandle>;

  /// Creates a connect handle and begins the connect attempt.
  ///
  /// \param evb The EventBase that drives the operation.
  /// \param fd The socket to connect.
  /// \param callback The callback notified of the connect outcome.
  /// \param timeout The maximum time to wait for the connection.
  /// \returns An owning pointer to the new connect handle.
  static IoUringConnectHandle::UniquePtr create(
      EventBase* evb,
      NetworkSocket fd,
      IoUringConnectCallback* callback,
      std::chrono::milliseconds timeout);

  /// Constructs a connect handle; use create() instead.
  ///
  /// \param tag Tag that restricts public construction.
  /// \param evb The EventBase that drives the operation.
  /// \param backend The io_uring backend that submits the operation.
  /// \param fd The socket to connect.
  /// \param callback The callback notified of the connect outcome.
  IoUringConnectHandle(
      NotPubliclyConstructible tag,
      EventBase* evb,
      IoUringBackend* backend,
      NetworkSocket fd,
      IoUringConnectCallback* callback);

  /*
   * IoSqeBase
   */
  /// Fills the submission queue entry for the connect operation.
  ///
  /// \param sqe The io_uring submission queue entry to populate.
  void processSubmit(struct io_uring_sqe* sqe) noexcept override;
  /// Handles completion of the connect operation.
  ///
  /// \param cqe The io_uring completion queue entry for this operation.
  void callback(const struct io_uring_cqe* cqe) noexcept override;
  /// Handles completion after the connect operation was cancelled.
  ///
  /// \param cqe The io_uring completion queue entry for this operation.
  void callbackCancelled(const io_uring_cqe* cqe) noexcept override;

  /*
   * AsyncTimeout
   */
  /// Called when the connect timeout fires.
  void timeoutExpired() noexcept override;

  /// Cancels the pending connect attempt.
  ///
  /// \returns `true` if the connect was cancelled.
  bool cancel();

 private:
  IoUringBackend* backend_;
  NetworkSocket fd_;
  IoUringConnectCallback* callback_;
};

} // namespace folly
