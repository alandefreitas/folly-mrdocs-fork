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

#include <stdexcept>
#include <string>

#include <folly/CppAttributes.h>
#include <folly/ScopeGuard.h>
#include <folly/io/IOBuf.h>
#include <folly/lang/Exception.h>

/// Root namespace for the Folly library.
namespace folly {

/// Cursor and queue utilities for reading and writing IOBuf chains.
namespace io {
/// Selects whether a read-write cursor unshares buffers before writing.
enum class CursorAccess;
/// Read-write cursor over an IOBuf chain.
template <CursorAccess>
class RWCursor;
} // namespace io

/**
 * An IOBufQueue encapsulates a chain of IOBufs and provides
 * convenience functions to append data to the back of the chain
 * and remove data from the front.
 *
 * You may also prepend data into the headroom of the first buffer in the
 * chain, if any.
 */
class IOBufQueue {
 private:
  template <io::CursorAccess>
  friend class io::RWCursor;

  /**
   * This guard should be taken by any method that intends to do any changes
   * to in data_ (e.g. appending to it).
   *
   * It flushes the writable tail cache and refills it on destruction.
   *
   * If while the guard is held new IOBufs are appended to the chain,
   * allowTailReuse allows forking the old tail IOBuf so we can reuse its
   * writable tail.
   */
  auto updateGuard(bool allowTailReuse = false) {
    flushCache();
    folly::IOBuf* oldTail =
        (allowTailReuse && head_ != nullptr) ? head_->prev() : nullptr;
    return folly::makeGuard([this, oldTail] {
      if (oldTail != nullptr) {
        maybeReuseTail(*oldTail);
      }
      updateWritableTailCache();
    });
  }

  struct WritableRangeCacheData {
    std::pair<uint8_t*, uint8_t*> cachedRange{};
    bool attached{};

    WritableRangeCacheData() = default;

    WritableRangeCacheData(WritableRangeCacheData&& other) noexcept
        : cachedRange(other.cachedRange), attached(other.attached) {
      other.cachedRange = std::pair<uint8_t*, uint8_t*>{};
      other.attached = {};
    }
    WritableRangeCacheData& operator=(WritableRangeCacheData&& other) noexcept {
      cachedRange = std::exchange(other.cachedRange, {});
      attached = std::exchange(other.attached, {});
      return *this;
    }

    WritableRangeCacheData(const WritableRangeCacheData&) = delete;
    WritableRangeCacheData& operator=(const WritableRangeCacheData&) = delete;
  };

 public:
  /// Configuration options for an IOBufQueue.
  struct Options {
    /// Constructs the default options.
    Options() : cacheChainLength(false) {}
    bool cacheChainLength; ///< Whether to cache the total chain length.
  };

  /**
   * Get Options with cacheChainLength=true.
   * @methodset Configuration
   *
   * Commonly used Options, currently the only possible value other than
   * the default.
   *
   * @return Options with cacheChainLength set to true.
   */
  static Options cacheChainLength() {
    Options options;
    options.cacheChainLength = true;
    return options;
  }

  /**
   * WritableRangeCache represents a cache of current writable tail and provides
   * cheap and simple interface to append to it that avoids paying the cost of
   * preallocate/postallocate pair (i.e. indirections and checks).
   *
   * The cache is flushed on destruction/copy/move and on non-const accesses to
   * the underlying IOBufQueue.
   *
   * Note: there can be only one active cache for a given IOBufQueue, i.e. when
   *       you fill a cache object it automatically invalidates other
   *       cache (if any).
   */
  class WritableRangeCache {
   public:
    /// Constructs a cache, optionally attached to the given queue.
    ///
    /// \param q The queue to attach to, or nullptr for none.
    explicit WritableRangeCache(folly::IOBufQueue* q = nullptr) : queue_(q) {
      if (queue_) {
        fillCache();
      }
    }

