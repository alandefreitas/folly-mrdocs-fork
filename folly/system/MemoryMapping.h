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

#include <cassert>

#include <folly/File.h>
#include <folly/Range.h>
#include <folly/portability/Unistd.h>

namespace folly {

/**
 * Maps files in memory (read-only).
 */
class MemoryMapping {
 public:
  /**
   * Lock the pages in memory?
   * TRY_LOCK  = try to lock, log warning if permission denied
   * MUST_LOCK = lock, fail assertion if permission denied.
   */
  enum class LockMode {
    TRY_LOCK, ///< Try to lock; log a warning if permission is denied.
    MUST_LOCK, ///< Lock; fail an assertion if permission is denied.
  };

  /// Flags controlling how pages are locked into memory.
  struct LockFlags {
    /// Constructs lock flags with default values.
    LockFlags() {}

    /**
     * Instead of locking all the pages in the mapping before the call returns,
     * only lock those that are currently resident and mark the others to be
     * locked at the time they're populated by their first page fault.
     *
     * Uses mlock2(flags=MLOCK_ONFAULT). Requires Linux >= 4.4.
     */
    bool lockOnFault = false;

    /**
     * Call madvise(MADV_POPULATE_READ) + madvise(MADV_COLLAPSE) before mlock().
     * See man 7 mmap for MADV_COLLAPSE details: it will synchronously allocate
     * a THP 2MB page, fault in the existing 4KB pages and copy them into the
     * THP page all in a best effort manner. Per the man page, at least one
     * consituent 4KB page needs to be present but using a prior
     * MADV_POPULATE_READ gives better chances of this collapse. On error
     * proceeds to mlock/mlock2. Requires Linux >= 6.1.
     */
    bool tryCollapseToTHP = false;
  };

  /**
   * Map a portion of the file indicated by filename in memory, causing SIGABRT
   * on error.
   *
   * By default, map the whole file.  length=-1: map from offset to EOF.
   * Unlike the mmap() system call, offset and length don't need to be
   * page-aligned.  length is clipped to the end of the file if it's too large.
   *
   * The mapping will be destroyed (and the memory pointed-to by data() will
   * likely become inaccessible) when the MemoryMapping object is destroyed.
   */
  struct Options {
    /// Constructs options with default values.
    Options() {}

    /// Sets the page size to use for the mapping.
    ///
    /// \param v The page size in bytes, or 0 for the default.
    /// \returns A reference to these options for chaining.
    Options& setPageSize(off_t v) {
      pageSize = v;
      return *this;
    }
    /// Sets whether the mapping is shared with other processes.
    ///
    /// \param v `true` for a shared mapping, `false` for private.
    /// \returns A reference to these options for chaining.
    Options& setShared(bool v) {
      shared = v;
      return *this;
    }
    /// Sets whether to populate page tables up front.
    ///
    /// \param v `true` to prefault the pages.
    /// \returns A reference to these options for chaining.
    Options& setPrefault(bool v) {
      prefault = v;
      return *this;
    }
    /// Sets whether the mapped pages are readable.
    ///
    /// \param v `true` to map the pages readable.
    /// \returns A reference to these options for chaining.
    Options& setReadable(bool v) {
      readable = v;
      return *this;
    }
    /// Sets whether the mapped pages are writable.
    ///
    /// \param v `true` to map the pages writable.
    /// \returns A reference to these options for chaining.
    Options& setWritable(bool v) {
      writable = v;
      return *this;
    }
    /// Sets whether to grow the file to the requested length.
    ///
    /// \param v `true` to grow the file before mapping.
    /// \returns A reference to these options for chaining.
    Options& setGrow(bool v) {
      grow = v;
      return *this;
    }

    /// Page size. 0 = use appropriate page size.
    /// (On Linux, we use a huge page size if the file is on a hugetlbfs
    /// file system, and the default page size otherwise)
    off64_t pageSize = 0;

    /// If shared (default), the memory mapping is shared with other processes
    /// mapping the same file (or children); if not shared (private), each
    /// process has its own mapping. Changes in writable, private mappings are
    /// not reflected to the underlying file. See the discussion of
    /// MAP_PRIVATE vs MAP_SHARED in the mmap(2) manual page.
    bool shared = true;

    /// Populate page tables; subsequent accesses should not be blocked
    /// by page faults. This is a hint, as it may not be supported.
    bool prefault = false;

