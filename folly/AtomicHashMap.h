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

/*
 * AtomicHashMap --
 *
 * A high-performance concurrent hash map with int32_t or int64_t keys. Supports
 * insert, find(key), findAt(index), erase(key), size, and more.  Memory cannot
 * be freed or reclaimed by erase.  Can grow to a maximum of about 18 times the
 * initial capacity, but performance degrades linearly with growth. Can also be
 * used as an object store with unique 32-bit references directly into the
 * internal storage (retrieved with iterator::getIndex()).
 *
 * Advantages:
 *    - High-performance (~2-4x tbb::concurrent_hash_map in heavily
 *      multi-threaded environments).
 *    - Efficient memory usage if initial capacity is not over estimated
 *      (especially for small keys and values).
 *    - Good fragmentation properties (only allocates in large slabs which can
 *      be reused with clear() and never move).
 *    - Can generate unique, long-lived 32-bit references for efficient lookup
 *      (see findAt()).
 *
 * Disadvantages:
 *    - Keys must be native int32_t or int64_t, or explicitly converted.
 *    - Must be able to specify unique empty, locked, and erased keys
 *    - Performance degrades linearly as size grows beyond initialization
 *      capacity.
 *    - Max size limit of ~18x initial size (dependent on max load factor).
 *    - Memory is not freed or reclaimed by erase.
 *
 * Usage and Operation Details:
 *   Simple performance/memory tradeoff with maxLoadFactor.  Higher load factors
 *   give better memory utilization but probe lengths increase, reducing
 *   performance.
 *
 * Implementation and Performance Details:
 *   AHArray is a fixed size contiguous block of value_type cells.  When
 *   writing a cell, the key is locked while the rest of the record is
 *   written.  Once done, the cell is unlocked by setting the key.  find()
 *   is completely wait-free and doesn't require any non-relaxed atomic
 *   operations.  AHA cannot grow beyond initialization capacity, but is
 *   faster because of reduced data indirection.
 *
 *   AHMap is a wrapper around AHArray sub-maps that allows growth and provides
 *   an interface closer to the STL UnorderedAssociativeContainer concept. These
 *   sub-maps are allocated on the fly and are processed in series, so the more
 *   there are (from growing past initial capacity), the worse the performance.
 *
 *   Insert returns false if there is a key collision and throws if the max size
 *   of the map is exceeded.
 *
 *   Benchmark performance with 8 simultaneous threads processing 1 million
 *   unique <int64_t, int64_t> entries on a 4-core, 2.5 GHz machine:
 *
 *     Load Factor   Mem Efficiency   usec/Insert   usec/Find
 *         50%             50%           0.19         0.05
 *         85%             85%           0.20         0.06
 *         90%             90%           0.23         0.08
 *         95%             95%           0.27         0.10
 *
 *   See folly/tests/AtomicHashMapTest.cpp for more benchmarks.
 */

#pragma once
#define FOLLY_ATOMICHASHMAP_H_

#include <atomic>
#include <functional>
#include <stdexcept>

#include <folly/AtomicHashArray.h>
#include <folly/CPortability.h>
#include <folly/Likely.h>
#include <folly/ThreadCachedInt.h>
#include <folly/container/Foreach.h>
#include <folly/hash/Hash.h>