    /**
     * Move constructor/assignment can move the cached range, but must update
     * the reference in IOBufQueue.
     *
     * @param other The cache to move from.
     */
    WritableRangeCache(WritableRangeCache&& other) noexcept
        : data_(std::move(other.data_)), queue_(other.queue_) {
      if (data_.attached) {
        queue_->updateCacheRef(data_);
      }
    }
    /**
     * Move assigns from another cache, updating the queue reference.
     *
     * @param other The cache to move from.
     * @return A reference to this cache.
     */
    WritableRangeCache& operator=(WritableRangeCache&& other) {
      if (data_.attached) {
        queue_->clearWritableRangeCache();
      }

      data_ = std::move(other.data_);
      queue_ = other.queue_;

      if (data_.attached) {
        queue_->updateCacheRef(data_);
      }

      return *this;
    }

    /**
     * Copy constructor/assignment cannot copy the cached range.
     *
     * @param other The cache to copy the queue reference from.
     */
    WritableRangeCache(const WritableRangeCache& other)
        : queue_(other.queue_) {}
    /**
     * Copy assigns the queue reference, without copying the cached range.
     *
     * @param other The cache to copy the queue reference from.
     * @return A reference to this cache.
     */
    WritableRangeCache& operator=(const WritableRangeCache& other) {
      if (data_.attached) {
        queue_->clearWritableRangeCache();
      }

      queue_ = other.queue_;

      return *this;
    }

    /// Destroys the cache, flushing it back to the queue if attached.
    ~WritableRangeCache() {
      if (data_.attached) {
        queue_->clearWritableRangeCache();
      }
    }

    /**
     * Reset the underlying IOBufQueue, will flush current cache if present.
     *
     * @param q The queue to attach to.
     */
    void reset(IOBufQueue* q) {
      if (data_.attached) {
        queue_->clearWritableRangeCache();
      }

      queue_ = q;

      if (queue_) {
        fillCache();
      }
    }

    /**
     * Get a pointer to the underlying IOBufQueue object.
     *
     * @return The attached queue, or nullptr if none.
     */
    IOBufQueue* queue() { return queue_; }

    /**
     * Return a pointer to the start of cached writable tail.
     *
     * Note: doesn't populate cache.
     *
     * @return A pointer to the start of the cached writable tail.
     */
    uint8_t* writableData() {
      dcheckIntegrity();
      return data_.cachedRange.first;
    }

    /**
     * Return a length of cached writable tail.
     *
     * Note: doesn't populate cache.
     *
     * @return The number of cached writable tail bytes.
     */
    size_t length() const {
      dcheckIntegrity();
      return data_.cachedRange.second - data_.cachedRange.first;
    }

    /**
     * Mark n bytes as occupied (e.g. postallocate).
     *
     * @param n The number of bytes to mark as occupied.
     */
    void append(size_t n) noexcept {
      dcheckIntegrity();
      // This can happen only if somebody is misusing the interface.
      // E.g. calling append after touching IOBufQueue or without checking
      // the length().
      if (FOLLY_LIKELY(data_.cachedRange.first != nullptr)) {
        DCHECK_LE(n, length());
        data_.cachedRange.first += n;
      } else {
        appendSlow(n);
      }
    }

    /**
     * Same as append(n), but avoids checking if there is a cache.
     * The caller must guarantee that the cache is set (e.g. the caller just
     * called fillCache or checked that it's not empty).
     *
     * @param n The number of bytes to mark as occupied.
     */
    void appendUnsafe(size_t n) { data_.cachedRange.first += n; }

    /**
     * Fill the cache of writable tail from the underlying IOBufQueue.
     */
    void fillCache() { queue_->fillWritableRangeCache(data_); }

   private:
    WritableRangeCacheData data_;
    IOBufQueue* queue_;

    FOLLY_NOINLINE void appendSlow(size_t n) { queue_->postallocate(n); }

    void dcheckIntegrity() const {
      // Tail start should always be less than tail end.
      DCHECK_LE(
          (void*)data_.cachedRange.first, (void*)data_.cachedRange.second);
      DCHECK(
          data_.cachedRange.first != nullptr ||
          data_.cachedRange.second == nullptr);

      // Cached range should be always empty if the cache is not attached.
      DCHECK(
          data_.attached ||
          (data_.cachedRange.first == nullptr &&
           data_.cachedRange.second == nullptr));

      // We cannot be in attached state if the queue_ is not set.
      DCHECK(queue_ != nullptr || !data_.attached);

      // If we're attached and the cache is not empty, then it should coincide
      // with the tail buffer.
      DCHECK(
          !data_.attached || data_.cachedRange.first == nullptr ||
          (queue_->head_ != nullptr &&
           data_.cachedRange.first >= queue_->head_->prev()->writableTail() &&
           data_.cachedRange.second ==
               queue_->head_->prev()->writableTail() +
                   queue_->head_->prev()->tailroom()));
    }
  };

