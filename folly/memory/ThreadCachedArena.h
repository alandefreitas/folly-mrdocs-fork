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

#include <type_traits>

#include <folly/Likely.h>
#include <folly/Synchronized.h>
#include <folly/ThreadLocal.h>
#include <folly/memory/Allocator.h>
#include <folly/memory/Arena.h>

namespace folly {

/**
 * Thread-caching arena: allocate memory which gets freed when the arena gets
 * destroyed.
 *
 * The arena itself allocates memory using malloc() in blocks of
 * at least minBlockSize bytes.
 *
 * For speed, each thread gets its own Arena (see Arena.h); when threads
 * exit, the Arena gets merged into a "zombie" Arena, which will be deallocated
 * when the ThreadCachedArena object is destroyed.
 */
class ThreadCachedArena {
 public:
  /// Constructs a thread-caching arena.
  ///
  /// \param minBlockSize Minimum size, in bytes, of each per-thread block.
  /// \param maxAlign Maximum alignment, in bytes, honored by allocations.
  explicit ThreadCachedArena(
      size_t minBlockSize = SysArena::kDefaultMinBlockSize,
      size_t maxAlign = SysArena::kDefaultMaxAlign);

  /// Allocates memory from the calling thread's arena.
  ///
  /// \param size Number of bytes to allocate.
  /// \returns A pointer to the allocated memory.
  void* allocate(size_t size) {
    SysArena* arena = arena_.get();
    if (FOLLY_UNLIKELY(!arena)) {
      arena = allocateThreadLocalArena();
    }

    return arena->allocate(size);
  }

  /// Releases memory back to the arena; this is intentionally a no-op.
  ///
  /// \param p Pointer previously returned by allocate() (ignored).
  /// \param size Size of the allocation (ignored).
  void deallocate(void* p, size_t size = 0) {
    // Deallocate? Never!
  }

  /// Gets the total memory used by the arena.
  ///
  /// \returns The total number of bytes owned across all per-thread arenas.
  size_t totalSize() const;

 private:
  struct ThreadLocalPtrTag {};

  ThreadCachedArena(const ThreadCachedArena&) = delete;
  ThreadCachedArena(ThreadCachedArena&&) = delete;
  ThreadCachedArena& operator=(const ThreadCachedArena&) = delete;
  ThreadCachedArena& operator=(ThreadCachedArena&&) = delete;

  SysArena* allocateThreadLocalArena();

  // Zombify the blocks in arena, saving them for deallocation until
  // the ThreadCachedArena is destroyed.
  void zombify(SysArena&& arena);

  const size_t minBlockSize_;
  const size_t maxAlign_;

  ThreadLocalPtr<SysArena, ThreadLocalPtrTag> arena_; // Per-thread arena.

  // Allocations from threads that are now dead.
  Synchronized<SysArena> zombies_;
};

template <>
struct AllocatorHasTrivialDeallocate<ThreadCachedArena> : std::true_type {};

/// Standard-conforming allocator backed by a ThreadCachedArena.
template <typename T>
using ThreadCachedArenaAllocator = CxxAllocatorAdaptor<T, ThreadCachedArena>;

} // namespace folly
