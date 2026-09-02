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

// http://www.canonware.com/download/jemalloc/jemalloc-latest/doc/jemalloc.html

#pragma once

#include <cstddef>
#include <cstdint>

namespace folly {

/**
 * An allocator which uses Jemalloc to create a dedicated huge page arena,
 * backed by 2MB huge pages (on linux x86-64).
 *
 * This allocator is specifically intended for linux with the transparent
 * huge page support set to 'madvise' and defrag policy set to 'madvise'
 * or 'defer+madvise'.
 * These can be controller via /sys/kernel/mm/transparent_hugepage/enabled
 * and /sys/kernel/mm/transparent_hugepage/defrag.
 *
 * The allocator reserves a fixed-size area using mmap, and sets the
 * MADV_HUGEPAGE page attribute using the madvise system call.
 * A custom jemalloc hook is installed which is called when creating a new
 * extent of memory. This will allocate from the reserved area if possible,
 * and otherwise fall back to the default method.
 * Jemalloc does not use allocated extents across different arenas without
 * first unmapping them, and the advice flags are cleared on munmap.
 * A regular malloc will never end up allocating memory from this arena.
 *
 * If binary isn't linked with jemalloc, the logic falls back to malloc / free.
 *
 * Please note that as per kernel contract, page faults on an madvised region
 * will block, so we pre-allocate all the huge pages by touching the pages.
 * So, please only allocate as much you need as this will never be freed
 * during the lifetime of the application. If we run out of the free huge pages,
 * then huge page allocator falls back to the 4K regular pages.
 *
 * 1GB Huge Pages are not supported at this point.
 */
class JemallocHugePageAllocator {
 public:
  /// Initializes the arena with a default number of initial and max pages.
  ///
  /// \returns `true` if the arena was successfully initialized.
  static bool default_init();

  /// Initializes the arena with explicit initial and maximum page counts.
  ///
  /// \param initial_nr_pages The number of huge pages to pre-allocate.
  /// \param max_nr_pages The maximum number of huge pages the arena may use.
  /// \returns `true` if the arena was successfully initialized.
  static bool init(int initial_nr_pages, int max_nr_pages = 0);

  /// Allocates a block of memory from the huge page arena.
  ///
  /// \param size The number of bytes to allocate.
  /// \returns A pointer to the allocated memory.
  static void* allocate(size_t size);

  /// Resizes a previously allocated block of memory.
  ///
  /// \param p A pointer to the block to resize.
  /// \param size The new size, in bytes.
  /// \returns A pointer to the resized memory.
  static void* reallocate(void* p, size_t size);

  /// Frees a block of memory previously allocated from the arena.
  ///
  /// \param p A pointer to the block to free.
  /// \param size The size of the block, in bytes.
  static void deallocate(void* p, size_t size = 0);

  /// Determines whether the arena has been initialized.
  ///
  /// \returns `true` if the arena has been initialized.
  static bool initialized();

  /// Returns the amount of unused space in the arena.
  ///
  /// \returns The free space, in bytes.
  static size_t freeSpace();

  /// Determines whether an address lies within the arena.
  ///
  /// \param address The address to test.
  /// \returns `true` if the address is within the arena.
  static bool addressInArena(void* address);

 private:
  static bool hugePagesAllocSupported();

  static int flags_;
};

/// STL compatible huge page allocator, for use with STL-style containers.
///
/// \tparam T The type of object to allocate.
template <typename T>
class CxxHugePageAllocator {
 private:
  using Self = CxxHugePageAllocator<T>;

 public:
  /// The type of object allocated by this allocator.
  using value_type = T;

  /// Constructs an allocator.
  CxxHugePageAllocator() {}

  /// Constructs an allocator from one for a different value type.
  ///
  /// \tparam U The value type of the other allocator.
  /// \param other The allocator to copy from.
  template <typename U>
  explicit CxxHugePageAllocator(CxxHugePageAllocator<U> const& other) {}

  /// Allocates storage for `n` objects from the huge page arena.
  ///
  /// \param n The number of objects to allocate.
  /// \returns A pointer to the allocated storage.
  T* allocate(std::size_t n) {
    return static_cast<T*>(JemallocHugePageAllocator::allocate(sizeof(T) * n));
  }
  /// Frees storage previously allocated from the huge page arena.
  ///
  /// \param p A pointer to the storage to free.
  /// \param n The number of objects the storage held.
  void deallocate(T* p, std::size_t n) {
    JemallocHugePageAllocator::deallocate(p, sizeof(T) * n);
  }

  /// Compares two allocators for equality.
  ///
  /// \param lhs The left-hand allocator.
  /// \param rhs The right-hand allocator.
  /// \returns `true`, as all instances are interchangeable.
  friend bool operator==(Self const& lhs, Self const& rhs) noexcept { return true; }
  /// Compares two allocators for inequality.
  ///
  /// \param lhs The left-hand allocator.
  /// \param rhs The right-hand allocator.
  /// \returns `false`, as all instances are interchangeable.
  friend bool operator!=(Self const& lhs, Self const& rhs) noexcept { return false; }
};

} // namespace folly