  /// Construct an empty queue.
  ///
  /// \param options Configuration options for the queue.
  explicit IOBufQueue(const Options& options = Options());
  /// Destructor.
  ~IOBufQueue();

  /**
   * @brief Get writeable headroom
   * @methodset Access
   *
   * @return A pair of buffer-address and writeable-length
   */
  std::pair<void*, std::size_t> headroom();

  /**
   * @brief Indicate that n bytes from the headroom have been used.
   * @methodset Shifting
   *
   * @note Prepending happens at the end of the available headroom. If this
   * IOBufQueue's headroom() returns `{buf, len}`, then prepend(n) will
   * cause a subsequent call to headroom() to return `{buf, len-n}`.
   *
   * @param n The number of headroom bytes that were used.
   */
  void markPrepended(std::size_t n);

  /**
   * @brief Prepend an existing range
   * @methodset Modifiers
   *
   * Throws std::overflow_error if not enough room.
   *
   * @param buf The bytes to prepend.
   * @param n The number of bytes to prepend.
   */
  void prepend(const void* buf, std::size_t n);

  /**
   * @overloadbrief Append data.
   * @methodset Modifiers
   *
   * Add a buffer or buffer chain to the end of this queue. The
   * queue takes ownership of buf.
   *
   * If pack is true, we try to reduce wastage at the end of this queue
   * by copying some data from the first buffers in the buf chain (and
   * releasing the buffers), if possible.  If pack is false, we leave
   * the chain topology unchanged.
   *
   * If allowTailReuse is true, the current tail is split and reappended at the
   * end of the chain when possible and beneficial. This can reduce memory
   * overhead when append() is interleaved with other writes: without this
   * option, appending a new buffer strands the previous tail, even if there is
   * room left. It is preferable to enable this only if the whole chain is
   * likely to share the lifetime, since the split tail buffers hold the entire
   * allocation of the buffer they were split from.
   *
   * @param buf The buffer or buffer chain to append.
   * @param pack Whether to pack data into the tail to reduce wastage.
   * @param allowTailReuse Whether to split and reappend the current tail.
   */
  void append(
      std::unique_ptr<folly::IOBuf>&& buf,
      bool pack = false,
      bool allowTailReuse = false);
  /// Append a copy of a buffer or buffer chain to the end of this queue.
  ///
  /// @methodset Modifiers
  ///
  /// \param buf The buffer or buffer chain to copy and append.
  /// \param pack Whether to pack data into the tail to reduce wastage.
  /// \param allowTailReuse Whether to split and reappend the current tail.
  void append(
      const folly::IOBuf& buf, bool pack = false, bool allowTailReuse = false);
  /// Append a buffer or buffer chain to the end of this queue, taking ownership.
  ///
  /// @methodset Modifiers
  ///
  /// \param buf The buffer or buffer chain to append.
  /// \param pack Whether to pack data into the tail to reduce wastage.
  /// \param allowTailReuse Whether to split and reappend the current tail.
  void append(
      folly::IOBuf&& buf, bool pack = false, bool allowTailReuse = false);

  /**
   * Add a queue to the end of this queue. `this` takes ownership of
   * all buffers from the other queue.
   *
   * @param other The queue whose buffers are moved to the end of this queue.
   * @param pack Whether to pack data into the tail to reduce wastage.
   * @param allowTailReuse Whether to split and reappend the current tail.
   */
  void append(
      IOBufQueue& other, bool pack = false, bool allowTailReuse = false);
  /// Add a queue to the end of this queue, taking ownership of its buffers.
  ///
  /// @methodset Modifiers
  ///
  /// \param other The queue whose buffers are moved to the end of this queue.
  /// \param pack Whether to pack data into the tail to reduce wastage.
  /// \param allowTailReuse Whether to split and reappend the current tail.
  void append(
      IOBufQueue&& other, bool pack = false, bool allowTailReuse = false) {
    append(other, pack, allowTailReuse);
  }

