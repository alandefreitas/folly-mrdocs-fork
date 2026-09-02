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

#include <sys/stat.h>
#include <sys/types.h>

#include <cassert>
#include <filesystem>
#include <limits>

#include <folly/Portability.h>
#include <folly/Range.h>
#include <folly/ScopeGuard.h>
#include <folly/net/NetworkSocket.h>
#include <folly/portability/Fcntl.h>
#include <folly/portability/SysUio.h>
#include <folly/portability/Unistd.h>

namespace folly {

/**
 * Convenience wrappers around some commonly used system calls.  The *NoInt
 * wrappers retry on EINTR.  The *Full wrappers retry on EINTR and also loop
 * until all data is written.  Note that *Full wrappers weaken the thread
 * semantics of underlying system calls.
 *
 * \param name Path of the file to open.
 * \param flags open() flags.
 * \param mode Permission bits used when creating the file.
 * \returns The new file descriptor, or -1 on error.
 */
int openNoInt(const char* name, int flags, mode_t mode = 0666);
// Two overloads, as we may be closing either a file or a socket.
/// Closes a file descriptor, retrying on EINTR.
/// \param fd The file descriptor to close.
/// \returns 0 on success, or -1 on error.
int closeNoInt(int fd);
/// Closes a network socket, retrying on EINTR.
/// \param fd The socket to close.
/// \returns 0 on success, or -1 on error.
int closeNoInt(NetworkSocket fd);
/// Duplicates a file descriptor, retrying on EINTR.
/// \param fd The file descriptor to duplicate.
/// \returns The new file descriptor, or -1 on error.
int dupNoInt(int fd);
/// Duplicates a file descriptor onto another, retrying on EINTR.
/// \param oldFd The file descriptor to duplicate.
/// \param newFd The target file descriptor.
/// \returns The new file descriptor, or -1 on error.
int dup2NoInt(int oldFd, int newFd);
/// Synchronizes a file's state to storage, retrying on EINTR.
/// \param fd The file descriptor to sync.
/// \returns 0 on success, or -1 on error.
int fsyncNoInt(int fd);
/// Synchronizes a file's data to storage, retrying on EINTR.
/// \param fd The file descriptor to sync.
/// \returns 0 on success, or -1 on error.
int fdatasyncNoInt(int fd);
/// Truncates a file by descriptor, retrying on EINTR.
/// \param fd The file descriptor to truncate.
/// \param len The new file length.
/// \returns 0 on success, or -1 on error.
int ftruncateNoInt(int fd, off_t len);
/// Truncates a file by path, retrying on EINTR.
/// \param path Path of the file to truncate.
/// \param len The new file length.
/// \returns 0 on success, or -1 on error.
int truncateNoInt(const char* path, off_t len);
/// Applies an advisory lock, retrying on EINTR.
/// \param fd The file descriptor to lock.
/// \param operation The flock() operation to perform.
/// \returns 0 on success, or -1 on error.
int flockNoInt(int fd, int operation);
/// Shuts down a socket, retrying on EINTR.
/// \param fd The socket to shut down.
/// \param how The shutdown mode.
/// \returns 0 on success, or -1 on error.
int shutdownNoInt(NetworkSocket fd, int how);

/// Reads from a file descriptor, retrying on EINTR.
/// \param fd The file descriptor to read from.
/// \param buf Destination buffer.
/// \param count Maximum number of bytes to read.
/// \returns The number of bytes read, or -1 on error.
ssize_t readNoInt(int fd, void* buf, size_t count);
/// Reads from a file descriptor at an offset, retrying on EINTR.
/// \param fd The file descriptor to read from.
/// \param buf Destination buffer.
/// \param count Maximum number of bytes to read.
/// \param offset File offset to read from.
/// \returns The number of bytes read, or -1 on error.
ssize_t preadNoInt(int fd, void* buf, size_t count, off_t offset);
/// Scatter-reads from a file descriptor, retrying on EINTR.
/// \param fd The file descriptor to read from.
/// \param iov Array of buffers to fill.
/// \param count Number of entries in iov.
/// \returns The number of bytes read, or -1 on error.
ssize_t readvNoInt(int fd, const iovec* iov, int count);
/// Scatter-reads from a file descriptor at an offset, retrying on EINTR.
/// \param fd The file descriptor to read from.
/// \param iov Array of buffers to fill.
/// \param count Number of entries in iov.
/// \param offset File offset to read from.
/// \returns The number of bytes read, or -1 on error.
ssize_t preadvNoInt(int fd, const iovec* iov, int count, off_t offset);

/// Writes to a file descriptor, retrying on EINTR.
/// \param fd The file descriptor to write to.
/// \param buf Source buffer.
/// \param count Number of bytes to write.
/// \returns The number of bytes written, or -1 on error.
ssize_t writeNoInt(int fd, const void* buf, size_t count);
/// Writes to a file descriptor at an offset, retrying on EINTR.
/// \param fd The file descriptor to write to.
/// \param buf Source buffer.
/// \param count Number of bytes to write.
/// \param offset File offset to write at.
/// \returns The number of bytes written, or -1 on error.
ssize_t pwriteNoInt(int fd, const void* buf, size_t count, off_t offset);
/// Gather-writes to a file descriptor, retrying on EINTR.
/// \param fd The file descriptor to write to.
/// \param iov Array of buffers to write.
/// \param count Number of entries in iov.
/// \returns The number of bytes written, or -1 on error.
ssize_t writevNoInt(int fd, const iovec* iov, int count);
/// Gather-writes to a file descriptor at an offset, retrying on EINTR.
/// \param fd The file descriptor to write to.
/// \param iov Array of buffers to write.
/// \param count Number of entries in iov.
/// \param offset File offset to write at.
/// \returns The number of bytes written, or -1 on error.
ssize_t pwritevNoInt(int fd, const iovec* iov, int count, off_t offset);

/**
 * Wrapper around read() (and pread()) that, in addition to retrying on
 * EINTR, will loop until all data is read.
 *
 * This wrapper is only useful for blocking file descriptors (for non-blocking
 * file descriptors, you have to be prepared to deal with incomplete reads
 * anyway), and only exists because POSIX allows read() to return an incomplete
 * read if interrupted by a signal (instead of returning -1 and setting errno
 * to EINTR).
 *
 * Note that this wrapper weakens the thread safety of read(): the file pointer
 * is shared between threads, but the system call is atomic.  If multiple
 * threads are reading from a file at the same time, you don't know where your
 * data came from in the file, but you do know that the returned bytes were
 * contiguous.  You can no longer make this assumption if using readFull().
 * You should probably use pread() when reading from the same file descriptor
 * from multiple threads simultaneously, anyway.
 *
 * Note that readvFull and preadvFull require iov to be non-const, unlike
 * readv and preadv.  The contents of iov after these functions return
 * is unspecified.
 *
 * \param fd The file descriptor to read from.
 * \param buf Destination buffer.
 * \param count Number of bytes to read.
 * \returns The number of bytes read, or -1 on error.
 */
[[nodiscard]] ssize_t readFull(int fd, void* buf, size_t count);
/// Like readFull but reads at a given offset.
/// \param fd The file descriptor to read from.
/// \param buf Destination buffer.
/// \param count Number of bytes to read.
/// \param offset File offset to read from.
/// \returns The number of bytes read, or -1 on error.
[[nodiscard]] ssize_t preadFull(int fd, void* buf, size_t count, off_t offset);
/// Like readFull but scatter-reads into multiple buffers.
/// \param fd The file descriptor to read from.
/// \param iov Array of buffers to fill; contents after return are unspecified.
/// \param count Number of entries in iov.
/// \returns The number of bytes read, or -1 on error.
[[nodiscard]] ssize_t readvFull(int fd, iovec* iov, int count);
/// Like readvFull but scatter-reads at a given offset.
/// \param fd The file descriptor to read from.
/// \param iov Array of buffers to fill; contents after return are unspecified.
/// \param count Number of entries in iov.
/// \param offset File offset to read from.
/// \returns The number of bytes read, or -1 on error.
[[nodiscard]] ssize_t preadvFull(int fd, iovec* iov, int count, off_t offset);

/**
 * Similar to readFull and preadFull above, wrappers around write() and
 * pwrite() that loop until all data is written.
 *
 * Generally, the write() / pwrite() system call may always write fewer bytes
 * than requested, just like read().  In certain cases (such as when writing to
 * a pipe), POSIX provides stronger guarantees, but not in the general case.
 * For example, Linux (even on a 64-bit platform) won't write more than 2GB in
 * one write() system call.
 *
 * Note that writevFull and pwritevFull require iov to be non-const, unlike
 * writev and pwritev.  The contents of iov after these functions return
 * is unspecified.
 *
 * These functions return -1 on error, or the total number of bytes written
 * (which is always the same as the number of requested bytes) on success.
 *
 * \param fd The file descriptor to write to.
 * \param buf Source buffer.
 * \param count Number of bytes to write.
 * \returns The number of bytes written, or -1 on error.
 */
ssize_t writeFull(int fd, const void* buf, size_t count);
/// Like writeFull but writes at a given offset.
/// \param fd The file descriptor to write to.
/// \param buf Source buffer.
/// \param count Number of bytes to write.
/// \param offset File offset to write at.
/// \returns The number of bytes written, or -1 on error.
ssize_t pwriteFull(int fd, const void* buf, size_t count, off_t offset);
/// Like writeFull but gather-writes from multiple buffers.
/// \param fd The file descriptor to write to.
/// \param iov Array of buffers to write; contents after return are unspecified.
/// \param count Number of entries in iov.
/// \returns The number of bytes written, or -1 on error.
ssize_t writevFull(int fd, iovec* iov, int count);
/// Like writevFull but gather-writes at a given offset.
/// \param fd The file descriptor to write to.
/// \param iov Array of buffers to write; contents after return are unspecified.
/// \param count Number of entries in iov.
/// \param offset File offset to write at.
/// \returns The number of bytes written, or -1 on error.
ssize_t pwritevFull(int fd, iovec* iov, int count, off_t offset);

/**
 * Read entire file (if num_bytes is defaulted) or no more than
 * num_bytes (otherwise) into container *out. The container is assumed
 * to be contiguous, with element size equal to 1, and offer size(),
 * reserve(), and random access (e.g. std::vector<char>, std::string,
 * fbstring).
 *
 * Returns: true on success or false on failure. In the latter case
 * errno will be set appropriately by the failing system primitive.
 *
 * \param fd The file descriptor to read from.
 * \param out The container that the file contents are read into.
 * \param num_bytes Maximum number of bytes to read.
 * \returns True on success, false on failure.
 */
template <class Container>
bool readFile(
    int fd,
    Container& out,
    size_t num_bytes = std::numeric_limits<size_t>::max()) {
  static_assert(
      sizeof(out[0]) == 1,
      "readFile: only containers with byte-sized elements accepted");

  size_t soFar = 0; // amount of bytes successfully read
  SCOPE_EXIT {
    assert(out.size() >= soFar); // resize better doesn't throw
    out.resize(soFar);
  };

  // Obtain file size:
  struct stat buf;
  if (fstat(fd, &buf) == -1) {
    return false;
  }
  // Some files (notably under /proc and /sys on Linux) lie about
  // their size, so treat the size advertised by fstat under advise
  // but don't rely on it. In particular, if the size is zero, we
  // should attempt to read stuff. If not zero, we'll attempt to read
  // one extra byte.
  constexpr size_t initialAlloc = 1024 * 4;
  out.resize(
      std::min(
          buf.st_size > 0 ? (size_t(buf.st_size) + 1) : initialAlloc,
          num_bytes));

  while (soFar < out.size()) {
    const auto actual = readFull(fd, &out[soFar], out.size() - soFar);
    if (actual == -1) {
      return false;
    }
    soFar += actual;
    if (soFar < out.size()) {
      // File exhausted
      break;
    }
    // Ew, allocate more memory. Use exponential growth to avoid
    // quadratic behavior. Cap size to num_bytes.
    out.resize(std::min(out.size() * 3 / 2, num_bytes));
  }

  return true;
}

/**
 * Same as above, but takes in a file name instead of fd
 *
 * \param file_name Path of the file to read.
 * \param out The container that the file contents are read into.
 * \param num_bytes Maximum number of bytes to read.
 * \returns True on success, false on failure.
 */
template <class Container>
bool readFile(
    const char* file_name,
    Container& out,
    size_t num_bytes = std::numeric_limits<size_t>::max()) {
  assert(file_name);

  const auto fd = openNoInt(file_name, O_RDONLY | O_CLOEXEC);
  if (fd == -1) {
    return false;
  }

  SCOPE_EXIT {
    // Ignore errors when closing the file
    closeNoInt(fd);
  };

  return readFile(fd, out, num_bytes);
}

/**
 * Same as above, but takes a std::filesystem::path instead of fd
 *
 * \param path Path of the file to read.
 * \param out The container that the file contents are read into.
 * \param num_bytes Maximum number of bytes to read.
 * \returns True on success, false on failure.
 */
template <class Container>
  requires(std::same_as<std::filesystem::path::value_type, char>)
bool readFile(
    const std::filesystem::path& path,
    Container& out,
    size_t num_bytes = std::numeric_limits<size_t>::max()) {
  return readFile(path.c_str(), out, num_bytes);
}

/**
 * Writes container to file. The container is assumed to be
 * contiguous, with element size equal to 1, and offering STL-like
 * methods empty(), size(), and indexed access
 * (e.g. std::vector<char>, std::string, fbstring, StringPiece).
 *
 * "flags" dictates the open flags to use. Default is to create file
 * if it doesn't exist and truncate it.
 *
 * Returns: true on success or false on failure. In the latter case
 * errno will be set appropriately by the failing system primitive.
 *
 * Note that this function may leave the file in a partially written state on
 * failure.  Use writeFileAtomic() if you want to ensure that the existing file
 * state will be unchanged on error.
 *
 * \param data The container whose contents are written.
 * \param filename Path of the file to write.
 * \param flags open() flags controlling how the file is opened.
 * \param mode Permission bits used when creating the file.
 * \returns True on success, false on failure.
 */
template <class Container>
bool writeFile(
    const Container& data,
    const char* filename,
    int flags = O_WRONLY | O_CREAT | O_TRUNC,
    mode_t mode = 0666) {
  static_assert(
      sizeof(data[0]) == 1, "writeFile works with element size equal to 1");
  int fd = fileops::open(filename, flags, mode);
  if (fd == -1) {
    return false;
  }
  bool ok = data.empty() ||
      writeFull(fd, &data[0], data.size()) == static_cast<ssize_t>(data.size());
  return closeNoInt(fd) == 0 && ok;
}

/**
 * Same as above, but takes a std::filesystem::path instead of filename
 *
 * \param data The container whose contents are written.
 * \param filename Path of the file to write.
 * \param flags open() flags controlling how the file is opened.
 * \param mode Permission bits used when creating the file.
 * \returns True on success, false on failure.
 */
template <class Container>
  requires(std::same_as<std::filesystem::path::value_type, char>)
bool writeFile(
    const Container& data,
    const std::filesystem::path& filename,
    int flags = O_WRONLY | O_CREAT | O_TRUNC,
    mode_t mode = 0666) {
  return writeFile(data, filename.c_str(), flags, mode);
}

/// Whether an atomic write syncs to storage to guarantee ordering.
enum class SyncType {
  WITH_SYNC, ///< Sync the data to storage before the rename.
  WITHOUT_SYNC, ///< Do not sync before the rename.
};

/// Options controlling the behavior of writeFileAtomic().
class WriteFileAtomicOptions {
 public:
  /// Constructs the options with default values.
  WriteFileAtomicOptions() = default;

