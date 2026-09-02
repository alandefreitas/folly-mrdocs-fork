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

/**
 *  AtomicHashArray is the building block for AtomicHashMap.  It provides the
 *  core lock-free functionality, but is limited by the fact that it cannot
 *  grow past its initialization size and is a little more awkward (no public
 *  constructor, for example).  If you're confident that you won't run out of
 *  space, don't mind the awkwardness, and really need bare-metal performance,
 *  feel free to use AHA directly.
 *
 *  Check out AtomicHashMap.h for more thorough documentation on perf and
 *  general pros and cons relative to other hash maps.
 *
 */

#pragma once
#define FOLLY_ATOMICHASHARRAY_H_

#include <atomic>

#include <folly/ThreadCachedInt.h>
#include <folly/Utility.h>
#include <folly/hash/Hash.h>

namespace folly {

/// Linear probing strategy for `AtomicHashArray`.
///
/// Advances to the next slot on each collision, wrapping around the end of
/// the table.
struct AtomicHashArrayLinearProbeFcn {
  /// Compute the next slot index to probe.
  ///
  /// \param idx The current slot index.
  /// \param numProbes The number of probes performed so far (unused for
  ///        linear probing).
  /// \param capacity The total number of slots in the table.
  /// \returns The next slot index to examine.
  inline size_t operator()(
      size_t idx, size_t numProbes, size_t capacity) const {
    idx += 1; // linear probing

    // Avoid modulus because it's slow
    return FOLLY_LIKELY(idx < capacity) ? idx : (idx - capacity);
  }
};

/// Quadratic probing strategy for `AtomicHashArray`.
///
/// Advances by the current probe count on each collision, wrapping around the
/// end of the table.
struct AtomicHashArrayQuadraticProbeFcn {
  /// Compute the next slot index to probe.
  ///
  /// \param idx The current slot index.
  /// \param numProbes The number of probes performed so far.
  /// \param capacity The total number of slots in the table.
  /// \returns The next slot index to examine.
  inline size_t operator()(
      size_t idx, size_t numProbes, size_t capacity) const {
    idx += numProbes; // quadratic probing

    // Avoid modulus because it's slow
    return FOLLY_LIKELY(idx < capacity) ? idx : (idx - capacity);
  }
};

// Enables specializing checkLegalKey without specializing its class.
namespace detail {
template <typename NotKeyT, typename KeyT>
inline void checkLegalKeyIfKeyTImpl(
    NotKeyT /* ignored */,
    KeyT /* emptyKey */,
    KeyT /* lockedKey */,
    KeyT /* erasedKey */) {}

template <typename KeyT>
inline void checkLegalKeyIfKeyTImpl(
    KeyT key_in, KeyT emptyKey, KeyT lockedKey, KeyT erasedKey) {
  DCHECK_NE(key_in, emptyKey);
  DCHECK_NE(key_in, lockedKey);
  DCHECK_NE(key_in, erasedKey);
}
} // namespace detail

/// Lock-free, growable hash map built on top of `AtomicHashArray`.
template <
    class KeyT,
    class ValueT,
    class HashFcn = std::hash<KeyT>,
    class EqualFcn = std::equal_to<KeyT>,
    class Allocator = std::allocator<char>,
    class ProbeFcn = AtomicHashArrayLinearProbeFcn,
    class KeyConvertFcn = Identity>
class AtomicHashMap;

/// Fixed-size, lock-free hash array and building block for `AtomicHashMap`.
///
/// Provides the core lock-free functionality but cannot grow past its
/// initialization size. Keys must be atomically compare-and-swappable integer
/// or pointer types. See `AtomicHashMap` for the growable container built on
/// top of this one.
template <
    class KeyT,
    class ValueT,
    class HashFcn = std::hash<KeyT>,
    class EqualFcn = std::equal_to<KeyT>,
    class Allocator = std::allocator<char>,
    class ProbeFcn = AtomicHashArrayLinearProbeFcn,
    class KeyConvertFcn = Identity>
class AtomicHashArray {
  static_assert(
      (std::is_convertible<KeyT, int32_t>::value ||
       std::is_convertible<KeyT, int64_t>::value ||
       std::is_convertible<KeyT, const void*>::value),
      "You are trying to use AtomicHashArray with disallowed key "
      "types.  You must use atomically compare-and-swappable integer "
      "keys, or a different container class.");

