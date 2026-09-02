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

#include <folly/CPortability.h>
#include <folly/portability/Config.h>
#include <folly/portability/Malloc.h>

#if defined(FOLLY_USE_JEMALLOC) && (!defined(FOLLY_SANITIZE) || !FOLLY_SANITIZE)

#include <folly/portability/SysMman.h>

#if (JEMALLOC_VERSION_MAJOR > 3) && defined(MADV_DONTDUMP)
#define FOLLY_JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED 1
#if (JEMALLOC_VERSION_MAJOR == 4)
#define FOLLY_JEMALLOC_NODUMP_ALLOCATOR_CHUNK
#define JEMALLOC_CHUNK_OR_EXTENT chunk
#else
#define FOLLY_JEMALLOC_NODUMP_ALLOCATOR_EXTENT
#define JEMALLOC_CHUNK_OR_EXTENT extent
#endif
#endif

#endif // FOLLY_USE_JEMALLOC

#include <cstddef>

namespace folly {

/**
 * An allocator which uses Jemalloc to create an dedicated arena to allocate
 * memory from. The only special property set on the allocated memory is that
 * the memory is not dump-able.
 *
 * This is done by setting MADV_DONTDUMP using the `madvise` system call. A
 * custom hook installed which is called when allocating a new chunk / extent of
 * memory.  All it does is call the original jemalloc hook to allocate the
 * memory and then set the advise on it before returning the pointer to the
 * allocated memory. Jemalloc does not use allocated chunks / extents across
 * different arenas, without `munmap`-ing them first, and the advises are not
 * sticky i.e. they are unset if `munmap` is done. Also this arena can't be used
 * by any other part of the code by just calling `malloc`.
 *
 * If target system doesn't support MADV_DONTDUMP or jemalloc doesn't support
 * custom arena hook, JemallocNodumpAllocator would fall back to using malloc /
 * free. Such behavior can be identified by using
 * !defined(FOLLY_JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED).
 *
 * Similarly, if binary isn't linked with jemalloc, the logic would fall back to
 * malloc / free.
 */
class JemallocNodumpAllocator {
 public:
  /// Whether the dedicated nodump arena is enabled.
  enum class State {
    ENABLED, ///< Allocations use the dedicated nodump arena.
    DISABLED, ///< Allocations fall back to plain malloc / free.
  };

  /// Frees memory previously allocated by this allocator.
  ///
  /// To be used as `IOBuf::FreeFunction`, `userData` should be set to
  /// `reinterpret_cast<void*>(getFlags())`.
  ///
  /// \param p Pointer to the memory to free.
  /// \param userData The allocator flags, as returned by `getFlags()`.
  static void deallocate(void* p, void* userData);

  /// Constructs the allocator, optionally in a disabled state.
  ///
  /// \param state Whether the dedicated nodump arena is enabled.
  explicit JemallocNodumpAllocator(State state = State::ENABLED);

  /// Allocates a block of memory from the dedicated arena.
  ///
  /// \param size Number of bytes to allocate.
  /// \returns A pointer to the allocated memory, or `nullptr` on failure.
  void* allocate(size_t size);

  /// Resizes a block of memory previously allocated from the arena.
  ///
  /// \param p Pointer to the memory to resize.
  /// \param size The new size in bytes.
  /// \returns A pointer to the reallocated memory, or `nullptr` on failure.
  void* reallocate(void* p, size_t size);

  /// Frees memory previously allocated by this allocator.
  ///
  /// \param p Pointer to the memory to free.
  /// \param size Size of the block, unused; present for interface parity.
  void deallocate(void* p, size_t size = 0);

  /// Returns the jemalloc arena index used by this allocator.
  ///
  /// \returns The arena index.
  unsigned getArenaIndex() const { return arena_index_; }

  /// Returns the jemalloc allocation flags used by this allocator.
  ///
  /// \returns The allocation flags.
  int getFlags() const { return flags_; }

 private:
#ifdef FOLLY_JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED
#ifdef FOLLY_JEMALLOC_NODUMP_ALLOCATOR_CHUNK
  static chunk_alloc_t* original_alloc_;
  static void* alloc(
      void* chunk,
#else
  static extent_hooks_t extent_hooks_;
  static extent_alloc_t* original_alloc_;
  static void* alloc(
      extent_hooks_t* extent,
      void* new_addr,
#endif
      size_t size,
      size_t alignment,
      bool* zero,
      bool* commit,
      unsigned arena_ind);
#endif // FOLLY_JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED

  bool extend_and_setup_arena();

  unsigned arena_index_{0};
  int flags_{0};
};

/**
 * JemallocNodumpAllocator singleton.
 *
 * \returns A reference to the process-wide allocator instance.
 */
JemallocNodumpAllocator& globalJemallocNodumpAllocator();

} // namespace folly