namespace folly {

/*
 * AtomicHashMap provides an interface somewhat similar to the
 * UnorderedAssociativeContainer concept in C++.  This does not
 * exactly match this concept (or even the basic Container concept),
 * because of some restrictions imposed by our datastructure.
 *
 * Specific differences (there are quite a few):
 *
 * - Efficiently thread safe for inserts (main point of this stuff),
 *   wait-free for lookups.
 *
 * - You can erase from this container, but the cell containing the key will
 *   not be free or reclaimed.
 *
 * - You can erase everything by calling clear() (and you must guarantee only
 *   one thread can be using the container to do that).
 *
 * - We aren't DefaultConstructible, CopyConstructible, Assignable, or
 *   EqualityComparable.  (Most of these are probably not something
 *   you actually want to do with this anyway.)
 *
 * - We don't support the various bucket functions, rehash(),
 *   reserve(), or equal_range().  Also no constructors taking
 *   iterators, although this could change.
 *
 * - Several insertion functions, notably operator[], are not
 *   implemented.  It is a little too easy to misuse these functions
 *   with this container, where part of the point is that when an
 *   insertion happens for a new key, it will atomically have the
 *   desired value.
 *
 * - The map has no templated insert() taking an iterator range, but
 *   we do provide an insert(key, value).  The latter seems more
 *   frequently useful for this container (to avoid sprinkling
 *   make_pair everywhere), and providing both can lead to some gross
 *   template error messages.
 *
 * - The Allocator must not be stateful (a new instance will be spun up for
 *   each allocation), and its allocate() method must take a raw number of
 *   bytes.
 *
 * - KeyT must be a 32 bit or 64 bit atomic integer type, and you must
 *   define special 'locked' and 'empty' key values in the ctor
 *
 * - We don't take the Hash function object as an instance in the
 *   constructor.
 *
 */

/// Thrown when insertion fails due to running out of space for submaps.
struct FOLLY_EXPORT AtomicHashMapFullError : std::runtime_error {
  /// Construct the error with a fixed "AtomicHashMap is full" message.
  explicit AtomicHashMapFullError()
      : std::runtime_error("AtomicHashMap is full") {}
};

/// A high-performance concurrent hash map with integer keys.
///
/// Provides an interface similar to the STL UnorderedAssociativeContainer
/// concept, with inserts that are thread safe and lookups that are wait-free.
/// See the file-level comment for the container's restrictions and tradeoffs.
///
/// \tparam KeyT The key type; must be a 32-bit or 64-bit atomic integer type.
/// \tparam ValueT The mapped value type.
/// \tparam HashFcn The hash function object type.
/// \tparam EqualFcn The key equality comparison type.
/// \tparam Allocator The stateless allocator type.
/// \tparam ProbeFcn The probing strategy type.
/// \tparam KeyConvertFcn The functor converting a lookup key to a stored key.
template <
    class KeyT,
    class ValueT,
    class HashFcn,
    class EqualFcn,
    class Allocator,
    class ProbeFcn,
    class KeyConvertFcn>
class AtomicHashMap {
  using SubMap = AtomicHashArray<
      KeyT,
      ValueT,
      HashFcn,
      EqualFcn,
      Allocator,
      ProbeFcn,
      KeyConvertFcn>;

 public:
  /// The key type stored in the map.
  using key_type = KeyT;
  /// The mapped value type stored in the map.
  using mapped_type = ValueT;
  /// The element type, a key/value pair.
  using value_type = std::pair<const KeyT, ValueT>;
  /// The hash function object type.
  using hasher = HashFcn;
  /// The key equality comparison type.
  using key_equal = EqualFcn;
  /// The functor type converting a lookup key to a stored key.
  using key_convert = KeyConvertFcn;
  /// Pointer to an element.
  using pointer = value_type*;
  /// Reference to an element.
  using reference = value_type&;
  /// Reference to a const element.
  using const_reference = const value_type&;
  /// Signed integer type for iterator differences.
  using difference_type = std::ptrdiff_t;
  /// Unsigned integer type for sizes and counts.
  using size_type = std::size_t;
  /// Configuration options forwarded to the underlying submaps.
  using Config = typename SubMap::Config;

  /// Iterator template shared by the mutable and const iterators.
  ///
  /// \tparam ContT The (possibly const) container type being iterated.
  /// \tparam IterVal The (possibly const) element type yielded.
  /// \tparam SubIt The underlying submap iterator type.
  template <class ContT, class IterVal, class SubIt>
  struct ahm_iterator;

  /// Read-only iterator over the elements of the map.
  using const_iterator = ahm_iterator<
      const AtomicHashMap,
      const value_type,
      typename SubMap::const_iterator>;
  /// Mutable iterator over the elements of the map.
  using iterator =
      ahm_iterator<AtomicHashMap, value_type, typename SubMap::iterator>;

