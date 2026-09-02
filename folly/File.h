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

//
// Docs: https://fburl.com/fbcref_file
//

#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>
#include <system_error>

#include <folly/ExceptionWrapper.h>
#include <folly/Expected.h>
#include <folly/Portability.h>
#include <folly/Range.h>
#include <folly/portability/Fcntl.h>
#include <folly/portability/Unistd.h>

namespace folly {

/**
 * A File represents an open file.
 * @class folly::File
 * @refcode folly/docs/examples/folly/File.cpp
 */
class File {
 public:
  /**
   * Creates an empty File object, for late initialization.
   */
  constexpr File() noexcept : fd_(-1), ownsFd_(false) {}

  /**
   * Create a File object from an existing file descriptor.
   *
   * @param fd Existing file descriptor
   * @param ownsFd Takes ownership of the file descriptor if ownsFd is true.
   */
  explicit File(int fd, bool ownsFd = false) noexcept;

  /**
   * Open and create a file object.  Throws on error.
   * Owns the file descriptor implicitly.
   *
   * @param name Path of the file to open.
   * @param flags Flags passed to open(), such as O_RDONLY.
   * @param mode Permission bits used when the file is created.
   */
  explicit File(const char* name, int flags = O_RDONLY, mode_t mode = 0666);

  /**
   * Open and create a file object.  Throws on error.
   * Owns the file descriptor implicitly.
   *
   * @param name Path of the file to open.
   * @param flags Flags passed to open(), such as O_RDONLY.
   * @param mode Permission bits used when the file is created.
   */
  explicit File(
      const std::string& name, int flags = O_RDONLY, mode_t mode = 0666);

  /**
   * Open and create a file object.  Throws on error.
   * Owns the file descriptor implicitly.
   *
   * @param name Path of the file to open.
   * @param flags Flags passed to open(), such as O_RDONLY.
   * @param mode Permission bits used when the file is created.
   */
  explicit File(StringPiece name, int flags = O_RDONLY, mode_t mode = 0666);

  /**
   * All the constructors that are not noexcept can throw std::system_error.
   * This is a helper method to use folly::Expected to chain a file open event
   * to something else you want to do with the open fd.
   *
   * @param args Arguments forwarded to a File constructor.
   * @return The opened File, or the captured exception on failure.
   */
  template <typename... Args>
  static Expected<File, exception_wrapper> makeFile(Args&&... args) noexcept {
    try {
      return File(std::forward<Args>(args)...);
    } catch (const std::system_error&) {
      return makeUnexpected(exception_wrapper(current_exception()));
    }
  }

  /// Close the file if owned, destroying the object.
  ~File();

  /**
   * Create and return a temporary, owned file.
   *
   * @return A File wrapping the newly created temporary file.
   */
  static File temporary();

  /**
   * Return the file descriptor, or -1 if the file was closed.
   *
   * @return The wrapped file descriptor, or -1 if none.
   */
  int fd() const { return fd_; }

  /**
   * Returns 'true' iff the file was successfully opened.
   *
   * @return True if the file is open, false otherwise.
   */
  explicit operator bool() const { return fd_ != -1; }

  /**
   * Duplicate file descriptor and return File that owns it.
   *
   * Duplicated file descriptor does not have close-on-exec flag set,
   * so it is leaked to child processes. Consider using "dupCloseOnExec".
   *
   * @return A File owning the duplicated descriptor.
   */
  File dup() const;

  /**
   * Duplicate file descriptor and return File that owns it.
   *
   * This functions creates a descriptor with close-on-exec flag set
   * (where supported, otherwise it is equivalent to "dup").
   *
   * @return A File owning the duplicated close-on-exec descriptor.
   */
  File dupCloseOnExec() const;

  /**
   * If we own the file descriptor, close the file and throw on error.
   * Otherwise, do nothing.
   */
  void close();

  /**
   * Closes the file (if owned).  Returns true on success, false (and sets
   * errno) on error.
   *
   * @return True on success, false (and sets errno) on error.
   */
  bool closeNoThrow();

  /**
   * Returns and releases the file descriptor; no longer owned by this File.
   * Returns -1 if the File object didn't wrap a file.
   *
   * @return The released file descriptor, or -1 if none was wrapped.
   */
  int release() noexcept;

  /**
   * Swap this File with another.
   *
   * @param other The File to swap with.
   */
  void swap(File& other) noexcept;

  /// Move-construct a File, taking ownership from another.
  ///
  /// \param other The File to take ownership from.
  File(File&& other) noexcept;

  /// Move-assign a File, taking ownership from another.
  ///
  /// \param other The File to take ownership from.
  /// \returns A reference to this File.
  File& operator=(File&& other);

  /**
   * FLOCK (INTERPROCESS) LOCKS
   *
   * NOTE THAT THESE LOCKS ARE flock() LOCKS.  That is, they may only be used
   * for inter-process synchronization -- an attempt to acquire a second lock
   * on the same file descriptor from the same process may succeed.  Attempting
   * to acquire a second lock on a different file descriptor for the same file
   * should fail, but some systems might implement flock() using fcntl() locks,
   * in which case it will succeed.
   */
  void lock();

  /// Try to acquire an exclusive flock without blocking.
  ///
  /// @return True if the lock was acquired, false otherwise.
  bool try_lock();

  /// Release an exclusive flock.
  void unlock();

  /// Acquire a shared flock, blocking until it is held.
  void lock_shared();

  /// Try to acquire a shared flock without blocking.
  ///
  /// @return True if the lock was acquired, false otherwise.
  bool try_lock_shared();

  /// Release a shared flock.
  void unlock_shared();

 private:
  void doLock(int op);
  bool doTryLock(int op);

  // unique
  File(const File&) = delete;
  File& operator=(const File&) = delete;

  int fd_;
  bool ownsFd_;
};

/**
 * Swaps the file descriptors and ownership
 *
 * @param a The first File to swap.
 * @param b The second File to swap.
 */
void swap(File& a, File& b) noexcept;

} // namespace folly