    /// Map the pages readable. Note that mapping pages without read permissions
    /// is not universally supported (not supported on hugetlbfs on Linux, for
    /// example)
    bool readable = true;

    /// Map the pages writable.
    bool writable = false;

    /// When mapping a file in writable mode, grow the file to the requested
    /// length (using ftruncate()) before mapping; if false, truncate the
    /// mapping to the actual file size instead.
    bool grow = false;

    /// Fix map at this address, if not nullptr. Must be aligned to a multiple
    /// of the appropriate page size.
    void* address = nullptr;
  };

  /// Options to emulate the old WritableMemoryMapping: readable and writable,
  /// allow growing the file if mapping past EOF.
  ///
  /// \returns Options configured for a writable, growable mapping.
  static Options writable() {
    return Options().setWritable(true).setGrow(true);
  }

  /// Selector tag type for the anonymous-mapping constructor.
  enum AnonymousType {
    kAnonymous, ///< The anonymous-mapping selector value.
  };

  /**
   * Create an anonymous mapping.
   *
   * \param anon Tag selecting the anonymous-mapping constructor.
   * \param length Number of bytes to map.
   * \param options Mapping options.
   */
  MemoryMapping(AnonymousType anon, off64_t length, Options options = Options());

  /// Maps a portion of an already-open file.
  ///
  /// \param file The file to map.
  /// \param offset Byte offset into the file where the mapping starts.
  /// \param length Number of bytes to map, or -1 to map to end of file.
  /// \param options Mapping options.
  explicit MemoryMapping(
      File file,
      off64_t offset = 0,
      off64_t length = -1,
      Options options = Options());

  /// Maps a portion of the file with the given name.
  ///
  /// \param name Path of the file to map.
  /// \param offset Byte offset into the file where the mapping starts.
  /// \param length Number of bytes to map, or -1 to map to end of file.
  /// \param options Mapping options.
  explicit MemoryMapping(
      const char* name,
      off64_t offset = 0,
      off64_t length = -1,
      Options options = Options());

  /// Maps a portion of an open file referenced by a descriptor.
  ///
  /// \param fd File descriptor of the file to map.
  /// \param offset Byte offset into the file where the mapping starts.
  /// \param length Number of bytes to map, or -1 to map to end of file.
  /// \param options Mapping options.
  explicit MemoryMapping(
      int fd,
      off64_t offset = 0,
      off64_t length = -1,
      Options options = Options());

  /// Deleted copy constructor; mappings are not copyable.
  ///
  /// \param other The mapping that would be copied.
  MemoryMapping(const MemoryMapping& other) = delete;

  /// Move-constructs from another mapping.
  ///
  /// \param other The mapping to move from.
  MemoryMapping(MemoryMapping&& other) noexcept;

  /// Destroys the mapping, unmapping the memory.
  ~MemoryMapping();

  /// Deleted copy assignment; mappings are not copyable.
  ///
  /// \param other The mapping that would be copied.
  /// \returns A reference to this mapping.
  MemoryMapping& operator=(const MemoryMapping& other) = delete;

  /// Move-assigns from another mapping.
  ///
  /// \param other The mapping to move from.
  /// \returns A reference to this mapping.
  MemoryMapping& operator=(MemoryMapping&& other) noexcept;

  /// Swaps the contents of this mapping with another.
  ///
  /// \param other The mapping to swap with.
  void swap(MemoryMapping& other) noexcept;

  /**
   * Lock the pages in memory
   *
   * \param mode Whether a locking failure is fatal.
   * \param flags Additional locking behaviour flags.
   * \returns `true` if the pages were locked, `false` otherwise.
   */
  bool mlock(LockMode mode, LockFlags flags = {});

  /**
   * Unlock the pages.
   * If dontneed is true, the kernel is instructed to release these pages
   * (per madvise(MADV_DONTNEED)).
   *
   * \param dontneed If `true`, release the pages via `madvise(MADV_DONTNEED)`.
   */
  void munlock(bool dontneed = false);

  /**
   * Hint that these pages will be scanned linearly.
   * madvise(MADV_SEQUENTIAL)
   */
  void hintLinearScan();

  /**
   * Advise the kernel about memory access.
   *
   * \param advice The `madvise` advice value to apply.
   */
  void advise(int advice) const;

