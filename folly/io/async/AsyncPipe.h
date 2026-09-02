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

#include <list>
#include <system_error>

#include <folly/io/IOBufQueue.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/io/async/EventHandler.h>

namespace folly {

class AsyncSocketException;

/**
 * Read from a pipe in an async manner.
 */
class AsyncPipeReader
    : public EventHandler,
      public AsyncReader,
      public DelayedDestruction {
 public:
  /// Smart pointer type that destroys the reader through DelayedDestruction.
  using UniquePtr = folly::DelayedDestructionUniquePtr<AsyncPipeReader>;

  /**
   * Create a new AsyncPipeReader managed by a UniquePtr.
   *
   * \param eventBase The EventBase used to drive the reader.
   * \param pipeFd The pipe file descriptor to read from.
   * \returns A UniquePtr owning the new reader.
   */
  static UniquePtr newReader(
      folly::EventBase* eventBase, NetworkSocket pipeFd) {
    return UniquePtr(new AsyncPipeReader(eventBase, pipeFd));
  }

  /**
   * Construct an AsyncPipeReader for the given pipe file descriptor.
   *
   * \param eventBase The EventBase used to drive the reader.
   * \param pipeFd The pipe file descriptor to read from.
   */
  AsyncPipeReader(folly::EventBase* eventBase, NetworkSocket pipeFd)
      : EventHandler(eventBase, pipeFd), fd_(pipeFd) {}

  /**
   * Set the read callback and automatically install/uninstall the handler
   * for events.
   *
   * \param callback The read callback to install, or nullptr to remove it.
   */
  void setReadCB(AsyncReader::ReadCallback* callback) override {
    if (callback == readCallback_) {
      return;
    }
    readCallback_ = callback;
    if (readCallback_ && !isHandlerRegistered()) {
      registerHandler(EventHandler::READ | EventHandler::PERSIST);
    } else if (!readCallback_ && isHandlerRegistered()) {
      unregisterHandler();
    }
  }

  /**
   * Get the read callback
   *
   * \returns The currently installed read callback, or nullptr if none.
   */
  AsyncReader::ReadCallback* getReadCallback() const override {
    return readCallback_;
  }

  /**
   * Set a special hook to close the socket (otherwise, will call close())
   *
   * \param closeCb The hook invoked to close the pipe file descriptor.
   */
  void setCloseCallback(std::function<void(NetworkSocket)> closeCb) {
    closeCb_ = closeCb;
  }

 private:
  ~AsyncPipeReader() override;

  void handlerReady(uint16_t events) noexcept override;
  void failRead(const AsyncSocketException& ex);
  void close();

  NetworkSocket fd_;
  AsyncReader::ReadCallback* readCallback_{nullptr};
  std::function<void(NetworkSocket)> closeCb_;
};

/**
 * Write to a pipe in an async manner.
 */
class AsyncPipeWriter
    : public EventHandler,
      public AsyncWriter,
      public DelayedDestruction {
 public:
  /// Smart pointer type that destroys the writer through DelayedDestruction.
  using UniquePtr = folly::DelayedDestructionUniquePtr<AsyncPipeWriter>;

  /**
   * Create a new AsyncPipeWriter managed by a UniquePtr.
   *
   * \param eventBase The EventBase used to drive the writer.
   * \param pipeFd The pipe file descriptor to write to.
   * \returns A UniquePtr owning the new writer.
   */
  static UniquePtr newWriter(
      folly::EventBase* eventBase, NetworkSocket pipeFd) {
    return UniquePtr(new AsyncPipeWriter(eventBase, pipeFd));
  }

  /**
   * Construct an AsyncPipeWriter for the given pipe file descriptor.
   *
   * \param eventBase The EventBase used to drive the writer.
   * \param pipeFd The pipe file descriptor to write to.
   */
  AsyncPipeWriter(folly::EventBase* eventBase, NetworkSocket pipeFd)
      : EventHandler(eventBase, pipeFd), fd_(pipeFd) {}

  /**
   * Asynchronously write the given iobuf to this pipe, and invoke the callback
   * on success/error.
   *
   * \param buf The buffer to write to the pipe.
   * \param callback The callback invoked on success or error, or nullptr.
   */
  void write(
      std::unique_ptr<folly::IOBuf> buf,
      AsyncWriter::WriteCallback* callback = nullptr);

  /**
   * Set a special hook to close the socket (otherwise, will call close())
   *
   * \param closeCb The hook invoked to close the pipe file descriptor.
   */
  void setCloseCallback(std::function<void(NetworkSocket)> closeCb) {
    closeCb_ = closeCb;
  }

  /**
   * Returns true if the pipe is closed
   *
   * \returns True if the pipe is closed or scheduled to close when empty.
   */
  bool closed() const { return (fd_ == NetworkSocket() || closeOnEmpty_); }

  /**
   * Notify the pipe to close as soon as all pending writes complete
   */
  void closeOnEmpty();

  /**
   * Close the pipe immediately, and fail all pending writes
   */
  void closeNow();

  /**
   * Return true if there are currently writes pending (eg: the pipe is blocked
   * for writing)
   *
   * \returns True if there are writes pending in the queue.
   */
  bool hasPendingWrites() const { return !queue_.empty(); }

  /**
   * Write a raw buffer to the pipe, wrapping it in an IOBuf chain.
   *
   * \param callback The callback invoked on success or error, or nullptr.
   * \param buf Pointer to the bytes to write.
   * \param bytes The number of bytes to write.
   * \param flags Flags controlling how the write is performed.
   */
  // AsyncWriter methods
  void write(
      folly::AsyncWriter::WriteCallback* callback,
      const void* buf,
      size_t bytes,
      WriteFlags flags = WriteFlags::NONE) override {
    writeChain(callback, IOBuf::wrapBuffer(buf, bytes), flags);
  }
  /**
   * Scatter/gather write, which is not supported by this pipe.
   *
   * \param callback The write callback (unused).
   * \param iov The iovec array to write (unused).
   * \param count The number of iovec entries (unused).
   * \param flags Flags controlling how the write is performed.
   */
  void writev(
      folly::AsyncWriter::WriteCallback* callback,
      const iovec* iov,
      size_t count,
      WriteFlags flags = WriteFlags::NONE) override {
    throw std::runtime_error("writev is not supported. Please use writeChain.");
  }
  /**
   * Write an IOBuf chain to the pipe, and invoke the callback on completion.
   *
   * \param callback The callback invoked on success or error, or nullptr.
   * \param buf The IOBuf chain to write.
   * \param flags Flags controlling how the write is performed.
   */
  void writeChain(
      folly::AsyncWriter::WriteCallback* callback,
      std::unique_ptr<folly::IOBuf>&& buf,
      WriteFlags flags = WriteFlags::NONE) override;

 private:
  void handlerReady(uint16_t events) noexcept override;
  void handleWrite();
  void failAllWrites(const AsyncSocketException& ex);

  NetworkSocket fd_;
  std::list<std::pair<folly::IOBufQueue, AsyncWriter::WriteCallback*>> queue_;
  bool closeOnEmpty_{false};
  std::function<void(NetworkSocket)> closeCb_;

  ~AsyncPipeWriter() override { closeNow(); }
};

} // namespace folly
