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

#include <folly/io/async/AsyncBase.h>

#if __has_include(<libaio.h>)

#include <libaio.h>

/// Facebook Folly library namespace.
namespace folly {

/// An AsyncBaseOp backed by the Linux libaio (io_submit) interface.
class AsyncIOOp : public AsyncBaseOp {
  friend class AsyncIO;
  /// Write a description of the operation to the stream.
  friend std::ostream& operator<<(std::ostream& os, const AsyncIOOp& o);

 public:
  /// Construct an operation with an optional completion callback.
  ///
  /// \param cb Callback invoked when the operation completes.
  explicit AsyncIOOp(NotificationCallback cb = NotificationCallback());
  /// Deleted copy constructor.
  ///
  /// \param other The operation to copy from.
  AsyncIOOp(const AsyncIOOp& other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param other The operation to copy from.
  /// \returns A reference to this operation.
  AsyncIOOp& operator=(const AsyncIOOp& other) = delete;
  /// Destroy the operation.
  ~AsyncIOOp() override;

  /**
   * Initiate a read request.
   *
   * \param fd File descriptor to read from.
   * \param buf Buffer to read into.
   * \param size Number of bytes to read.
   * \param start Offset in the file to read from.
   */
  void pread(int fd, void* buf, size_t size, off_t start) override;
  /// Initiate a vectored read request.
  ///
  /// \param fd File descriptor to read from.
  /// \param iov Array of iovec entries to read into.
  /// \param iovcnt Number of entries in iov.
  /// \param start Offset in the file to read from.
  void preadv(int fd, const iovec* iov, int iovcnt, off_t start) override;

  /**
   * Initiate a write request.
   *
   * \param fd File descriptor to write to.
   * \param buf Buffer to write from.
   * \param size Number of bytes to write.
   * \param start Offset in the file to write to.
   */
  void pwrite(int fd, const void* buf, size_t size, off_t start) override;
  /// Initiate a vectored write request.
  ///
  /// \param fd File descriptor to write to.
  /// \param iov Array of iovec entries to write from.
  /// \param iovcnt Number of entries in iov.
  /// \param start Offset in the file to write to.
  void pwritev(int fd, const iovec* iov, int iovcnt, off_t start) override;

  /// Reset the operation for reuse.
  ///
  /// \param cb Callback invoked when the reused operation completes.
  void reset(NotificationCallback cb = NotificationCallback()) override;

  /// Return this operation as an AsyncIOOp.
  ///
  /// \returns This operation.
  AsyncIOOp* getAsyncIOOp() override { return this; }

  /// Return this operation as an IoUringOp.
  ///
  /// \returns Always nullptr for this backend.
  IoUringOp* getIoUringOp() override { return nullptr; }

  /// Write a human-readable description of this operation to the stream.
  ///
  /// \param os Stream to write to.
  void toStream(std::ostream& os) const override;

  /// Return the underlying libaio control block.
  ///
  /// \returns The libaio iocb for this operation.
  const iocb& getIocb() const { return iocb_; }

 private:
  iocb iocb_;
};

/// Write a description of the operation to the stream.
///
/// \param os Stream to write to.
/// \param o Operation to describe.
/// \returns The stream.
std::ostream& operator<<(std::ostream& os, const AsyncIOOp& op);

/**
 * C++ interface around Linux Async IO.
 */
class AsyncIO : public AsyncBase {
 public:
  /// The operation type managed by this context.
  using Op = AsyncIOOp;

  /**
   * Create a libaio context with the given capacity.
   *
   * Note: the maximum number of allowed concurrent requests is controlled
   * by the fs.aio-max-nr sysctl, the default value is usually 64K.
   *
   * \param capacity Maximum number of concurrently pending requests.
   * \param pollMode Whether to expose a pollable completion file descriptor.
   */
  explicit AsyncIO(size_t capacity, PollMode pollMode = NOT_POLLABLE);
  /// Deleted copy constructor.
  ///
  /// \param other The context to copy from.
  AsyncIO(const AsyncIO& other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param other The context to copy from.
  /// \returns A reference to this context.
  AsyncIO& operator=(const AsyncIO& other) = delete;
  /// Destroy the context.
  ~AsyncIO() override;

  /// Initialize the libaio context.
  void initializeContext() override;

 protected:
  /// Drain readiness notifications from the poll file descriptor.
  ///
  /// \returns The number of notifications drained.
  int drainPollFd() override;
  /// Submit a single operation to libaio.
  ///
  /// \param op The operation to submit.
  /// \returns The number of operations submitted.
  int submitOne(AsyncBase::Op* op) override;
  /// Submit a range of operations to libaio.
  ///
  /// \param ops The operations to submit.
  /// \returns The number of operations submitted.
  int submitRange(Range<AsyncBase::Op**> ops) override;

 private:
  Range<AsyncBase::Op**> doWait(
      WaitType type,
      size_t minRequests,
      size_t maxRequests,
      std::vector<AsyncBase::Op*>& result) override;

  io_context_t ctx_{nullptr};
};

/// A queue of operations backed by an AsyncIO context.
using AsyncIOQueue = AsyncBaseQueue;
} // namespace folly

#endif