  /**
   * Copy len bytes, starting at buf, to the end of this queue.
   * The caller retains ownership of the source data.
   *
   * @param buf The bytes to copy.
   * @param len The number of bytes to copy.
   */
  void append(const void* buf, size_t len);

  /**
   * Copy a string to the end of this queue.
   * The caller retains ownership of the source data.
   *
   * @param sp The string to copy.
   */
  void append(StringPiece sp) { append(sp.data(), sp.size()); }

  /**
   * @brief Append a buffer by wrapping.
   * @methodset Modifiers
   *
   * Append a chain of IOBuf objects that point to consecutive regions
   * within buf.
   *
   * Just like IOBuf::wrapBuffer, this should only be used when the caller
   * knows ahead of time and can ensure that all IOBuf objects that will point
   * to this buffer will be destroyed before the buffer itself is destroyed;
   * all other caveats from wrapBuffer also apply.
   *
   * Every buffer except for the last will wrap exactly blockSize bytes.
   * Importantly, this method may be used to wrap buffers larger than 4GB.
   *
   * @param buf The buffer to wrap.
   * @param len The number of bytes to wrap.
   * @param blockSize The number of bytes each wrapping IOBuf covers.
   */
  void wrapBuffer(
      const void* buf,
      size_t len,
      std::size_t blockSize = (1U << 31)); // default block size: 2GB

  /**
   * @brief Obtain a writable block of contiguous bytes at the end of this
   * queue, allocating more space if necessary.
   * @methodset Allocation
   *
   * The amount of space reserved will be at least min.  If min contiguous space
   * is not available at the end of the queue, and IOBuf with size
   * newAllocationSize is appended to the chain and returned.  The actual
   * available space may be larger than newAllocationSize, but will be truncated
   * to max, if specified.
   *
   * If the caller subsequently writes anything into the returned space,
   * it must call the postallocate() method.
   *
   * @return The starting address of the block and the length in bytes.
   *
   * @param min The minimum amount of contiguous space to reserve.
   * @param newAllocationSize The size of a new buffer if one is allocated.
   * @param max The maximum amount of space to return.
   *
   * @note The point of the preallocate()/postallocate() mechanism is
   *       to support I/O APIs such as AsyncSocket::ReadCallback that
   *       request a buffer from the application and then, in a later
   *       callback, tell the application how much of the buffer they
   *       have filled with data.
   */
  std::pair<void*, std::size_t> preallocate(
      std::size_t min,
      std::size_t newAllocationSize,
      std::size_t max = std::numeric_limits<std::size_t>::max()) {
    dcheckCacheIntegrity();

    if (FOLLY_LIKELY(writableTail() != nullptr && tailroom() >= min)) {
      return std::make_pair(
          writableTail(), std::min<std::size_t>(max, tailroom()));
    }

    return preallocateSlow(min, newAllocationSize, max);
  }

  /**
   * @brief Tell the queue that the caller has written data into the first n
   * bytes provided by the previous preallocate() call.
   * @methodset Allocation
   *
   * @note n should be less than or equal to the size returned by
   *       preallocate().  If n is zero, the caller may skip the call
   *       to postallocate().  If n is nonzero, the caller must not
   *       invoke any other non-const methods on this IOBufQueue between
   *       the call to preallocate and the call to postallocate().
   *
   * @param n The number of preallocated bytes that were written.
   */
  void postallocate(std::size_t n) noexcept {
    dcheckCacheIntegrity();
    DCHECK_LE(
        (void*)(cachePtr_->cachedRange.first + n),
        (void*)cachePtr_->cachedRange.second);
    cachePtr_->cachedRange.first += n;
  }