 public:
  /// The type of the keys stored in the map.
  using key_type = KeyT;
  /// The type of the mapped values.
  using mapped_type = ValueT;
  /// The hash function type used to hash keys.
  using hasher = HashFcn;
  /// The equality comparison type used to compare keys.
  using key_equal = EqualFcn;
  /// The functor type used to convert a lookup key into a stored key.
  using key_convert = KeyConvertFcn;
  /// The type of the elements stored in the map.
  using value_type = std::pair<const KeyT, ValueT>;
  /// An unsigned integral type used for sizes and capacities.
  using size_type = std::size_t;
  /// A signed integral type used for distances between iterators.
  using difference_type = std::ptrdiff_t;
  /// A reference to an element.
  using reference = value_type&;
  /// A const reference to an element.
  using const_reference = const value_type&;
  /// A pointer to an element.
  using pointer = value_type*;
  /// A const pointer to an element.
  using const_pointer = const value_type*;

  /// The total number of slots in the underlying array.
  const size_t capacity_;
  /// The maximum number of entries allowed before the map is considered full.
  const size_t maxEntries_;
  /// The sentinel key value that marks an empty slot.
  const KeyT kEmptyKey_;
  /// The sentinel key value that marks a slot locked during insertion.
  const KeyT kLockedKey_;
  /// The sentinel key value that marks an erased slot.
  const KeyT kErasedKey_;

  /// Random-access iterator over the elements of an `AtomicHashArray`.
  template <class ContT, class IterVal>
  struct aha_iterator;

  /// A read-only iterator over the elements of the map.
  using const_iterator = aha_iterator<const AtomicHashArray, const value_type>;
  /// A mutable iterator over the elements of the map.
  using iterator = aha_iterator<AtomicHashArray, value_type>;

  /// Destroy an `AtomicHashArray` created with `create`.
  ///
  /// You really shouldn't need this if you use the `SmartPtr` provided by
  /// `create`, but if you really want to do something crazy like stick the
  /// released pointer into a `DiscriminatedPtr` or something, you'll need this
  /// to clean up after yourself.
  ///
  /// \param arr The array to destroy.
  static void destroy(AtomicHashArray* arr);

 private:
  const size_t kAnchorMask_;

  struct Deleter {
    void operator()(AtomicHashArray* ptr) { AtomicHashArray::destroy(ptr); }
  };

 public:
  /// A `unique_ptr` to an `AtomicHashArray` with a custom deleter.
  using SmartPtr = std::unique_ptr<AtomicHashArray, Deleter>;

  /// Named parameters used to configure `create`.
  ///
  /// The struct has sensible defaults for everything, but is overloaded: if
  /// you specify a positive `capacity`, that value is used directly instead of
  /// computing it based on `maxLoadFactor`.
  struct Config {
    /// The sentinel key value that marks an empty slot.
    KeyT emptyKey;
    /// The sentinel key value that marks a slot locked during insertion.
    KeyT lockedKey;
    /// The sentinel key value that marks an erased slot.
    KeyT erasedKey;
    /// The maximum load factor before the map is considered full.
    double maxLoadFactor;
    /// The factor by which the owning map grows when this array fills up.
    double growthFactor;
    /// The per-thread cache size for the entry counters.
    uint32_t entryCountThreadCacheSize;
    /// The explicit slot count; if positive, overrides `maxLoadFactor`.
    size_t capacity;

    /// Construct a `Config` with default values.
    ///
    /// Cannot be a constexpr constructor because some compilers rightly
    /// complain.
    Config()
        : emptyKey((KeyT)-1),
          lockedKey((KeyT)-2),
          erasedKey((KeyT)-3),
          maxLoadFactor(0.8),
          growthFactor(-1),
          entryCountThreadCacheSize(1000),
          capacity(0) {}
  };

