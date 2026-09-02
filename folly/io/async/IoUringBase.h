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

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/slist.hpp>
#include <folly/io/IOBuf.h>
#include <folly/io/async/DelayedDestruction.h>

/// A submission queue entry (SQE) from the liburing C API.
struct io_uring_sqe;
/// A completion queue entry (CQE) from the liburing C API.
struct io_uring_cqe;

/// Facebook Folly library namespace.
namespace folly {

/// The io_uring-based EventBase backend.
class IoUringBackend;
/// The libevent-based event loop that drives asynchronous operations.
class EventBase;

/// Base class for an io_uring submission queue entry managed by Folly.
///
/// Instances model a single asynchronous operation that is submitted to an
/// io_uring ring and later completed via a callback. Because io_uring stores
/// raw pointers to these objects, copying and moving are disabled.
struct IoSqeBase
    : boost::intrusive::list_base_hook<
          boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {
  /// The kind of operation an IoSqeBase represents.
  enum class Type {
    Unknown, ///< The operation kind is not specified.
    Read, ///< A read operation.
    Write, ///< A write operation.
    Open, ///< An open operation.
    Close, ///< A close operation.
    Connect, ///< A connect operation.
    Cancel, ///< A cancellation operation.
  };

  /// Constructs an entry with an unknown operation type.
  IoSqeBase() : IoSqeBase(Type::Unknown) {}
  /// Constructs an entry for the given operation type.
  ///
  /// \param type The kind of operation this entry represents.
  explicit IoSqeBase(Type type) : type_(type) {}
  // use raw addresses, so disallow copy/move
  /// Deleted move constructor; entries are pinned in memory.
  ///
  /// \param other The entry to move from.
  IoSqeBase(IoSqeBase&& other) = delete;
  /// Deleted copy constructor; entries are pinned in memory.
  ///
  /// \param other The entry to copy from.
  IoSqeBase(const IoSqeBase& other) = delete;
  /// Deleted move assignment; entries are pinned in memory.
  ///
  /// \param other The entry to move from.
  /// \returns A reference to this entry.
  IoSqeBase& operator=(IoSqeBase&& other) = delete;
  /// Deleted copy assignment; entries are pinned in memory.
  ///
  /// \param other The entry to copy from.
  /// \returns A reference to this entry.
  IoSqeBase& operator=(const IoSqeBase& other) = delete;

  /// Destroys the entry.
  virtual ~IoSqeBase() = default;
  /// Fills the given submission queue entry before it is submitted.
  ///
  /// \param sqe The io_uring submission queue entry to populate.
  virtual void processSubmit(struct io_uring_sqe* sqe) noexcept = 0;
  /// Handles completion of the operation.
  ///
  /// \param cqe The io_uring completion queue entry for this operation.
  virtual void callback(const io_uring_cqe* cqe) noexcept = 0;
  /// Handles completion of the operation after it was cancelled.
  ///
  /// \param cqe The io_uring completion queue entry for this operation.
  virtual void callbackCancelled(const io_uring_cqe* cqe) noexcept = 0;
  /// Returns the operation type of this entry.
  ///
  /// \returns The Type of operation this entry represents.
  IoSqeBase::Type type() const { return type_; }
  /// Reports whether the operation is currently submitted and awaiting completion.
  ///
  /// \returns `true` if the operation is in flight.
  bool inFlight() const { return inFlight_; }
  /// Reports whether the operation has been cancelled.
  ///
  /// \returns `true` if the operation was cancelled.
  bool cancelled() const { return cancelled_; }
  /// Marks the operation as cancelled.
  void markCancelled() { cancelled_ = true; }
  /// Associates the entry with an EventBase.
  ///
  /// \param evb The EventBase that owns this operation.
  void setEventBase(EventBase* evb) { evb_ = evb; }

 protected:
  // This is used if you want to prepare this sqe for reuse, but will manage the
  // lifetime. For example for zerocopy send, you might want to reuse the sqe
  // but still have a notification inbound.
  /// Prepares the entry for reuse while its lifetime stays managed elsewhere.
  void prepareForReuse() { internalMarkInflight(false); }
  /// Sets the in-flight state of the entry.
  ///
  /// \param val The new in-flight state.
  void internalMarkInflight(bool val) { inFlight_ = val; }

 private:
  friend class IoUringBackend;
  void internalSubmit(struct io_uring_sqe* sqe) noexcept;
  void internalCallback(const io_uring_cqe* cqe) noexcept;

  bool inFlight_ = false;
  bool cancelled_ = false;
  EventBase* evb_ = nullptr;
  Type type_;
};

/// Tracks a file descriptor registered with an io_uring ring.
struct IoUringFdRegistrationRecord
    : public boost::intrusive::slist_base_hook<
          boost::intrusive::cache_last<false>> {
  /// Reference count for the registration.
  int count_{0};
  /// The registered file descriptor.
  int fd_{-1};
  /// The index of the descriptor within the ring's registration table.
  int idx_{0};
};

} // namespace folly
