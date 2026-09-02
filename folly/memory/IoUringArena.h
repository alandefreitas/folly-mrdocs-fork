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

#include <cstddef>

/// Facebook's Folly library namespace.
namespace folly {

/// A memory arena registered with the kernel for io_uring operations.
class IoUringArena {
 public:
  /// Initializes the arena with the given size.
  ///
  /// \param size The size of the arena, in bytes.
  /// \returns `true` if the arena was successfully initialized.
  static bool init(size_t size);

  /// Allocates a block of memory from the arena.
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

  /// Determines whether an address lies within the arena.
  ///
  /// \param address The address to test.
  /// \returns `true` if the address is within the arena.
  static bool addressInArena(void* address);

  /// Returns the base address of the arena.
  ///
  /// \returns A pointer to the start of the arena region.
  static void* base();

  /// Returns the size of the arena region.
  ///
  /// \returns The size of the region, in bytes.
  static size_t regionSize();

  /// Returns the amount of unused space in the arena.
  ///
  /// \returns The free space, in bytes.
  static size_t freeSpace();

  /// Returns the jemalloc arena index used by this arena.
  ///
  /// \returns The arena index.
  static unsigned arenaIndex();

  /// Returns the allocation flags used for jemalloc calls.
  ///
  /// \returns The flags bitmask.
  static int flags();

  /// Determines whether io_uring arena support is available.
  ///
  /// \returns `true` if the io_uring arena is supported.
  static bool ioUringArenaSupported();

 private:
  static int flags_;
};

/// An STL-compatible allocator backed by the io_uring arena.
///
/// \tparam T The type of object to allocate.
template <typename T>
class CxxIoUringAllocator {
 public:
  /// The type of object allocated by this allocator.
  using value_type = T;

  /// Constructs an allocator.
  CxxIoUringAllocator() = default;

  /// Constructs an allocator from one for a different value type.
  ///
  /// \tparam U The value type of the other allocator.
  /// \param other The allocator to copy from.
  template <typename U>
  explicit CxxIoUringAllocator(CxxIoUringAllocator<U> const& other) {}

  /// Allocates storage for `n` objects from the arena.
  ///
  /// \param n The number of objects to allocate.
  /// \returns A pointer to the allocated storage.
  T* allocate(std::size_t n) {
    return static_cast<T*>(IoUringArena::allocate(sizeof(T) * n));
  }

  /// Frees storage previously allocated from the arena.
  ///
  /// \param p A pointer to the storage to free.
  /// \param n The number of objects the storage held.
  void deallocate(T* p, std::size_t n) {
    IoUringArena::deallocate(p, sizeof(T) * n);
  }

  /// Compares two allocators for equality.
  ///
  /// \param lhs The left-hand allocator.
  /// \param rhs The right-hand allocator.
  /// \returns `true`, as all instances are interchangeable.
  friend bool operator==(
      CxxIoUringAllocator const& lhs, CxxIoUringAllocator const& rhs) noexcept {
    return true;
  }

  /// Compares two allocators for inequality.
  ///
  /// \param lhs The left-hand allocator.
  /// \param rhs The right-hand allocator.
  /// \returns `false`, as all instances are interchangeable.
  friend bool operator!=(
      CxxIoUringAllocator const& lhs, CxxIoUringAllocator const& rhs) noexcept {
    return false;
  }
};

} // namespace folly