  /// Create an `AtomicHashArray`.
  ///
  /// Use this instead of a constructor. It avoids the perf penalty of a second
  /// pointer indirection when composing these into `AtomicHashMap`, and takes
  /// a max size plus a `Config` struct to simulate named constructor
  /// parameters. Cannot have a pre-instantiated const `Config` instance
  /// because of SIOF.
  ///
  /// \param maxSize The maximum number of elements the array can hold.
  /// \param c The configuration parameters for the new array.
  /// \returns A `SmartPtr` owning the newly created array.
  static SmartPtr create(size_t maxSize, const Config& c = Config());

  /// Find the element with the given key.
  ///
  /// As an optional feature, the type of the key to look up (`LookupKeyT`) is
  /// allowed to be different from the type of keys actually stored (`KeyT`).
  /// This enables use cases where materializing the key is costly and usually
  /// redundant, e.g., canonicalizing/interning a set of strings and being able
  /// to look up by `StringPiece`. To use this feature, `LookupHashFcn` must
  /// take a `LookupKeyT`, and `LookupEqualFcn` must take `KeyT` and
  /// `LookupKeyT` as first and second parameter, respectively.
  ///
  /// \tparam LookupKeyT The type of the key used for lookup.
  /// \tparam LookupHashFcn The hash functor applied to the lookup key.
  /// \tparam LookupEqualFcn The equality functor comparing stored and lookup
  ///         keys.
  /// \param k The key to look up.
  /// \returns An iterator to the element if found, otherwise `end()`.
  template <
      typename LookupKeyT = key_type,
      typename LookupHashFcn = hasher,
      typename LookupEqualFcn = key_equal>
  iterator find(LookupKeyT k) {
    return iterator(
        this, findInternal<LookupKeyT, LookupHashFcn, LookupEqualFcn>(k).idx);
  }

  /// Find the element with the given key.
  ///
  /// \tparam LookupKeyT The type of the key used for lookup.
  /// \tparam LookupHashFcn The hash functor applied to the lookup key.
  /// \tparam LookupEqualFcn The equality functor comparing stored and lookup
  ///         keys.
  /// \param k The key to look up.
  /// \returns A const iterator to the element if found, otherwise `end()`.
  template <
      typename LookupKeyT = key_type,
      typename LookupHashFcn = hasher,
      typename LookupEqualFcn = key_equal>
  const_iterator find(LookupKeyT k) const {
    return const_cast<AtomicHashArray*>(this)
        ->find<LookupKeyT, LookupHashFcn, LookupEqualFcn>(k);
  }

  /// Insert an element into the map.
  ///
  /// Fails on key collision (does not overwrite) or if the map becomes full,
  /// at which point no element is inserted, the iterator is set to `end()`,
  /// and success is set false. On collisions, success is set false, but the
  /// iterator is set to the existing entry. Retrieve the index with
  /// `ret.first.getIndex()`.
  ///
  /// \param r The key/value pair to insert.
  /// \returns A pair with an iterator to the element and a bool success flag.
  std::pair<iterator, bool> insert(const value_type& r) {
    return emplace(r.first, r.second);
  }
  /// Insert an element into the map by moving it.
  ///
  /// \param r The key/value pair to insert.
  /// \returns A pair with an iterator to the element and a bool success flag.
  std::pair<iterator, bool> insert(value_type&& r) {
    return emplace(r.first, std::move(r.second));
  }

  /// Insert an element, constructing the value in place.
  ///
  /// Same contract as `insert()`, but performs in-place construction of the
  /// value type using the specified arguments. Also, like `find()`, this
  /// method optionally allows `key_in` to have a type different from that
  /// stored in the table. If and only if no equal key is already present, this
  /// method converts `key_in` to a key of type `KeyT` using the provided
  /// `LookupKeyToKeyFcn`.
  ///
  /// \tparam LookupKeyT The type of the key used for lookup.
  /// \tparam LookupHashFcn The hash functor applied to the lookup key.
  /// \tparam LookupEqualFcn The equality functor comparing stored and lookup
  ///         keys.
  /// \tparam LookupKeyToKeyFcn The functor converting the lookup key to a
  ///         stored key.
  /// \tparam ArgTs The types of the arguments forwarded to the value
  ///         constructor.
  /// \param key_in The key to insert.
  /// \param vCtorArgs The arguments forwarded to construct the mapped value.
  /// \returns A pair with an iterator to the element and a bool success flag.
  template <
      typename LookupKeyT = key_type,
      typename LookupHashFcn = hasher,
      typename LookupEqualFcn = key_equal,
      typename LookupKeyToKeyFcn = key_convert,
      typename... ArgTs>
  std::pair<iterator, bool> emplace(LookupKeyT key_in, ArgTs&&... vCtorArgs) {
    SimpleRetT ret = insertInternal<
        LookupKeyT,
        LookupHashFcn,
        LookupEqualFcn,
        LookupKeyToKeyFcn>(key_in, std::forward<ArgTs>(vCtorArgs)...);
    return std::make_pair(iterator(this, ret.idx), ret.success);
  }