  /**
   * @brief Obtain a writable block of n contiguous bytes, allocating more space
   * if necessary, and mark it as used.
   * @methodset Allocation
   *
   * The space is obtained at the end of the queue. The caller can fill it
   * later.
   *
   * @param n The number of contiguous bytes to obtain.
   * @return A pointer to the allocated block.
   */
  void* allocate(std::size_t n) {
    void* p = preallocate(n, n).first;
    postallocate(n);
    return p;
  }

  /**
   * @brief Get a pointer to the writable tail section.
   * @methodset Access
   *
   * @return A pointer to the writable tail section.
   */
  void* writableTail() {
    dcheckCacheIntegrity();
    return cachePtr_->cachedRange.first;
  }

  /**
   * @brief Get the amount of free space at the end of the buffer.
   * @methodset Access
   *
   * @return The number of free bytes at the end of the buffer.
   */
  size_t tailroom() const {
    dcheckCacheIntegrity();
    return cachePtr_->cachedRange.second - cachePtr_->cachedRange.first;
  }

  /**
   * @brief Split off the first n bytes of the queue into a separate IOBuf
   * chain.
   * @methodset Modifiers
   *
   * Transfer ownership of the new chain to the caller.  The IOBufQueue
   * retains ownership of everything after the split point.
   *
   * @warning If the split point lies in the middle of some IOBuf within
   *          the chain, this function may, as an implementation detail,
   *          clone that IOBuf.
   *
   * @throws std::underflow_error if n exceeds the number of bytes
   *         in the queue.
   *
   * @param n The number of bytes to split off the front.
   * @return The split-off chain of the first n bytes.
   */
  std::unique_ptr<folly::IOBuf> split(size_t n) { return split(n, true); }

  /**
   * @brief Split at most n bytes.
   * @methodset Modifiers
   *
   * Similar to split, but will return the entire queue instead of throwing
   * if n exceeds the number of bytes in the queue.
   *
   * @param pack  When true, small IOBuf nodes are packed into the
   *         preceding node's spare tail capacity via memcpy (up to
   *         kMaxPackCopy bytes per node), reducing chain link count.
   *         Empty nodes are dropped. Default is false.
   * @param n The maximum number of bytes to split off the front.
   * @return The split-off chain of up to n bytes.
   */
  std::unique_ptr<folly::IOBuf> splitAtMost(size_t n, bool pack = false) {
    return split(n, false, pack);
  }

  /**
   * @brief Remove bytes from the front.
   * @methodset Shifting
   *
   * Similar to IOBuf::trimStart, but works on the whole queue.  Will
   * pop off buffers that have been completely trimmed.
   *
   * @param amount The number of bytes to remove from the front.
   */
  void trimStart(size_t amount);

  /**
   * @brief Maybe remove bytes from the front.
   * @methodset Shifting
   *
   * Similar to trimStart, but will trim at most amount bytes and returns
   * the number of bytes trimmed.
   *
   * @param amount The maximum number of bytes to remove from the front.
   * @return The number of bytes actually removed.
   */
  size_t trimStartAtMost(size_t amount);

  /**
   * @brief Remove bytes from the end.
   * @methodset Shifting
   *
   * Similar to IOBuf::trimEnd, but works on the whole queue.  Will
   * pop off buffers that have been completely trimmed.
   *
   * @param amount The number of bytes to remove from the end.
   */
  void trimEnd(size_t amount);

  /**
   * @brief Maybe remove bytes from the end.
   * @methodset Shifting
   *
   * Similar to trimEnd, but will trim at most amount bytes and returns
   * the number of bytes trimmed.
   *
   * @param amount The maximum number of bytes to remove from the end.
   * @return The number of bytes actually removed.
   */
  size_t trimEndAtMost(size_t amount);

  /**
   * @brief Transfer ownership of the queue's entire IOBuf chain to the caller.
   * @methodset Conversions
   *
   * @return The queue's entire IOBuf chain.
   */
  std::unique_ptr<folly::IOBuf> move() {
    auto guard = updateGuard();
    std::unique_ptr<folly::IOBuf> res = std::move(head_);
    chainLength_ = 0;
    return res;
  }

  /// Transfer the queue's entire IOBuf chain to the caller, by value.
  ///
  /// @methodset Conversions
  ///
  /// \returns The queue's entire IOBuf chain, as a value.
  folly::IOBuf moveAsValue() { return std::move(*move()); }

