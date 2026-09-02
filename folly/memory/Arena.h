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
#define FOLLY_ARENA_H_

#include <cassert>
#include <limits>
#include <stdexcept>
#include <utility>

#include <boost/intrusive/slist.hpp>

#include <folly/Conv.h>
#include <folly/Likely.h>
#include <folly/Memory.h>
#include <folly/lang/Align.h>
#include <folly/lang/CheckedMath.h>
#include <folly/lang/Exception.h>
#include <folly/memory/Allocator.h>
#include <folly/memory/Malloc.h>

namespace folly {

/**
 * Simple arena: allocate memory which gets freed when the arena gets
 * destroyed.
 *
 * The arena itself allocates memory using a custom allocator which conforms
 * to the C++ concept Allocator.
 *
 *   http://en.cppreference.com/w/cpp/concept/Allocator
 *
 * You may also specialize ArenaAllocatorTraits for your allocator type to
 * provide:
 *
 *   size_t goodSize(const Allocator& alloc, size_t size) const;
 *      Return a size (>= the provided size) that is considered "good" for your
 *      allocator (for example, if your allocator allocates memory in 4MB
 *      chunks, size should be rounded up to 4MB).  The provided value is
 *      guaranteed to be rounded up to a multiple of the maximum alignment
 *      required on your system; the returned value must be also.
 *
 * An implementation that uses malloc() / free() is defined below, see SysArena.
 */
template <class Alloc>
struct ArenaAllocatorTraits;
/// Arena that allocates memory in blocks and frees it all at destruction.
///
/// Memory obtained from allocate() stays alive until the arena is cleared or
/// destroyed; individual deallocations are no-ops. Blocks come from the
/// user-supplied allocator \p Alloc.
template <class Alloc>
class Arena {
 public:
  /// Constructs an arena backed by the given allocator.
  ///
  /// \param alloc Allocator used to obtain memory blocks.
  /// \param minBlockSize Minimum size, in bytes, of each regular block.
  /// \param sizeLimit Soft cap on the total allocated size, or kNoSizeLimit for
  ///   no limit.
  /// \param maxAlign Maximum alignment, in bytes, honored by allocate().
  explicit Arena(
      const Alloc& alloc,
      size_t minBlockSize = kDefaultMinBlockSize,
      size_t sizeLimit = kNoSizeLimit,
      size_t maxAlign = kDefaultMaxAlign)
      : allocAndSize_(alloc, minBlockSize),
        currentBlock_(blocks_.last()),
        ptr_(nullptr),
        end_(nullptr),
        totalAllocatedSize_(0),
        bytesUsed_(0),
        sizeLimit_(sizeLimit),
        maxAlign_(maxAlign) {
    if (!valid_align_value(maxAlign_)) {
      throw_exception<std::invalid_argument>(
          folly::to<std::string>("Invalid maxAlign: ", maxAlign_));
    }
  }

  /// Destroys the arena, freeing every block it owns.
  ~Arena() {
    freeBlocks();
    freeLargeBlocks();
  }

  /// Allocates a properly aligned chunk of memory from the arena.
  ///
  /// \param size Number of bytes to allocate.
  /// \returns A pointer to the allocated memory.
  void* allocate(size_t size) {
    size = roundUp(size);
    bytesUsed_ += size;

    assert(ptr_ <= end_);
    if (FOLLY_LIKELY((size_t)(end_ - ptr_) >= size)) {
      // Fast path: there's enough room in the current block
      char* r = ptr_;
      ptr_ += size;
      assert(isAligned(r));
      return r;
    }

    if (canReuseExistingBlock(size)) {
      currentBlock_++;
      char* r = align(currentBlock_->start());
      ptr_ = r + size;
      end_ = currentBlock_->start() + blockGoodAllocSize() - sizeof(Block);
      assert(ptr_ <= end_);
      assert(isAligned(r));
      return r;
    }

    // Not enough room in the current block
    void* r = allocateSlow(size);
    assert(isAligned(r));
    return r;
  }

  /// Releases memory back to the arena; this is intentionally a no-op.
  ///
  /// \param p Pointer previously returned by allocate() (ignored).
  /// \param size Size of the allocation (ignored).
  void deallocate(void* p, size_t size = 0) {
    // Deallocate? Never!
  }

  /// Transfers ownership of all memory allocated from \p other to this arena.
  ///
  /// \param other Arena whose memory is taken over.
  void merge(Arena&& other);