  mode_t permissions{0644}; ///< Permission bits for the temporary file.
  SyncType syncType{SyncType::WITHOUT_SYNC}; ///< Whether to sync before rename.
  std::string temporaryDirectory; ///< Directory for the temporary file.

  /// Sets the mode bits used for the temporary file.
  /// \param permissions The permission bits to use.
  /// \returns A reference to this options object for chaining.
  WriteFileAtomicOptions& setPermissions(mode_t permissions);

  // The default implementation does not sync the data to storage before the
  // rename.  Therefore, the write is *not* atomic in the event of a power
  // failure or OS crash.  To guarantee atomicity in these cases, specify
  // syncType = WITH_SYNC, which will incur a performance cost of waiting for
  // the data to be persisted to storage.  Note that the return of the function
  // does not guarantee the directory modifications have been written to disk; a
  // further sync of the directory after the function returns is required to
  // ensure the modification is durable.
  /// Sets whether the data is synced to storage before the rename.
  /// \param syncType The sync behavior to use.
  /// \returns A reference to this options object for chaining.
  WriteFileAtomicOptions& setSyncType(SyncType syncType);

  // The implementation creates a temporary file as an implementation detail
  // within this directory.  The temporary filenames themselves are
  // implementation defined.
  /// Sets the directory used for the temporary file.
  /// \param temporaryDirectory The directory to create the temporary file in.
  /// \returns A reference to this options object for chaining.
  WriteFileAtomicOptions& setTemporaryDirectory(std::string temporaryDirectory);
};

/*
 * writeFileAtomic() does not currently work on Windows.
 * Windows does not provide atomic file renames, which makes implementing this
 * tricky.  Windows does have a MoveFileTransactedA() API which could
 * potentially be used, but according to the Microsoft documentation this API is
 * discouraged and may be removed in a future version.
 *
 * In order to implement this properly on Windows we would probably need a pair
 * of functions: one for writing the file, and one for reading the contents,
 * where the two functions synchronize with each other.  We can probably only
 * provide atomic update behavior with cooperation from the reader.
 */
#ifndef _WIN32

/**
 * Write file contents "atomically".
 *
 * This writes the data to a temporary file in the destination directory, and
 * then renames it to the specified path.  This guarantees that the specified
 * file will be replaced the specified contents on success, or will not be
 * modified on failure.
 *
 * Note that on platforms that do not provide atomic filesystem rename
 * functionality (e.g., Windows) this behavior may not be truly atomic.
 *
 * The default implementation does not sync the data to storage before the
 * rename.  Therefore, the write is *not* atomic in the event of a power failure
 * or OS crash.  To guarantee atomicity in these cases, specify syncType =
 * WITH_SYNC, which will incur a performance cost of waiting for the data to be
 * persisted to storage.  Note that the return of the function does not
 * guarantee the directory modifications have been written to disk; a further
 * sync of the directory after the function returns is required to ensure the
 * modification is durable.
 *
 * \param filePath Destination path to write.
 * \param iov Array of buffers holding the data to write.
 * \param count Number of entries in iov.
 * \param permissions Permission bits used for the file.
 * \param syncType Whether to sync the data to storage before the rename.
 */
void writeFileAtomic(
    StringPiece filePath,
    iovec* iov,
    int count,
    mode_t permissions = 0644,
    SyncType syncType = SyncType::WITHOUT_SYNC);
/// Atomically writes a byte range to a file.
/// \param filePath Destination path to write.
/// \param data The data to write.
/// \param permissions Permission bits used for the file.
/// \param syncType Whether to sync the data to storage before the rename.
void writeFileAtomic(
    StringPiece filePath,
    ByteRange data,
    mode_t permissions = 0644,
    SyncType syncType = SyncType::WITHOUT_SYNC);
/// Atomically writes a string to a file.
/// \param filePath Destination path to write.
/// \param data The data to write.
/// \param permissions Permission bits used for the file.
/// \param syncType Whether to sync the data to storage before the rename.
void writeFileAtomic(
    StringPiece filePath,
    StringPiece data,
    mode_t permissions = 0644,
    SyncType syncType = SyncType::WITHOUT_SYNC);

/// Atomically writes a string to a file using the given options.
/// \param filePath Destination path to write.
/// \param data The data to write.
/// \param options Options controlling permissions, sync, and temp directory.
void writeFileAtomic(
    StringPiece filePath,
    StringPiece data,
    const WriteFileAtomicOptions& options);

/**
 * A version of writeFileAtomic() that returns an errno value instead of
 * throwing on error.
 *
 * Returns 0 on success or an errno value on error.
 *
 * \param filePath Destination path to write.
 * \param iov Array of buffers holding the data to write.
 * \param count Number of entries in iov.
 * \param permissions Permission bits used for the file.
 * \param syncType Whether to sync the data to storage before the rename.
 * \returns 0 on success, or an errno value on error.
 */
int writeFileAtomicNoThrow(
    StringPiece filePath,
    iovec* iov,
    int count,
    mode_t permissions = 0644,
    SyncType syncType = SyncType::WITHOUT_SYNC);

/// A non-throwing writeFileAtomic() using the given options.
/// \param filePath Destination path to write.
/// \param data The data to write.
/// \param options Options controlling permissions, sync, and temp directory.
/// \returns 0 on success, or an errno value on error.
int writeFileAtomicNoThrow(
    StringPiece filePath,
    StringPiece data,
    const WriteFileAtomicOptions& options);

#endif // !_WIN32

} // namespace folly