  /**
   * @brief Access the front IOBuf.
   * @methodset Access
   *
   * Note: caller will see the current state of the chain, but may not see
   *       future updates immediately, due to the presence of a tail cache.
   * Note: the caller may potentially clone the chain, thus marking all buffers
   *       as shared. We may still continue writing to the tail of the last
   *       IOBuf without checking if it's shared, but this is fine, since the
   *       cloned IOBufs won't reference that data.
   *
   * @return The first IOBuf in the chain, or nullptr if the queue is empty.
   */
  const folly::IOBuf* front() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    flushCache();
    return head_.get();
  }

  /**
   * @brief Removes and returns the first IOBuf in the chain.
   * @methodset Conversions
   *
   * @return first IOBuf in the chain or nullptr if none.
   */
  std::unique_ptr<folly::IOBuf> pop_front();

  /**
   * @brief Get cached chain length.
   * @methodset Capacity
   *
   * Total chain length, only valid if cacheLength was specified in the
   * constructor.
   *
   * @return The total number of bytes in the chain.
   */
  size_t chainLength() const {
    if (FOLLY_UNLIKELY(!options_.cacheChainLength)) {
      folly::throw_exception<std::invalid_argument>(
          "IOBufQueue: chain length not cached");
    }
    dcheckCacheIntegrity();
    return chainLength_ + (cachePtr_->cachedRange.first - tailStart_);
  }

  /**
   * @brief Returns true iff the IOBuf chain length is 0.
   * @methodset Capacity
   *
   * @return True if the chain contains no bytes.
   */
  bool empty() const {
    dcheckCacheIntegrity();
    return !head_ ||
        (head_->empty() && cachePtr_->cachedRange.first == tailStart_);
  }

  /**
   * @brief Get the options used to configure this IOBufQueue.
   * @methodset Configuration
   *
   * @return The options used to configure this queue.
   */
  const Options& options() const { return options_; }

  /**
   * @brief Clear the queue, freeing all the buffers.
   * @methodset Allocation
   *
   * Options are preserved.
   */
  void reset() { move(); }

  /**
   * @brief Clear the queue, but try to clear and keep the largest buffer for
   * reuse when possible.
   * @methodset Allocation
   *
   * Options are preserved.
   */
  void clearAndTryReuseLargestBuffer();

  /**
   * @brief Append the queue to a std::string.
   * @methodset Utility
   *
   * Non-destructive.
   *
   * @param out The string the queue contents are appended to.
   */
  void appendToString(std::string& out) const;

  /**
   * @brief Calls IOBuf::gather() on the head of the queue, if it exists.
   * @methodset Capacity
   *
   * @param maxLength The maximum number of bytes to gather contiguously.
   */
  void gather(std::size_t maxLength);

  /// Move constructor.
  ///
  /// \param other The queue to move from.
  IOBufQueue(IOBufQueue&& other) noexcept;
  /// Move assignment operator.
  ///
  /// \param other The queue to move from.
  /// \returns A reference to this queue.
  IOBufQueue& operator=(IOBufQueue&& other) noexcept;

  /**
   * @brief Set a custom IOBufFactory for buffer allocations.
   * @methodset Configuration
   *
   * When set, all internal buffer allocations will use this factory
   * instead of IOBuf::create. The caller is responsible for ensuring
   * the factory outlives the IOBufQueue.
   *
   * @param factory The factory used for internal buffer allocations.
   */
  void setIOBufFactory(IOBufFactory* factory) { factory_ = factory; }

  /// The maximum number of bytes copied per node when packing a chain.
  static constexpr size_t kMaxPackCopy = 4096;

 private:
  std::unique_ptr<folly::IOBuf> createBuf(std::size_t capacity) const {
    return factory_ ? (*factory_)(capacity) : folly::IOBuf::create(capacity);
  }

  std::unique_ptr<folly::IOBuf> split(
      size_t n, bool throwOnUnderflow, bool pack = false);

  static const size_t kChainLengthNotCached = (size_t)-1;
  /** Not copyable */
  IOBufQueue(const IOBufQueue&) = delete;
  IOBufQueue& operator=(const IOBufQueue&) = delete;