  /// Erase the element with the given key.
  ///
  /// \param k The key to erase.
  /// \returns The number of elements erased, which should never exceed 1.
  size_t erase(KeyT k);

  /// Clear all keys and values in the map and reset all counters.
  ///
  /// Not thread safe.
  void clear();

  /// Return the exact number of elements in the map.
  ///
  /// Note that `readFull()` acquires a mutex; see `folly/ThreadCachedInt.h`
  /// for more details.
  ///
  /// \returns The number of elements currently stored.
  size_t size() const {
    return numEntries_.readFull() - numErases_.load(std::memory_order_relaxed);
  }

  /// Check whether the map is empty.
  ///
  /// \returns True if the map holds no elements, false otherwise.
  bool empty() const { return size() == 0; }

  /// Return an iterator to the first element.
  ///
  /// \returns An iterator to the first non-empty slot, or `end()` if empty.
  iterator begin() {
    iterator it(this, 0);
    it.advancePastEmpty();
    return it;
  }
  /// Return a const iterator to the first element.
  ///
  /// \returns A const iterator to the first non-empty slot, or `end()` if
  ///          empty.
  const_iterator begin() const {
    const_iterator it(this, 0);
    it.advancePastEmpty();
    return it;
  }

  /// Return an iterator past the last element.
  ///
  /// \returns An iterator referring to one past the last slot.
  iterator end() { return iterator(this, capacity_); }
  /// Return a const iterator past the last element.
  ///
  /// \returns A const iterator referring to one past the last slot.
  const_iterator end() const { return const_iterator(this, capacity_); }

  /// Access an element directly by slot index.
  ///
  /// See `AtomicHashMap::findAt`. WARNING: this function will fail silently
  /// for a hashtable with capacity greater than 2^32.
  ///
  /// \param idx The slot index to access.
  /// \returns An iterator referring to the slot at `idx`.
  iterator findAt(uint32_t idx) {
    DCHECK_LT(idx, capacity_);
    return iterator(this, idx);
  }
  /// Access an element directly by slot index.
  ///
  /// See `AtomicHashMap::findAt`. WARNING: this function will fail silently
  /// for a hashtable with capacity greater than 2^32.
  ///
  /// \param idx The slot index to access.
  /// \returns A const iterator referring to the slot at `idx`.
  const_iterator findAt(uint32_t idx) const {
    return const_cast<AtomicHashArray*>(this)->findAt(idx);
  }

  /// Build an iterator referring to the given slot index.
  ///
  /// \param idx The slot index the iterator should refer to.
  /// \returns An iterator referring to the slot at `idx`.
  iterator makeIter(size_t idx) { return iterator(this, idx); }
  /// Build a const iterator referring to the given slot index.
  ///
  /// \param idx The slot index the iterator should refer to.
  /// \returns A const iterator referring to the slot at `idx`.
  const_iterator makeIter(size_t idx) const {
    return const_iterator(this, idx);
  }

  /// Return the maximum load factor allowed for this map.
  ///
  /// \returns The ratio of the maximum entry count to the capacity.
  double maxLoadFactor() const { return ((double)maxEntries_) / capacity_; }

  /// Set the per-thread cache size for the entry counters.
  ///
  /// \param newSize The new per-thread cache size.
  void setEntryCountThreadCacheSize(uint32_t newSize) {
    numEntries_.setCacheSize(newSize);
    numPendingEntries_.setCacheSize(newSize);
  }