 public:
  /// How much to grow when we run out of capacity.
  const float kGrowthFrac_;

  /// Construct a map sized for a given estimated final element count.
  ///
  /// \param finalSizeEst The optimal number of elements to maximize space
  ///     utilization and performance.
  /// \param c A `Config` object specifying more advanced options.
  explicit AtomicHashMap(size_t finalSizeEst, const Config& c = Config());

  /// Deleted copy constructor; the map is not copyable.
  ///
  /// \param other The map that would be copied from.
  AtomicHashMap(const AtomicHashMap& other) = delete;

  /// Deleted copy assignment; the map is not assignable.
  ///
  /// \param other The map that would be assigned from.
  /// \returns A reference to this map.
  AtomicHashMap& operator=(const AtomicHashMap& other) = delete;

  /// Destroy the map and all of its allocated submaps.
  ~AtomicHashMap() {
    const unsigned int numMaps =
        numMapsAllocated_.load(std::memory_order_relaxed);
    FOR_EACH_RANGE (i, 0, numMaps) {
      SubMap* thisMap = subMaps_[i].load(std::memory_order_relaxed);
      DCHECK(thisMap);
      SubMap::destroy(thisMap);
    }
  }

  /// Return the key equality comparison object.
  ///
  /// \returns A default-constructed `key_equal`.
  key_equal key_eq() const { return key_equal(); }

  /// Return the hash function object.
  ///
  /// \returns A default-constructed `hasher`.
  hasher hash_function() const { return hasher(); }

  /**
   * Insert an element by copying a key/value pair.
   *
   *   Returns a pair with iterator to the element at r.first and
   *   success.  Retrieve the index with ret.first.getIndex().
   *
   *   Does not overwrite on key collision, but returns an iterator to
   *   the existing element (since this could due to a race with
   *   another thread, it is often important to check this return
   *   value).
   *
   *   Allocates new sub maps as the existing ones become full.  If
   *   all sub maps are full, no element is inserted, and
   *   `AtomicHashMapFullError` is thrown.
   *
   * \param r The key/value pair to insert.
   * \returns A pair of an iterator to the element and a bool that is `true`
   *     when a new element was inserted.
   */
  std::pair<iterator, bool> insert(const value_type& r) {
    return emplace(r.first, r.second);
  }

  /// Insert an element by copying a key and value.
  ///
  /// \param k The key to insert.
  /// \param v The value to associate with the key.
  /// \returns A pair of an iterator to the element and a bool that is `true`
  ///     when a new element was inserted.
  std::pair<iterator, bool> insert(key_type k, const mapped_type& v) {
    return emplace(k, v);
  }

  /// Insert an element by moving a key/value pair.
  ///
  /// \param r The key/value pair to insert.
  /// \returns A pair of an iterator to the element and a bool that is `true`
  ///     when a new element was inserted.
  std::pair<iterator, bool> insert(value_type&& r) {
    return emplace(r.first, std::move(r.second));
  }

  /// Insert an element by moving the value.
  ///
  /// \param k The key to insert.
  /// \param v The value to move into the map.
  /// \returns A pair of an iterator to the element and a bool that is `true`
  ///     when a new element was inserted.
  std::pair<iterator, bool> insert(key_type k, mapped_type&& v) {
    return emplace(k, std::move(v));
  }

  /**
   * Insert an element, constructing the value in place.
   *
   *   Same contract as insert(), but performs in-place construction
   *   of the value type using the specified arguments.
   *
   *   Also, like find(), this method optionally allows 'k' to have a type
   *   different from that stored in the table; see find(). If and only if no
   *   equal key is already present, this method converts 'k' to a key of
   *   type KeyT using the provided LookupKeyToKeyFcn.
   *
   * \tparam LookupKeyT The type of the lookup key.
   * \tparam LookupHashFcn The hash functor for the lookup key.
   * \tparam LookupEqualFcn The equality functor for the lookup key.
   * \tparam LookupKeyToKeyFcn The functor converting the lookup key to a
   *     stored key.
   * \tparam ArgTs The value constructor argument types.
   * \param k The key to insert.
   * \param vCtorArg The arguments used to construct the value in place.
   * \returns A pair of an iterator to the element and a bool that is `true`
   *     when a new element was inserted.
   */
  template <
      typename LookupKeyT = key_type,
      typename LookupHashFcn = hasher,
      typename LookupEqualFcn = key_equal,
      typename LookupKeyToKeyFcn = key_convert,
      typename... ArgTs>
  std::pair<iterator, bool> emplace(LookupKeyT k, ArgTs&&... vCtorArg);