  /// Resets the arena, reusing regular blocks and freeing large ones.
  void clear() {
    bytesUsed_ = 0;
    freeLargeBlocks(); // We don't reuse large blocks
    if (blocks_.empty()) {
      return;
    }
    currentBlock_ = blocks_.begin();
    ptr_ = align(currentBlock_->start());
    end_ = currentBlock_->start() + blockGoodAllocSize() - sizeof(Block);
    assert(ptr_ <= end_);
  }

  /// Gets the total memory used by the arena.
  ///
  /// \returns The total number of bytes owned by the arena, including overhead.
  size_t totalSize() const { return totalAllocatedSize_ + sizeof(Arena); }

  /// Gets the total number of "used" bytes.
  ///
  /// These are bytes that the arena users allocated via the calls to
  /// `allocate`. Doesn't include fragmentation, e.g. if block size is 4KB and
  /// you allocate 2 objects of 3KB in size, `bytesUsed()` will be 6KB, while
  /// `totalSize()` will be 8KB+.
  ///
  /// \returns The number of bytes handed out by allocate().
  size_t bytesUsed() const { return bytesUsed_; }

  // not copyable or movable
  /// Deleted copy constructor; Arena is not copyable.
  ///
  /// \param other The Arena that would be copied.
  Arena(const Arena& other) = delete;
  /// Deleted copy assignment; Arena is not copyable.
  ///
  /// \param other The Arena that would be assigned from.
  /// \returns A reference to this Arena.
  Arena& operator=(const Arena& other) = delete;
  /// Deleted move constructor; Arena is not movable.
  ///
  /// \param other The Arena that would be moved from.
  Arena(Arena&& other) = delete;
  /// Deleted move assignment; Arena is not movable.
  ///
  /// \param other The Arena that would be move-assigned from.
  /// \returns A reference to this Arena.
  Arena& operator=(Arena&& other) = delete;

 private:
  using AllocTraits =
      typename std::allocator_traits<Alloc>::template rebind_traits<char>;
  using BlockLink = boost::intrusive::slist_member_hook<>;

  struct alignas(max_align_v) Block {
    BlockLink link;

    char* start() { return reinterpret_cast<char*>(this + 1); }

    Block() = default;
    ~Block() = default;
  };

  constexpr size_t blockGoodAllocSize() {
    return ArenaAllocatorTraits<Alloc>::goodSize(
        alloc(), roundUp(sizeof(Block)) + minBlockSize());
  }

  struct alignas(max_align_v) LargeBlock {
    BlockLink link;
    const size_t allocSize;

    char* start() { return reinterpret_cast<char*>(this + 1); }

    LargeBlock(size_t s) : allocSize(s) {}
    ~LargeBlock() = default;
  };

  bool canReuseExistingBlock(size_t size) {
    if (size > minBlockSize()) {
      // We don't reuse large blocks
      return false;
    }
    if (blocks_.empty() || currentBlock_ == blocks_.last()) {
      // No regular blocks to reuse
      return false;
    }
    return true;
  }

  void freeBlocks() {
    blocks_.clear_and_dispose([this](Block* b) {
      b->~Block();
      AllocTraits::deallocate(
          alloc(), reinterpret_cast<char*>(b), blockGoodAllocSize());
    });
  }

  void freeLargeBlocks() {
    largeBlocks_.clear_and_dispose([this](LargeBlock* b) {
      auto size = b->allocSize;
      totalAllocatedSize_ -= size;
      b->~LargeBlock();
      AllocTraits::deallocate(alloc(), reinterpret_cast<char*>(b), size);
    });
  }

 public:
  /// Default minimum size, in bytes, of each regular block.
  static constexpr size_t kDefaultMinBlockSize = 4096 - sizeof(Block);
  /// Sentinel value meaning the arena has no total size limit.
  static constexpr size_t kNoSizeLimit = 0;
  /// Default maximum alignment, in bytes, honored by allocations.
  static constexpr size_t kDefaultMaxAlign = alignof(Block);
  /// Per-block bookkeeping overhead, in bytes.
  static constexpr size_t kBlockOverhead = sizeof(Block);

 private:
  bool isAligned(uintptr_t address) const {
    return (address & (maxAlign_ - 1)) == 0;
  }
  bool isAligned(void* p) const {
    return isAligned(reinterpret_cast<uintptr_t>(p));
  }