  /// Return the per-thread cache size for the entry counters.
  ///
  /// \returns The current per-thread cache size.
  uint32_t getEntryCountThreadCacheSize() const {
    return numEntries_.getCacheSize();
  }

  /* Private data and helper functions... */

 private:
  friend class AtomicHashMap<
      KeyT,
      ValueT,
      HashFcn,
      EqualFcn,
      Allocator,
      ProbeFcn>;

  struct SimpleRetT {
    size_t idx;
    bool success;
    SimpleRetT(size_t i, bool s) : idx(i), success(s) {}
    SimpleRetT() = default;
  };

  template <
      typename LookupKeyT = key_type,
      typename LookupHashFcn = hasher,
      typename LookupEqualFcn = key_equal,
      typename LookupKeyToKeyFcn = Identity,
      typename... ArgTs>
  SimpleRetT insertInternal(LookupKeyT key, ArgTs&&... vCtorArgs);

  template <
      typename LookupKeyT = key_type,
      typename LookupHashFcn = hasher,
      typename LookupEqualFcn = key_equal>
  SimpleRetT findInternal(const LookupKeyT key);

  template <typename MaybeKeyT>
  void checkLegalKeyIfKey(MaybeKeyT key) {
    detail::checkLegalKeyIfKeyTImpl(key, kEmptyKey_, kLockedKey_, kErasedKey_);
  }

  static std::atomic<KeyT>* cellKeyPtr(const value_type& r) {
    // We need some illegal casting here in order to actually store
    // our value_type as a std::pair<const,>.  But a little bit of
    // undefined behavior never hurt anyone ...
    static_assert(
        sizeof(std::atomic<KeyT>) == sizeof(KeyT),
        "std::atomic is implemented in an unexpected way for AHM");
    return const_cast<std::atomic<KeyT>*>(
        reinterpret_cast<std::atomic<KeyT> const*>(&r.first));
  }

  static KeyT relaxedLoadKey(const value_type& r) {
    return cellKeyPtr(r)->load(std::memory_order_relaxed);
  }

  static KeyT acquireLoadKey(const value_type& r) {
    return cellKeyPtr(r)->load(std::memory_order_acquire);
  }

  // Fun with thread local storage - atomic increment is expensive
  // (relatively), so we accumulate in the thread cache and periodically
  // flush to the actual variable, and walk through the unflushed counts when
  // reading the value, so be careful of calling size() too frequently.  This
  // increases insertion throughput several times over while keeping the count
  // accurate.
  ThreadCachedInt<uint64_t> numEntries_; // Successful key inserts
  ThreadCachedInt<uint64_t> numPendingEntries_; // Used by insertInternal
  std::atomic<int64_t> isFull_; // Used by insertInternal
  std::atomic<int64_t> numErases_; // Successful key erases

  value_type cells_[0]; // This must be the last field of this class

  // Force constructor/destructor private since create/destroy should be
  // used externally instead
  AtomicHashArray(
      size_t capacity,
      KeyT emptyKey,
      KeyT lockedKey,
      KeyT erasedKey,
      double maxLoadFactor,
      uint32_t cacheSize);

  AtomicHashArray(const AtomicHashArray&) = delete;
  AtomicHashArray& operator=(const AtomicHashArray&) = delete;

  ~AtomicHashArray() = default;

  inline void unlockCell(value_type* const cell, KeyT newKey) {
    cellKeyPtr(*cell)->store(newKey, std::memory_order_release);
  }

  inline bool tryLockCell(value_type* const cell) {
    KeyT expect = kEmptyKey_;
    return cellKeyPtr(*cell)->compare_exchange_strong(
        expect, kLockedKey_, std::memory_order_acq_rel);
  }

  template <class LookupKeyT = key_type, class LookupHashFcn = hasher>
  inline size_t keyToAnchorIdx(const LookupKeyT k) const {
    const size_t hashVal = LookupHashFcn()(k);
    const size_t probe = hashVal & kAnchorMask_;
    return FOLLY_LIKELY(probe < capacity_) ? probe : hashVal % capacity_;
  }

}; // AtomicHashArray

} // namespace folly

#include <folly/AtomicHashArray-inl.h>