  /**
   * Find the element with the given key.
   *
   *   As an optional feature, the type of the key to look up (LookupKeyT) is
   *   allowed to be different from the type of keys actually stored (KeyT).
   *
   *   This enables use cases where materializing the key is costly and usually
   *   redundant, e.g., canonicalizing/interning a set of strings and being able
   *   to look up by StringPiece. To use this feature, LookupHashFcn must take
   *   a LookupKeyT, and LookupEqualFcn must take KeyT and LookupKeyT as first
   *   and second parameter, respectively.
   *
   *   See folly/test/ArrayHashMapTest.cpp for sample usage.
   *
   * \tparam LookupKeyT The type of the lookup key.
   * \tparam LookupHashFcn The hash functor for the lookup key.
   * \tparam LookupEqualFcn The equality functor for the lookup key.
   * \param k The key to look up.
   * \returns An iterator to the element if found, otherwise `end()`.
   */
  template <
      typename LookupKeyT = key_type,
      typename LookupHashFcn = hasher,
      typename LookupEqualFcn = key_equal>
  iterator find(LookupKeyT k);

  /// Find the element with the given key.
  ///
  /// \tparam LookupKeyT The type of the lookup key.
  /// \tparam LookupHashFcn The hash functor for the lookup key.
  /// \tparam LookupEqualFcn The equality functor for the lookup key.
  /// \param k The key to look up.
  /// \returns A const iterator to the element if found, otherwise `end()`.
  template <
      typename LookupKeyT = key_type,
      typename LookupHashFcn = hasher,
      typename LookupEqualFcn = key_equal>
  const_iterator find(LookupKeyT k) const;

  /**
   * Erase a key from the map.
   *
   * \param k The key to erase.
   * \returns 1 if the key was found and erased, and 0 otherwise.
   */
  size_type erase(key_type k);

  /**
   * Wipe all keys and values from the map.
   *
   *   Wipes all keys and values from primary map and destroys all secondary
   *   maps.  Primary map remains allocated and thus the memory can be reused
   *   in place.  Not thread safe.
   */
  void clear();

  /**
   * Return the exact number of elements in the map.
   *
   *  Note this is not as cheap as typical
   *  size() implementations because, for each AtomicHashArray in this AHM, we
   *  need to grab a lock and accumulate the values from all the thread local
   *  counters.  See folly/ThreadCachedInt.h for more details.
   *
   * \returns The number of elements currently stored.
   */
  size_t size() const;

  /// Report whether the map is empty.
  ///
  /// \returns `true` if the map contains no elements.
  bool empty() const { return size() == 0; }

  /// Count the elements with a given key.
  ///
  /// \param k The key to count.
  /// \returns 1 if the key is present, and 0 otherwise.
  size_type count(key_type k) const { return find(k) == end() ? 0 : 1; }

  /**
   * Return an iterator to the element at a stored index.
   *
   *   idx should only be an unmodified value returned by calling getIndex() on
   *   a valid iterator returned by find() or insert(). If idx is invalid you
   *   have a bug and the process aborts.
   *
   * \param idx An index previously obtained from `iterator::getIndex()`.
   * \returns An iterator to the element at that index.
   */
  iterator findAt(uint32_t idx) {
    SimpleRetT ret = findAtInternal(idx);
    DCHECK_LT(ret.i, numSubMaps());
    return iterator(
        this,
        ret.i,
        subMaps_[ret.i].load(std::memory_order_relaxed)->makeIter(ret.j));
  }

