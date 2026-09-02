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

#include <sys/types.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <iosfwd>
#include <mutex>
#include <utility>
#include <vector>

#include <folly/Function.h>
#include <folly/Portability.h>
#include <folly/Range.h>
#include <folly/portability/SysUio.h>

/// Facebook Folly library namespace.
namespace folly {
/// A pending operation backed by the Linux io_submit interface.
class AsyncIOOp;
/// A pending operation backed by the Linux io_uring interface.
class IoUringOp;
/**
 * An AsyncBaseOp represents a pending operation.  You may set a notification
 * callback or you may use this class's methods directly.
 *
 * The op must remain allocated until it is completed or canceled.
 */
class AsyncBaseOp {
  friend class AsyncBase;

 public:
  /// Callback invoked when the operation completes.
  using NotificationCallback = folly::Function<void(AsyncBaseOp*)>;

  /// Construct an operation with an optional completion callback.
  ///
  /// \param cb Callback invoked when the operation completes.
  explicit AsyncBaseOp(NotificationCallback cb = NotificationCallback());
  /// Deleted copy constructor.
  ///
  /// \param other The operation to copy from.
  AsyncBaseOp(const AsyncBaseOp& other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param other The operation to copy from.
  /// \returns A reference to this operation.
  AsyncBaseOp& operator=(const AsyncBaseOp& other) = delete;
  /// Destroy the operation.
  virtual ~AsyncBaseOp();

  /// Lifecycle state of an operation.
  enum class State {
    UNINITIALIZED, ///< Not yet initialized.
    INITIALIZED, ///< Initialized but not yet submitted.
    PENDING, ///< Submitted and awaiting completion.
    COMPLETED, ///< Completed.
    CANCELED, ///< Canceled before completion.
  };

  /**
   * Initiate a read request.
   *
   * \param fd File descriptor to read from.
   * \param buf Buffer to read into.
   * \param size Number of bytes to read.
   * \param start Offset in the file to read from.
   */
  virtual void pread(int fd, void* buf, size_t size, off_t start) = 0;
  /// Initiate a read request into the given range.
  ///
  /// \param fd File descriptor to read from.
  /// \param range Buffer range to read into.
  /// \param start Offset in the file to read from.
  void pread(int fd, Range<unsigned char*> range, off_t start) {
    pread(fd, range.begin(), range.size(), start);
  }
  /// Initiate a vectored read request.
  ///
  /// \param fd File descriptor to read from.
  /// \param iov Array of iovec entries to read into.
  /// \param iovcnt Number of entries in iov.
  /// \param start Offset in the file to read from.
  virtual void preadv(int fd, const iovec* iov, int iovcnt, off_t start) = 0;
  /// Initiate a read request using a registered buffer index.
  ///
  /// \param fd File descriptor to read from.
  /// \param buf Buffer to read into.
  /// \param size Number of bytes to read.
  /// \param start Offset in the file to read from.
  /// \param buf_index Index of the registered buffer to use.
  virtual void pread(
      int fd, void* buf, size_t size, off_t start, int buf_index) {
    pread(fd, buf, size, start);
  }

  /**
   * Initiate a write request.
   *
   * \param fd File descriptor to write to.
   * \param buf Buffer to write from.
   * \param size Number of bytes to write.
   * \param start Offset in the file to write to.
   */
  virtual void pwrite(int fd, const void* buf, size_t size, off_t start) = 0;
  /// Initiate a write request from the given range.
  ///
  /// \param fd File descriptor to write to.
  /// \param range Buffer range to write from.
  /// \param start Offset in the file to write to.
  void pwrite(int fd, Range<const unsigned char*> range, off_t start) {
    pwrite(fd, range.begin(), range.size(), start);
  }
  /// Initiate a vectored write request.
  ///
  /// \param fd File descriptor to write to.
  /// \param iov Array of iovec entries to write from.
  /// \param iovcnt Number of entries in iov.
  /// \param start Offset in the file to write to.
  virtual void pwritev(int fd, const iovec* iov, int iovcnt, off_t start) = 0;
  /// Initiate a write request using a registered buffer index.
  ///
  /// \param fd File descriptor to write to.
  /// \param buf Buffer to write from.
  /// \param size Number of bytes to write.
  /// \param start Offset in the file to write to.
  /// \param buf_index Index of the registered buffer to use.
  virtual void pwrite(
      int fd, const void* buf, size_t size, off_t start, int buf_index) {
    pwrite(fd, buf, size, start);
  }

  /// Return this operation as an AsyncIOOp, if it is one.
  ///
  /// \returns This operation as an AsyncIOOp, or nullptr.
  virtual AsyncIOOp* getAsyncIOOp() = 0;
  /// Return this operation as an IoUringOp, if it is one.
  ///
  /// \returns This operation as an IoUringOp, or nullptr.
  virtual IoUringOp* getIoUringOp() = 0;

  /// Write a human-readable description of this operation to the stream.
  ///
  /// \param os Stream to write to.
  virtual void toStream(std::ostream& os) const = 0;

  /**
   * Return the current operation state.
   *
   * \returns The current lifecycle state.
   */
  State state() const { return state_; }

  /**
   * user data get/set
   *
   * \returns The user data pointer associated with this operation.
   */
  void* getUserData() const { return userData_; }

  /// Associate an opaque user data pointer with this operation.
  ///
  /// \param userData The user data pointer to store.
  void setUserData(void* userData) { userData_ = userData; }

  /**
   * Reset the operation for reuse.  It is an error to call reset() on
   * an Op that is still pending.
   *
   * \param cb Callback invoked when the reused operation completes.
   */
  virtual void reset(NotificationCallback cb = NotificationCallback()) = 0;

  /// Set the callback invoked when the operation completes.
  ///
  /// \param cb Callback invoked when the operation completes.
  void setNotificationCallback(NotificationCallback cb) { cb_ = std::move(cb); }

  /**
   * Get the notification callback from the op.
   *
   * Note that this moves the callback out, leaving the callback in an
   * uninitialized state! You must call setNotificationCallback before
   * submitting the operation!
   *
   * \returns The notification callback, moved out of the operation.
   */
  NotificationCallback getNotificationCallback() { return std::move(cb_); }

  /**
   * Retrieve the result of this operation.  Returns >=0 on success,
   * -errno on failure (that is, using the Linux kernel error reporting
   * conventions).  Use checkKernelError (folly/Exception.h) on the result to
   * throw a std::system_error in case of error instead.
   *
   * It is an error to call this if the Op hasn't completed.
   *
   * \returns The operation result: >=0 on success, -errno on failure.
   */
  ssize_t result() const;

  /// Return a human-readable name for the given file descriptor.
  ///
  /// \param fd File descriptor to describe.
  /// \returns A human-readable name for the descriptor.
  static std::string fd2name(int fd);

 protected:
  /// Initialize the operation.
  void init();
  /// Mark the operation as started.
  void start();
  /// Revert the operation from the started state.
  void unstart();
  /// Mark the operation as completed with the given result.
  ///
  /// \param result The operation result.
  void complete(ssize_t result);
  /// Mark the operation as canceled.
  void cancel();

  NotificationCallback cb_; ///< Completion callback.
  std::atomic<State> state_; ///< Current lifecycle state.
  ssize_t result_; ///< Result of the completed operation.
  void* userData_{nullptr}; ///< Opaque user data pointer.
};

/// Write a description of the operation to the stream.
///
/// \param os Stream to write to.
/// \param op Operation to describe.
/// \returns The stream.
std::ostream& operator<<(std::ostream& os, const AsyncBaseOp& op);
/// Write the name of the operation state to the stream.
///
/// \param os Stream to write to.
/// \param state State to describe.
/// \returns The stream.
std::ostream& operator<<(std::ostream& os, AsyncBaseOp::State state);

/**
 * Generic C++ interface around Linux IO(io_submit, io_uring)
 */
class AsyncBase {
 public:
  /// The operation type managed by this context.
  using Op = AsyncBaseOp;