  /// Advise the kernel about access to a sub-range of the mapping.
  ///
  /// \param advice The `madvise` advice value to apply.
  /// \param offset Byte offset into the mapping where the advice starts.
  /// \param length Number of bytes the advice applies to.
  void advise(int advice, size_t offset, size_t length) const;

  /**
   * A bitwise cast of the mapped bytes as range of values. Only intended for
   * use with POD or in-place usable types.
   *
   * \returns The mapped bytes reinterpreted as a range of `T`.
   */
  template <class T>
  Range<const T*> asRange() const {
    size_t count = data_.size() / sizeof(T);
    return Range<const T*>(
        static_cast<const T*>(static_cast<const void*>(data_.data())), count);
  }

  /**
   * A range of bytes mapped by this mapping.
   *
   * \returns The mapped bytes as a byte range.
   */
  ByteRange range() const { return data_; }

  /**
   * A bitwise cast of the mapped bytes as range of mutable values. Only
   * intended for use with POD or in-place usable types.
   *
   * \returns The mapped bytes reinterpreted as a mutable range of `T`.
   */
  template <class T>
  Range<T*> asWritableRange() const {
    assert(options_.writable); // you'll segfault anyway...
    size_t count = data_.size() / sizeof(T);
    return Range<T*>(static_cast<T*>(static_cast<void*>(data_.data())), count);
  }

  /**
   * A range of mutable bytes mapped by this mapping.
   *
   * \returns The mapped bytes as a mutable range.
   */
  MutableByteRange writableRange() const {
    assert(options_.writable); // you'll segfault anyway...
    return data_;
  }

  /**
   * Return the memory area where the file was mapped.
   * Deprecated; use range() instead.
   *
   * \returns The mapped memory as a `StringPiece`.
   */
  StringPiece data() const { return asRange<const char>(); }

  /// Reports whether the mapping is currently locked in memory.
  ///
  /// \returns `true` if the pages are locked, `false` otherwise.
  bool mlocked() const { return locked_; }

  /// Returns the file descriptor backing this mapping.
  ///
  /// \returns The underlying file descriptor.
  int fd() const { return file_.fd(); }

 private:
  MemoryMapping();

  enum InitFlags {
    kGrow = 1 << 0,
    kAnon = 1 << 1,
  };
  void init(off64_t offset, off64_t length);

  File file_;
  void* mapStart_ = nullptr;
  off64_t mapLength_ = 0;
  Options options_;
  bool locked_ = false;
  MutableByteRange data_;
};

/// Swaps the contents of two memory mappings.
///
/// \param a First mapping to swap.
/// \param b Second mapping to swap.
void swap(MemoryMapping& a, MemoryMapping& b) noexcept;

/**
 * A special case of memcpy() that always copies memory forwards.
 * (libc's memcpy() is allowed to copy memory backwards, and will do so
 * when using SSSE3 instructions).
 *
 * Assumes src and dest are aligned to alignof(unsigned long).
 *
 * Useful when copying from/to memory mappings after hintLinearScan();
 * copying backwards renders any prefetching useless (even harmful).
 *
 * \param dst Destination buffer, aligned to `alignof(unsigned long)`.
 * \param src Source buffer, aligned to `alignof(unsigned long)`.
 * \param size Number of bytes to copy.
 */
void alignedForwardMemcpy(void* dst, const void* src, size_t size);

/**
 * Copy a file using mmap(). Overwrites dest.
 *
 * \param src Path of the source file to copy.
 * \param dest Path of the destination file to overwrite.
 * \param mode Permission bits for the destination file.
 */
void mmapFileCopy(const char* src, const char* dest, mode_t mode = 0666);

/**
 * mlock2 is Linux-only and exists since Linux 4.4
 * On Linux pre-4.4 and other platforms fail with ENOSYS.
 * glibc added the mlock2 wrapper in 2.27
 * https://lists.gnu.org/archive/html/info-gnu/2018-02/msg00000.html
 *
 * \param addr Start address of the range to lock.
 * \param len Length in bytes of the range to lock.
 * \param flags Locking flags passed to `mlock2`.
 * \returns 0 on success, or -1 with `errno` set on failure.
 */
int mlock2wrapper(const void* addr, size_t len, MemoryMapping::LockFlags flags);

} // namespace folly