  /// Return a const iterator to the element at a stored index.
  ///
  /// \param idx An index previously obtained from `iterator::getIndex()`.
  /// \returns A const iterator to the element at that index.
  const_iterator findAt(uint32_t idx) const {
    return const_cast<AtomicHashMap*>(this)->findAt(idx);
  }

  /// Return the total capacity, the sum of the capacities of all submaps.
  ///
  /// \returns The total number of elements the map can hold without growing.
  size_t capacity() const;

  /// Return the number of insertions before all submaps reach max load factor.
  ///
  /// \returns The number of new insertions still available at current capacity.
  size_t spaceRemaining() const;

  /// Set the thread-cache size of the entry counter for every submap.
  ///
  /// \param newSize The new per-thread cache size for entry counting.
  void setEntryCountThreadCacheSize(int32_t newSize) {
    const int numMaps = numMapsAllocated_.load(std::memory_order_acquire);
    for (int i = 0; i < numMaps; ++i) {
      SubMap* map = subMaps_[i].load(std::memory_order_relaxed);
      map->setEntryCountThreadCacheSize(newSize);
    }
  }

  /// Return the number of submaps allocated so far to implement this map.
  ///
  /// The more there are, the worse the performance.
  ///
  /// \returns The count of currently allocated submaps.
  int numSubMaps() const {
    return numMapsAllocated_.load(std::memory_order_acquire);
  }

  /// Return an iterator to the first element.
  ///
  /// \returns An iterator to the beginning of the map.
  iterator begin() {
    iterator it(this, 0, subMaps_[0].load(std::memory_order_relaxed)->begin());
    it.checkAdvanceToNextSubmap();
    return it;
  }

  /// Return a const iterator to the first element.
  ///
  /// \returns A const iterator to the beginning of the map.
  const_iterator begin() const {
    const_iterator it(
        this, 0, subMaps_[0].load(std::memory_order_relaxed)->begin());
    it.checkAdvanceToNextSubmap();
    return it;
  }

  /// Return an iterator past the last element.
  ///
  /// \returns An iterator to the end of the map.
  iterator end() { return iterator(); }

  /// Return a const iterator past the last element.
  ///
  /// \returns A const iterator to the end of the map.
  const_iterator end() const { return const_iterator(); }

  /* Advanced functions for direct access: */

  /// Return the stored index for a key/value pair, optionally inserting it.
  ///
  /// \param r The key/value pair to locate or insert.
  /// \param mayInsert Whether to insert the pair when it is not present.
  /// \returns The encoded 32-bit index of the element.
  inline uint32_t recToIdx(const value_type& r, bool mayInsert = true) {
    SimpleRetT ret =
        mayInsert ? insertInternal(r.first, r.second) : findInternal(r.first);
    return encodeIndex(ret.i, ret.j);
  }

  /// Return the stored index for a moved key/value pair, optionally inserting.
  ///
  /// \param r The key/value pair to locate or insert.
  /// \param mayInsert Whether to insert the pair when it is not present.
  /// \returns The encoded 32-bit index of the element.
  inline uint32_t recToIdx(value_type&& r, bool mayInsert = true) {
    SimpleRetT ret = mayInsert
        ? insertInternal(r.first, std::move(r.second))
        : findInternal(r.first);
    return encodeIndex(ret.i, ret.j);
  }

  /// Return the stored index for a key and value, optionally inserting them.
  ///
  /// \param k The key to locate or insert.
  /// \param v The value to associate with the key when inserting.
  /// \param mayInsert Whether to insert the pair when it is not present.
  /// \returns The encoded 32-bit index of the element.
  inline uint32_t recToIdx(
      key_type k, const mapped_type& v, bool mayInsert = true) {
    SimpleRetT ret = mayInsert ? insertInternal(k, v) : findInternal(k);
    return encodeIndex(ret.i, ret.j);
  }