  /// Whether the context exposes a pollable completion file descriptor.
  enum PollMode {
    NOT_POLLABLE, ///< Completions are retrieved with wait().
    POLLABLE, ///< Completions are signalled through pollFd().
  };

  /**
   * Create an AsyncBase context capable of holding at most 'capacity' pending
   * requests at the same time.  As requests complete, others can be scheduled,
   * as long as this limit is not exceeded.
   *
   * If pollMode is POLLABLE, pollFd() will return a file descriptor that
   * can be passed to poll / epoll / select and will become readable when
   * any IOs on this AsyncBase have completed.  If you do this, you must use
   * pollCompleted() instead of wait() -- do not read from the pollFd()
   * file descriptor directly.
   *
   * You may use the same AsyncBase object from multiple threads, as long as
   * there is only one concurrent caller of wait() / pollCompleted() / cancel()
   * (perhaps by always calling it from the same thread, or by providing
   * appropriate mutual exclusion).  In this case, pending() returns a snapshot
   * of the current number of pending requests.
   *
   * \param capacity Maximum number of concurrently pending requests.
   * \param pollMode Whether to expose a pollable completion file descriptor.
   */
  explicit AsyncBase(size_t capacity, PollMode pollMode = NOT_POLLABLE);
  /// Deleted copy constructor.
  ///
  /// \param other The context to copy from.
  AsyncBase(const AsyncBase& other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param other The context to copy from.
  /// \returns A reference to this context.
  AsyncBase& operator=(const AsyncBase& other) = delete;
  /// Destroy the context.
  virtual ~AsyncBase();

  /**
   * Initialize context
   */
  virtual void initializeContext() = 0;

  /**
   * Wait for at least minRequests to complete.  Returns the requests that
   * have completed; the returned range is valid until the next call to
   * wait().  minRequests may be 0 to not block.
   *
   * \param minRequests Minimum number of requests to wait for.
   * \returns The requests that have completed.
   */
  Range<Op**> wait(size_t minRequests);

  /**
   * Cancel all pending requests and return them; the returned range is
   * valid until the next call to cancel().
   *
   * \returns The canceled requests.
   */
  Range<Op**> cancel();

  /**
   * Return the number of pending requests.
   *
   * \returns The number of pending requests.
   */
  size_t pending() const { return pending_; }

  /**
   * Return the maximum number of requests that can be kept outstanding
   * at any one time.
   *
   * \returns The context capacity.
   */
  size_t capacity() const { return capacity_; }

  /**
   * Return the accumulative number of submitted I/O, since this object
   * has been created.
   *
   * \returns The total number of submitted requests.
   */
  size_t totalSubmits() const { return submitted_; }

  /**
   * If POLLABLE, return a file descriptor that can be passed to poll / epoll
   * and will become readable when any async IO operations have completed.
   * If NOT_POLLABLE, return -1.
   *
   * \returns The completion file descriptor, or -1 if not pollable.
   */
  int pollFd() const { return pollFd_; }

  /**
   * If POLLABLE, call instead of wait after the file descriptor returned
   * by pollFd() became readable.  The returned range is valid until the next
   * call to pollCompleted().
   *
   * \returns The requests that have completed.
   */
  Range<Op**> pollCompleted();

  /**
   * Submit an op for execution.
   *
   * \param op The operation to submit.
   */
  void submit(Op* op);

  /**
   * Submit a range of ops for execution
   *
   * \param ops The operations to submit.
   * \returns The number of operations submitted.
   */
  int submit(Range<Op**> ops);

 protected:
  /// Drain readiness notifications from the poll file descriptor.
  ///
  /// \returns The number of notifications drained.
  virtual int drainPollFd() = 0;
  /// Mark an operation as completed with the given result.
  ///
  /// \param op The operation to complete.
  /// \param result The operation result.
  void complete(Op* op, ssize_t result) { op->complete(result); }

  /// Mark an operation as canceled.
  ///
  /// \param op The operation to cancel.
  void cancel(Op* op) { op->cancel(); }

  /// Return whether the context has been initialized.
  ///
  /// \returns True if the context has been initialized.
  bool isInit() const { return init_.load(std::memory_order_relaxed); }

  /// Decrease the pending request count by the given amount.
  ///
  /// \param num Number of pending requests to subtract.
  void decrementPending(size_t num = 1);
  /// Submit a single operation to the underlying backend.
  ///
  /// \param op The operation to submit.
  /// \returns The number of operations submitted.
  virtual int submitOne(AsyncBase::Op* op) = 0;
  /// Submit a range of operations to the underlying backend.
  ///
  /// \param ops The operations to submit.
  /// \returns The number of operations submitted.
  virtual int submitRange(Range<AsyncBase::Op**> ops) = 0;

  /// Whether a wait should collect completed or canceled operations.
  enum class WaitType {
    COMPLETE, ///< Wait for operations to complete.
    CANCEL ///< Wait for operations to be canceled.
  };
  /// Wait for operations to complete or be canceled.
  ///
  /// \param type Whether to collect completed or canceled operations.
  /// \param minRequests Minimum number of operations to wait for.
  /// \param maxRequests Maximum number of operations to collect.
  /// \param result Vector receiving the collected operations.
  /// \returns The collected operations.
  virtual Range<AsyncBase::Op**> doWait(
      WaitType type,
      size_t minRequests,
      size_t maxRequests,
      std::vector<Op*>& result) = 0;

  std::atomic<bool> init_{false}; ///< Whether the context is initialized.
  std::mutex initMutex_; ///< Guards initialization.

  std::atomic<size_t> pending_{0}; ///< Number of pending requests.
  std::atomic<size_t> submitted_{0}; ///< Total number of submitted requests.
  const size_t capacity_; ///< Maximum concurrent pending requests.
  const PollMode pollMode_; ///< Configured poll mode.
  int pollFd_{-1}; ///< Completion file descriptor, or -1.
  std::vector<Op*> completed_; ///< Buffer of completed operations.
  std::vector<Op*> canceled_; ///< Buffer of canceled operations.
};

/**
 * Wrapper around AsyncBase that allows you to schedule more requests than
 * the AsyncBase's object capacity.  Other requests are queued and processed
 * in a FIFO order.
 */
class AsyncBaseQueue {
 public:
  /**
   * Create a queue, using the given AsyncBase object.
   * The AsyncBase object may not be used by anything else until the
   * queue is destroyed.
   *
   * \param asyncBase The AsyncBase object backing this queue.
   */
  explicit AsyncBaseQueue(AsyncBase* asyncBase);
  /// Destroy the queue.
  ~AsyncBaseQueue();

  /// Return the number of operations currently queued.
  ///
  /// \returns The number of queued operations.
  size_t queued() const { return queue_.size(); }

  /**
   * Submit an op to the AsyncBase queue.  The op will be queued until
   * the AsyncBase object has room.
   *
   * \param op The operation to submit.
   */
  void submit(AsyncBaseOp* op);

  /// Factory that lazily creates an operation when the queue has room.
  using OpFactory = std::function<AsyncBaseOp*()>;
  /**
   * Submit a delayed op to the AsyncBase queue; this allows you to postpone
   * creation of the Op (which may require allocating memory, etc) until
   * the AsyncBase object has room.
   *
   * \param op Factory that creates the operation when there is room.
   */
  void submit(OpFactory op);

 private:
  void onCompleted(AsyncBaseOp* op);
  void maybeDequeue();

  AsyncBase* asyncBase_;

  std::deque<OpFactory> queue_;
};

} // namespace folly