  // Round up size so it's properly aligned
  size_t roundUp(size_t size) const {
    auto maxAl = maxAlign_ - 1;
    size_t realSize;
    if (!checked_add<size_t>(&realSize, size, maxAl)) {
      throw_exception<std::bad_alloc>();
    }
    return realSize & ~maxAl;
  }

  char* align(char* ptr) { return align_ceil(ptr, maxAlign_); }

  // cache_last<true> makes the list keep a pointer to the last element, so we
  // have push_back() and constant time splice_after()
  using BlockList = boost::intrusive::slist<
      Block,
      boost::intrusive::member_hook<Block, BlockLink, &Block::link>,
      boost::intrusive::constant_time_size<false>,
      boost::intrusive::cache_last<true>>;

  using LargeBlockList = boost::intrusive::slist<
      LargeBlock,
      boost::intrusive::member_hook<LargeBlock, BlockLink, &LargeBlock::link>,
      boost::intrusive::constant_time_size<false>,
      boost::intrusive::cache_last<true>>;

  void* allocateSlow(size_t size);

  // Empty member optimization: package Alloc with a non-empty member
  // in case Alloc is empty (as it is in the case of SysAllocator).
  struct AllocAndSize : public Alloc {
    explicit AllocAndSize(const Alloc& a, size_t s)
        : Alloc(a), minBlockSize(s) {}

    size_t minBlockSize;
  };

  size_t minBlockSize() const { return allocAndSize_.minBlockSize; }
  Alloc& alloc() { return allocAndSize_; }
  const Alloc& alloc() const { return allocAndSize_; }

  AllocAndSize allocAndSize_;
  BlockList blocks_;
  typename BlockList::iterator currentBlock_;
  LargeBlockList largeBlocks_;
  char* ptr_;
  char* end_;
  size_t totalAllocatedSize_;
  size_t bytesUsed_;
  const size_t sizeLimit_;
  const size_t maxAlign_;
};

template <class Alloc>
struct AllocatorHasTrivialDeallocate<Arena<Alloc>> : std::true_type {};

/**
 * By default, don't pad the given size.
 */
template <class Alloc>
struct ArenaAllocatorTraits {
  /// Returns a "good" allocation size for the allocator.
  ///
  /// \param alloc Allocator being queried (ignored by the default policy).
  /// \param size Requested size, in bytes.
  /// \returns The unchanged \p size; the default policy adds no padding.
  static size_t goodSize(const Alloc& alloc, size_t size) { return size; }
};

template <>
struct ArenaAllocatorTraits<SysAllocator<char>> {
  static size_t goodSize(const SysAllocator<char>& /* alloc */, size_t size) {
    return goodMallocSize(size);
  }
};

/**
 * Arena that uses the system allocator (malloc / free)
 */
class SysArena : public Arena<SysAllocator<char>> {
 public:
  /// Constructs a system-allocator arena.
  ///
  /// \param minBlockSize Minimum size, in bytes, of each regular block.
  /// \param sizeLimit Soft cap on the total allocated size, or kNoSizeLimit for
  ///   no limit.
  /// \param maxAlign Maximum alignment, in bytes, honored by allocations.
  explicit SysArena(
      size_t minBlockSize = kDefaultMinBlockSize,
      size_t sizeLimit = kNoSizeLimit,
      size_t maxAlign = kDefaultMaxAlign)
      : Arena<SysAllocator<char>>({}, minBlockSize, sizeLimit, maxAlign) {}
};

template <>
struct AllocatorHasTrivialDeallocate<SysArena> : std::true_type {};

/// Standard-conforming allocator that draws memory from an Arena.
template <typename T, typename Alloc>
using ArenaAllocator = CxxAllocatorAdaptor<T, Arena<Alloc>>;

/// Standard-conforming allocator backed by a SysArena.
template <typename T>
using SysArenaAllocator = ArenaAllocator<T, SysAllocator<char>>;

/// Arena allocator that falls back to std::allocator when the arena is exhausted.
template <typename T, typename Alloc>
using FallbackArenaAllocator =
    CxxAllocatorAdaptor<T, Arena<Alloc>, /* FallbackToStdAlloc */ true>;

/// SysArena allocator that falls back to std::allocator when exhausted.
template <typename T>
using FallbackSysArenaAllocator = FallbackArenaAllocator<T, SysAllocator<char>>;

} // namespace folly

#include <folly/memory/Arena-inl.h>