  /// Return the stored index for a key and moved value, optionally inserting.
  ///
  /// \param k The key to locate or insert.
  /// \param v The value to move into the map when inserting.
  /// \param mayInsert Whether to insert the pair when it is not present.
  /// \returns The encoded 32-bit index of the element.
  inline uint32_t recToIdx(key_type k, mapped_type&& v, bool mayInsert = true) {
    SimpleRetT ret =
        mayInsert ? insertInternal(k, std::move(v)) : findInternal(k);
    return encodeIndex(ret.i, ret.j);
  }

  /// Return the stored index for a key, optionally inserting a default value.
  ///
  /// \param k The key to locate or insert.
  /// \param mayInsert Whether to insert the key when it is not present.
  /// \returns The encoded 32-bit index of the element.
  inline uint32_t keyToIdx(const KeyT k, bool mayInsert = false) {
    return recToIdx(value_type(k), mayInsert);
  }

  /// Return the element stored at a given index.
  ///
  /// \param idx An encoded index previously produced by this map.
  /// \returns A const reference to the element at that index.
  inline const value_type& idxToRec(uint32_t idx) const {
    SimpleRetT ret = findAtInternal(idx);
    return subMaps_[ret.i].load(std::memory_order_relaxed)->idxToRec(ret.j);
  }

  /* Private data and helper functions... */

 private:
  // This limits primary submap size to 2^31 ~= 2 billion, secondary submap
  // size to 2^(32 - kNumSubMapBits_ - 1) = 2^27 ~= 130 million, and num subMaps
  // to 2^kNumSubMapBits_ = 16.
  static const uint32_t kNumSubMapBits_ = 4;
  static const uint32_t kSecondaryMapBit_ = 1u << 31; // Highest bit
  static const uint32_t kSubMapIndexShift_ = 32 - kNumSubMapBits_ - 1;
  static const uint32_t kSubMapIndexMask_ = (1 << kSubMapIndexShift_) - 1;
  static const uint32_t kNumSubMaps_ = 1 << kNumSubMapBits_;
  static const uintptr_t kLockedPtr_ = 0x88ULL << 48; // invalid pointer

  struct SimpleRetT {
    size_t j;
    uint32_t i;
    bool success;
    SimpleRetT(uint32_t ii, size_t jj, bool s) : j(jj), i(ii), success(s) {}
    SimpleRetT() = default;
  };

  template <
      typename LookupKeyT = key_type,
      typename LookupHashFcn = hasher,
      typename LookupEqualFcn = key_equal,
      typename LookupKeyToKeyFcn = key_convert,
      typename... ArgTs>
  SimpleRetT insertInternal(LookupKeyT key, ArgTs&&... value);

  template <
      typename LookupKeyT = key_type,
      typename LookupHashFcn = hasher,
      typename LookupEqualFcn = key_equal>
  SimpleRetT findInternal(const LookupKeyT k) const;

  SimpleRetT findAtInternal(uint32_t idx) const;

  std::atomic<SubMap*> subMaps_[kNumSubMaps_];
  std::atomic<uint32_t> numMapsAllocated_;

  inline bool tryLockMap(unsigned int idx) {
    SubMap* val = nullptr;
    return subMaps_[idx].compare_exchange_strong(
        val, (SubMap*)kLockedPtr_, std::memory_order_acquire);
  }

  static inline uint32_t encodeIndex(uint32_t subMap, uint32_t subMapIdx);

}; // AtomicHashMap

/// An `AtomicHashMap` alias that uses quadratic probing.
///
/// \tparam KeyT The key type.
/// \tparam ValueT The mapped value type.
/// \tparam HashFcn The hash function object type.
/// \tparam EqualFcn The key equality comparison type.
/// \tparam Allocator The stateless allocator type.
template <
    class KeyT,
    class ValueT,
    class HashFcn = std::hash<KeyT>,
    class EqualFcn = std::equal_to<KeyT>,
    class Allocator = std::allocator<char>>
using QuadraticProbingAtomicHashMap = AtomicHashMap<
    KeyT,
    ValueT,
    HashFcn,
    EqualFcn,
    Allocator,
    AtomicHashArrayQuadraticProbeFcn>;
} // namespace folly

#include <folly/AtomicHashMap-inl.h>