  Options options_;

  /**
   * Everything that has been appended but not yet discarded or moved out
   * Note: anything that needs to operate on a tail should either call
   * flushCache() or grab updateGuard() (it will flush the cache itself).
   */
  std::unique_ptr<folly::IOBuf> head_;

  IOBufFactory* factory_{nullptr};

  mutable uint8_t* tailStart_{nullptr};
  WritableRangeCacheData* cachePtr_{nullptr};
  WritableRangeCacheData localCache_;

  // NOTE that chainLength_ is still updated even if !options_.cacheChainLength
  // because doing it unchecked in postallocate() is faster (no (mis)predicted
  // branch)
  mutable size_t chainLength_{0};

  void dcheckCacheIntegrity() const noexcept {
    // Tail start should always be less than tail end.
    DCHECK_LE((void*)tailStart_, (void*)cachePtr_->cachedRange.first);
    DCHECK_LE(
        (void*)cachePtr_->cachedRange.first,
        (void*)cachePtr_->cachedRange.second);
    DCHECK(
        cachePtr_->cachedRange.first != nullptr ||
        cachePtr_->cachedRange.second == nullptr);

    // There is always an attached cache instance.
    DCHECK(cachePtr_->attached);

    // Either cache is empty or it coincides with the tail.
    if (cachePtr_->cachedRange.first != nullptr) {
      DCHECK(head_ != nullptr);
      DCHECK(tailStart_ == head_->prev()->writableTail());
      DCHECK(tailStart_ <= cachePtr_->cachedRange.first);
      DCHECK(cachePtr_->cachedRange.first >= head_->prev()->writableTail());
      DCHECK(
          cachePtr_->cachedRange.second ==
          head_->prev()->writableTail() + head_->prev()->tailroom());
    }
  }

  /**
   * Populate dest with writable tail range cache.
   */
  void fillWritableRangeCache(WritableRangeCacheData& dest) noexcept {
    dcheckCacheIntegrity();
    if (cachePtr_ != &dest) {
      dest = std::move(*cachePtr_);
      cachePtr_ = &dest;
    }
  }

  /**
   * Clear current writable tail cache and reset it to localCache_
   */
  void clearWritableRangeCache() noexcept {
    flushCache();

    if (cachePtr_ != &localCache_) {
      localCache_ = std::move(*cachePtr_);
      cachePtr_ = &localCache_;
    }

    DCHECK(cachePtr_ == &localCache_ && localCache_.attached);
  }

  /**
   * Commit any pending changes to the tail of the queue.
   */
  void flushCache() const noexcept {
    dcheckCacheIntegrity();

    if (tailStart_ != cachePtr_->cachedRange.first) {
      auto buf = head_->prev();
      DCHECK_EQ(
          (void*)(buf->writableTail() + buf->tailroom()),
          (void*)cachePtr_->cachedRange.second);
      auto len = cachePtr_->cachedRange.first - tailStart_;
      buf->append(len);
      chainLength_ += len;
      tailStart_ += len;
    }
  }

  // For WritableRangeCache move assignment/construction.
  void updateCacheRef(WritableRangeCacheData& newRef) noexcept {
    cachePtr_ = &newRef;
  }

  /**
   * Update cached writable tail range. Called by updateGuard()
   */
  void updateWritableTailCache() noexcept {
    if (FOLLY_LIKELY(head_ != nullptr)) {
      IOBuf* buf = head_->prev();
      if (FOLLY_LIKELY(!buf->isSharedOne())) {
        tailStart_ = buf->writableTail();
        cachePtr_->cachedRange = std::pair<uint8_t*, uint8_t*>(
            tailStart_, tailStart_ + buf->tailroom());
        return;
      }
    }
    tailStart_ = nullptr;
    cachePtr_->cachedRange = std::pair<uint8_t*, uint8_t*>();
  }

  std::pair<void*, std::size_t> preallocateSlow(
      std::size_t min, std::size_t newAllocationSize, std::size_t max);

  void maybeReuseTail(folly::IOBuf& oldTail);
};

} // namespace folly
