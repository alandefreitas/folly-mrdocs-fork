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

#include <algorithm>
#include <exception>
#include <functional>
#include <memory>

#include <boost/intrusive/list.hpp>
#include <boost/iterator/iterator_adaptor.hpp>

#include <folly/CppAttributes.h>
#include <folly/container/F14Set.h>
#include <folly/container/HeterogeneousAccess.h>
#include <folly/lang/Exception.h>

namespace folly {

/**
 * A general purpose LRU evicting cache designed to support constant time
 * set/get/insert/erase ops. The only required configuration parameter is the
 * `maxSize`, which is the maximum number of entries held by the cache, which
 * is also dynamically changeable. Insertion will evict (and destroy with ~TKey
 * and ~TValue) existing entries in LRU order as needed to keep number of
 * entries less than maxSize. When automatic eviction is triggered, the
 * minimum number of evictions is `clearSize`, which is configurable with a
 * default of 1. If a callback is specified with setPruneHook, it is invoked
 * for each eviction. However, the prune hook cannot manage object lifetimes
 * because it is not invoked on erase nor cache destruction.
 *
 * This is NOT a thread-safe implementation.
 *
 * Iterators and references are only invalidated when the referenced entry
 * might have been removed (pruned or erased), like std::map.
 *
 * NOTE: maxSize==0 is a special case that disables automatic evictions.
 * prune() can be used for manually trimming down the number of entries.
 *
 * Implementation: Maintains a doubly linked list (`lru_`) of entry nodes in
 * LRU order, which are also connected to hash table index (`index_`). The
 * access order is maintained on the list by moving an element to the front
 * of list on a get, and adding to the front on insert. Assuming quality
 * hashing, set/get are both constant time operations.
 *
 * NOTE: Previous versions of this structure used a hash table size that was
 * fixed at creation time, but that limitation is no longer present.
 */
template <
    class TKey,
    class TValue,
    class THash = HeterogeneousAccessHash<TKey>,
    class TKeyEqual = HeterogeneousAccessEqualTo<TKey>>
class EvictingCacheMap {
 private:
  // typedefs for brevity
  struct Node;
  struct NodeList;
  struct KeyHasher;
  struct KeyValueEqual;
  using NodeMap = F14VectorSet<Node*, KeyHasher, KeyValueEqual>;
  using TPair = std::pair<const TKey, TValue>;

 public:
  /// Callback type invoked on eviction with the key and value.
  using PruneHookCall = std::function<void(TKey, TValue&&)>;

  /// Iterator base that returns TPair on dereference.
  template <typename Value, typename TIterator>
  class iterator_base
      : public boost::iterator_adaptor<
            iterator_base<Value, TIterator>,
            TIterator,
            Value,
            boost::bidirectional_traversal_tag> {
   public:
    /// Constructs a singular iterator.
    iterator_base() {}

    /// Constructs from an underlying node-list iterator.
    ///
    /// \param it The underlying node-list iterator to wrap.
    explicit iterator_base(TIterator it)
        : iterator_base::iterator_adaptor_(it) {}

    /// Converts from a compatible iterator.
    ///
    /// \param other The compatible iterator to convert from.
    template <typename V, typename I>
      requires(
          std::is_same<V const, Value>::value &&
          std::is_convertible<I, TIterator>::value)
    /* implicit */ iterator_base(iterator_base<V, I> const& other)
        : iterator_base::iterator_adaptor_(other.base()) {}

    /// Returns a reference to the pointed-to key-value pair.
    ///
    /// \returns A reference to the pointed-to key-value pair.
    Value& dereference() const { return this->base_reference()->pr; }
  };

  // iterators
  /// Iterator over key-value pairs in LRU order.
  using iterator = iterator_base<TPair, typename NodeList::iterator>;
  /// Const iterator over key-value pairs in LRU order.
  using const_iterator =
      iterator_base<const TPair, typename NodeList::const_iterator>;
  /// Reverse iterator over key-value pairs in LRU order.
  using reverse_iterator =
      iterator_base<TPair, typename NodeList::reverse_iterator>;
  /// Const reverse iterator over key-value pairs in LRU order.
  using const_reverse_iterator =
      iterator_base<const TPair, typename NodeList::const_reverse_iterator>;

  // public type aliases for convenience
  /// The key type.
  using key_type = TKey;
  /// The mapped value type.
  using mapped_type = TValue;
  /// The hash function type.
  using hasher = THash;

  /**
   * Approximate size of memory used by each entry added to the cache,
   * including the shallow bits (sizeof) of TKey and TValue, but not the deep
   * bits. Using 128 (bytes per chunk) / 10 (avg entries per chunk) as
   * approximate F14 index entry size.
   */
  static constexpr std::size_t kApproximateEntryMemUsage = 13 + sizeof(Node);

 private:
  template <typename K>
  using IsIter = std::disjunction<
      std::is_same<iterator, remove_cvref_t<K>>,
      std::is_same<const_iterator, remove_cvref_t<K>>>;

  template <typename K>
  static constexpr bool kEligibleForHeterogeneousFind =
      detail::EligibleForHeterogeneousFind<TKey, THash, TKeyEqual, K>::value;

  template <typename K>
  static constexpr bool kEligibleForHeterogeneousInsert =
      detail::EligibleForHeterogeneousInsert<TKey, THash, TKeyEqual, K>::value;

  template <typename K>
  static constexpr bool kEligibleForHeterogeneousErase =
      detail::EligibleForHeterogeneousFind<
          TKey,
          THash,
          TKeyEqual,
          std::conditional_t<IsIter<K>::value, TKey, K>>::value &&
      !IsIter<K>::value;

 public:
  /**
   * Construct a EvictingCacheMap
   * @param maxSize maximum size of the cache map.  Once the map size exceeds
   *     maxSize, the map will begin to evict.
   * @param clearSize the number of elements to clear at a time when automatic
   *     eviction on insert is triggered.
   * @param keyHash hash function used to index keys
   * @param keyEqual equality comparison used to match keys
   */
  explicit EvictingCacheMap(
      std::size_t maxSize,
      std::size_t clearSize = 1,
      const THash& keyHash = THash(),
      const TKeyEqual& keyEqual = TKeyEqual())
      : keyHash_(keyHash),
        keyEqual_(keyEqual),
        index_(maxSize + /*transient*/ 1, keyHash_, keyEqual_),
        maxSize_(maxSize),
        clearSize_(clampClearSize(clearSize)) {}

  /// Copy construction is disabled.
  ///
  /// \param other The cache map that would be copied.
  EvictingCacheMap(const EvictingCacheMap& other) = delete;
  /// Copy assignment is disabled.
  ///
  /// \param other The cache map that would be copied.
  /// \returns A reference to this cache map.
  EvictingCacheMap& operator=(const EvictingCacheMap& other) = delete;
  /// Move-constructs from another cache map.
  ///
  /// \param other The cache map to move from.
  EvictingCacheMap(EvictingCacheMap&& other) = default;
  /// Move-assigns from another cache map.
  ///
  /// \param other The cache map to move from.
  /// \returns A reference to this cache map.
  EvictingCacheMap& operator=(EvictingCacheMap&& other) = default;

  /// Destroys the cache map.
  ~EvictingCacheMap() { assert(lru_.size() == index_.size()); }

  /**
   * Adjust the max size of EvictingCacheMap, evicting as needed to ensure the
   * new max is not exceeded.
   *
   * Calling this function with an argument of 0 removes the limit on the cache
   * size and elements are not evicted unless clients explicitly call prune.
   *
   * @param maxSize new maximum size of the cache map.
   * @param pruneHook eviction callback to use INSTEAD OF the configured one
   */
  void setMaxSize(size_t maxSize, PruneHookCall pruneHook = nullptr) {
    if (maxSize != 0 && maxSize < size()) {
      // Prune the excess elements with our new constraints.
      prune(std::max(size() - maxSize, clearSize_), pruneHook);
    }
    maxSize_ = maxSize;
  }

  /// Returns the maximum size of the cache map.
  ///
  /// \returns The maximum size of the cache map.
  std::size_t getMaxSize() const { return maxSize_; }

  /// Set the number of elements to evict at a time on automatic eviction.
  ///
  /// \param clearSize The number of elements to clear per automatic eviction.
  void setClearSize(std::size_t clearSize) {
    clearSize_ = clampClearSize(clearSize);
  }

  /**
   * Check for existence of a specific key in the map.  This operation has
   *     no effect on LRU order.
   * @param key key to search for
   * @return true if exists, false otherwise
   */
  bool exists(const TKey& key) const { return existsImpl(key); }

  /// Check for existence of a specific key in the map.
  ///
  /// \param key The key to search for.
  /// \returns True if the key exists, false otherwise.
  template <typename K>
    requires kEligibleForHeterogeneousFind<K>
  bool exists(const K& key) const {
    return existsImpl(key);
  }

  /**
   * Get the value associated with a specific key.  This function always
   *     promotes a found value to the head of the LRU.
   * @param key key associated with the value
   * @return the value if it exists
   * @throw std::out_of_range exception of the key does not exist
   */
  TValue& get(const TKey& key) [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return getImpl(key);
  }

  /// Get the value associated with a specific key, promoting it in the LRU.
  ///
  /// \param key The key associated with the value.
  /// \returns A reference to the value if it exists.
  template <typename K>
    requires kEligibleForHeterogeneousFind<K>
  TValue& get(const K& key) [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return getImpl(key);
  }

  /**
   * Get the iterator associated with a specific key.  This function always
   *     promotes a found value to the head of the LRU.
   * @param key key to associate with value
   * @return the iterator of the object (a std::pair of const TKey, TValue) or
   *     end() if it does not exist
   */
  iterator find(const TKey& key) [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return findImpl(*this, key);
  }

  /// Get the iterator associated with a specific key, promoting it in the LRU.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the element, or end() if it does not exist.
  template <typename K>
    requires kEligibleForHeterogeneousFind<K>
  iterator find(const K& key) [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return findImpl(*this, key);
  }

  /**
   * Get the value associated with a specific key.  This function never
   *     promotes a found value to the head of the LRU.
   * @param key key associated with the value
   * @return the value if it exists
   * @throw std::out_of_range exception of the key does not exist
   */
  const TValue& getWithoutPromotion(const TKey& key) const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return getWithoutPromotionImpl(*this, key);
  }

  /// Get the value associated with a specific key without LRU promotion.
  ///
  /// \param key The key associated with the value.
  /// \returns A reference to the value if it exists.
  template <typename K>
    requires kEligibleForHeterogeneousFind<K>
  const TValue& getWithoutPromotion(const K& key) const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return getWithoutPromotionImpl(*this, key);
  }

  /// Get the value associated with a specific key without LRU promotion.
  ///
  /// \param key The key associated with the value.
  /// \returns A reference to the value if it exists.
  TValue& getWithoutPromotion(const TKey& key)
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return getWithoutPromotionImpl(*this, key);
  }

  /// Get the value associated with a specific key without LRU promotion.
  ///
  /// \param key The key associated with the value.
  /// \returns A reference to the value if it exists.
  template <typename K>
    requires kEligibleForHeterogeneousFind<K>
  TValue& getWithoutPromotion(const K& key) [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return getWithoutPromotionImpl(*this, key);
  }

  /**
   * Get the iterator associated with a specific key.  This function never
   *     promotes a found value to the head of the LRU.
   * @param key key to associate with value
   * @return the iterator of the object (a std::pair of const TKey, TValue) or
   *     end() if it does not exist
   */
  const_iterator findWithoutPromotion(const TKey& key) const {
    return findWithoutPromotionImpl(*this, key);
  }

  /// Get the iterator associated with a specific key without LRU promotion.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the element, or end() if it does not exist.
  template <typename K>
    requires kEligibleForHeterogeneousFind<K>
  const_iterator findWithoutPromotion(const K& key) const {
    return findWithoutPromotionImpl(*this, key);
  }

  /// Get the iterator associated with a specific key without LRU promotion.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the element, or end() if it does not exist.
  iterator findWithoutPromotion(const TKey& key) {
    return findWithoutPromotionImpl(*this, key);
  }

  /// Get the iterator associated with a specific key without LRU promotion.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the element, or end() if it does not exist.
  template <typename K>
    requires kEligibleForHeterogeneousFind<K>
  iterator findWithoutPromotion(const K& key) {
    return findWithoutPromotionImpl(*this, key);
  }

  /**
   * Erase the key-value pair associated with key if it exists. Prune hook
   * is not called unless one passed in here.
   * @param key key associated with the value
   * @param eraseHook callback to use with erased entry (similar to a prune
   * hook)
   * @return true if the key existed and was erased, else false
   */
  bool erase(const TKey& key, PruneHookCall eraseHook = nullptr) {
    return eraseKeyImpl(key, eraseHook);
  }

  /// Erase the key-value pair associated with key if it exists.
  ///
  /// \param key The key associated with the value.
  /// \param eraseHook Callback to use with the erased entry, like a prune hook.
  /// \returns True if the key existed and was erased, false otherwise.
  template <typename K>
    requires kEligibleForHeterogeneousErase<K>
  bool erase(const K& key, PruneHookCall eraseHook = nullptr) {
    return eraseKeyImpl(key, eraseHook);
  }

  /**
   * Erase the key-value pair associated with pos. Prune hook is not called
   * unless one passed in here.
   * @param pos iterator to the element to be erased
   * @param eraseHook callback to use with erased entry (similar to a prune
   * hook)
   * @return iterator to the following element or end() if pos was the last
   *     element
   */
  iterator erase(const_iterator pos, PruneHookCall eraseHook = nullptr) {
    return iterator(
        eraseImpl(const_cast<Node*>(&(*pos.base())), pos.base(), eraseHook));
  }

  /**
   * Set a key-value pair in the dictionary
   * @param key key to associate with value
   * @param value value to associate with the key
   * @param promote boolean flag indicating whether or not to move something
   *     to the front of an LRU.  This only really matters if you're setting
   *     a value that already exists.
   * @param pruneHook eviction callback to use INSTEAD OF the configured one
   */
  void set(
      const TKey& key,
      TValue&& value,
      bool promote = true,
      PruneHookCall pruneHook = nullptr) {
    setImpl(key, std::move(value), promote, pruneHook);
  }

  /// Set a key-value pair in the dictionary, copying the value.
  ///
  /// \param key The key to associate with the value.
  /// \param value The value to associate with the key.
  /// \param promote Whether to move a pre-existing entry to the front of the LRU.
  /// \param pruneHook Eviction callback to use instead of the configured one.
  void set(
      const TKey& key,
      const TValue& value,
      bool promote = true,
      PruneHookCall pruneHook = nullptr) {
    TValue tmp{value}; // can't yet rely on temporary materialization
    setImpl(key, std::move(tmp), promote, pruneHook);
  }

  /// Set a key-value pair in the dictionary.
  ///
  /// \param key The key to associate with the value.
  /// \param value The value to associate with the key.
  /// \param promote Whether to move a pre-existing entry to the front of the LRU.
  /// \param pruneHook Eviction callback to use instead of the configured one.
  template <typename K>
    requires kEligibleForHeterogeneousInsert<K>
  void set(
      const K& key,
      TValue&& value,
      bool promote = true,
      PruneHookCall pruneHook = nullptr) {
    setImpl(key, std::move(value), promote, pruneHook);
  }

  /// Set a key-value pair in the dictionary, copying the value.
  ///
  /// \param key The key to associate with the value.
  /// \param value The value to associate with the key.
  /// \param promote Whether to move a pre-existing entry to the front of the LRU.
  /// \param pruneHook Eviction callback to use instead of the configured one.
  template <typename K>
    requires kEligibleForHeterogeneousInsert<K>
  void set(
      const K& key,
      const TValue& value,
      bool promote = true,
      PruneHookCall pruneHook = nullptr) {
    TValue tmp{value}; // can't yet rely on temporary materialization
    setImpl(key, std::move(tmp), promote, pruneHook);
  }

  /**
   * Insert a new key-value pair in the dictionary if no element exists for key
   * @param key key to associate with value
   * @param value value to associate with the key
   * @param pruneHook eviction callback to use INSTEAD OF the configured one
   * @return a pair consisting of an iterator to the inserted element (or to the
   *     element that prevented the insertion) and a bool denoting whether the
   *     insertion took place.
   */
  std::pair<iterator, bool> insert(
      const TKey& key, TValue&& value, PruneHookCall pruneHook = nullptr) {
    return insertImpl(key, std::move(value), pruneHook);
  }

  /// Insert a new key-value pair if no element exists for key, copying the
  /// value.
  ///
  /// \param key The key to associate with the value.
  /// \param value The value to associate with the key.
  /// \param pruneHook Eviction callback to use instead of the configured one.
  /// \returns A pair of an iterator to the element and whether insertion took place.
  std::pair<iterator, bool> insert(
      const TKey& key, const TValue& value, PruneHookCall pruneHook = nullptr) {
    TValue tmp{value}; // can't yet rely on temporary materialization
    return insertImpl(key, std::move(tmp), pruneHook);
  }

  /// Insert a new key-value pair if no element exists for key.
  ///
  /// \param key The key to associate with the value.
  /// \param value The value to associate with the key.
  /// \param pruneHook Eviction callback to use instead of the configured one.
  /// \returns A pair of an iterator to the element and whether insertion took place.
  template <typename K>
    requires kEligibleForHeterogeneousInsert<K>
  std::pair<iterator, bool> insert(
      const K& key, TValue&& value, PruneHookCall pruneHook = nullptr) {
    return insertImpl(key, std::move(value), pruneHook);
  }

  /// Insert a new key-value pair if no element exists for key, copying the
  /// value.
  ///
  /// \param key The key to associate with the value.
  /// \param value The value to associate with the key.
  /// \param pruneHook Eviction callback to use instead of the configured one.
  /// \returns A pair of an iterator to the element and whether insertion took place.
  template <typename K>
    requires kEligibleForHeterogeneousInsert<K>
  std::pair<iterator, bool> insert(
      const K& key, const TValue& value, PruneHookCall pruneHook = nullptr) {
    TValue tmp{value}; // can't yet rely on temporary materialization
    return insertImpl(key, std::move(tmp), pruneHook);
  }

  /**
   * Emplace a new key-value pair in the dictionary if no element exists for
   * key, utilizing the configured prunehook
   * @param key key to associate with value
   * @param args args to construct TValue in place, to associate with the key
   * @return a pair consisting of an iterator to the inserted element (or to the
   *     element that prevented the insertion) and a bool denoting whether the
   *     insertion took place.
   */
  template <typename K, typename... Args>
  std::pair<iterator, bool> try_emplace(const K& key, Args&&... args) {
    return emplaceWithPruneHook<K, Args...>(
        key, std::forward<Args>(args)..., nullptr);
  }

  /**
   * Emplace a new key-value pair in the dictionary if no element exists for key
   * @param key key to associate with value
   * @param args args to construct TValue in place, to associate with the key
   * @param pruneHook eviction callback to use INSTEAD OF the configured one
   * @return a pair consisting of an iterator to the inserted element (or to the
   *     element that prevented the insertion) and a bool denoting whether the
   *     insertion took place.
   */
  template <typename K, typename... Args>
  std::pair<iterator, bool> emplaceWithPruneHook(
      const K& key, Args&&... args, PruneHookCall pruneHook) {
    return insertImpl<K>(
        std::make_unique<Node>(
            std::piecewise_construct, key, std::forward<Args>(args)...),
        pruneHook);
  }

  /**
   * Get the number of elements in the dictionary
   * @return the size of the dictionary
   */
  std::size_t size() const {
    assert(index_.size() == lru_.size());
    return index_.size();
  }

  /**
   * Typical empty function
   * @return true if empty, false otherwise
   */
  bool empty() const { return index_.empty(); }

  /**
   * Remove all entries (as if all evicted)
   * @param pruneHook eviction callback to use INSTEAD OF the configured one
   */
  void clear(PruneHookCall pruneHook = nullptr) { prune(size(), pruneHook); }

  /**
   * Set the prune hook, which is the function invoked on the key and value
   *     on each eviction. An operation will throw if the pruneHook throws.
   *     Note that this prune hook is not automatically called on entries
   *     explicitly erase()ed nor on remaining entries at destruction time.
   * @param pruneHook eviction callback to set as default, or nullptr to clear
   */
  void setPruneHook(PruneHookCall pruneHook) { pruneHook_ = pruneHook; }

  /// Returns the currently configured prune hook.
  ///
  /// \returns The currently configured prune hook.
  PruneHookCall getPruneHook() { return pruneHook_; }

  /**
   * Prune the minimum of pruneSize and size() from the back of the LRU.
   * Will throw if pruneHook throws.
   * @param pruneSize minimum number of elements to prune
   * @param pruneHook eviction callback to use INSTEAD OF the configured one
   */
  void prune(std::size_t pruneSize, PruneHookCall pruneHook = nullptr) {
    auto& ph = (nullptr == pruneHook) ? pruneHook_ : pruneHook;

    for (std::size_t i = 0; i < pruneSize && !lru_.empty(); i++) {
      auto* node = &(*lru_.rbegin());
      std::unique_ptr<Node> node_owner(node);

      lru_.erase(lru_.iterator_to(*node));
      index_.erase(node);
      if (ph) {
        // NOTE: might throw, so we are in an exception-safe state
        ph(node->pr.first, std::move(node->pr.second));
      }
    }
  }

  // Iterators and such
  /// Returns an iterator to the most recently used entry.
  ///
  /// \returns An iterator to the most recently used entry.
  iterator begin() [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return iterator(lru_.begin());
  }
  /// Returns an iterator past the least recently used entry.
  ///
  /// \returns An iterator past the least recently used entry.
  iterator end() [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return iterator(lru_.end());
  }
  /// Returns a const iterator to the most recently used entry.
  ///
  /// \returns A const iterator to the most recently used entry.
  const_iterator begin() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return const_iterator(lru_.begin());
  }
  /// Returns a const iterator past the least recently used entry.
  ///
  /// \returns A const iterator past the least recently used entry.
  const_iterator end() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return const_iterator(lru_.end());
  }

  /// Returns a const iterator to the most recently used entry.
  ///
  /// \returns A const iterator to the most recently used entry.
  const_iterator cbegin() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return const_iterator(lru_.cbegin());
  }
  /// Returns a const iterator past the least recently used entry.
  ///
  /// \returns A const iterator past the least recently used entry.
  const_iterator cend() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return const_iterator(lru_.cend());
  }

  /// Returns a reverse iterator to the least recently used entry.
  ///
  /// \returns A reverse iterator to the least recently used entry.
  reverse_iterator rbegin() [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return reverse_iterator(lru_.rbegin());
  }
  /// Returns a reverse iterator past the most recently used entry.
  ///
  /// \returns A reverse iterator past the most recently used entry.
  reverse_iterator rend() [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return reverse_iterator(lru_.rend());
  }

  /// Returns a const reverse iterator to the least recently used entry.
  ///
  /// \returns A const reverse iterator to the least recently used entry.
  const_reverse_iterator rbegin() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return const_reverse_iterator(lru_.rbegin());
  }
  /// Returns a const reverse iterator past the most recently used entry.
  ///
  /// \returns A const reverse iterator past the most recently used entry.
  const_reverse_iterator rend() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return const_reverse_iterator(lru_.rend());
  }

  /// Returns a const reverse iterator to the least recently used entry.
  ///
  /// \returns A const reverse iterator to the least recently used entry.
  const_reverse_iterator crbegin() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return const_reverse_iterator(lru_.crbegin());
  }
  /// Returns a const reverse iterator past the most recently used entry.
  ///
  /// \returns A const reverse iterator past the most recently used entry.
  const_reverse_iterator crend() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return const_reverse_iterator(lru_.crend());
  }

 private:
  struct Node
      : public boost::intrusive::list_base_hook<
            boost::intrusive::link_mode<boost::intrusive::safe_link>> {
    template <typename K>
    Node(const K& key, TValue&& value) : pr(key, std::move(value)) {}

    template <typename Key, typename... Args>
    explicit Node(std::piecewise_construct_t, Key&& k, Args&&... args)
        : pr(std::piecewise_construct,
             std::forward_as_tuple(std::forward<Key>(k)),
             std::forward_as_tuple(std::forward<Args>(args)...)) {}
    TPair pr;
  };
  using NodePtr = Node*;

  // NOTE: deriving from boost::intrusive::list is likely discouraged. This is
  // simply an alternative to an ugly explicit move operator for
  // EvictingCacheMap. Change to that if this derivation proves problematic.
  struct NodeList : public boost::intrusive::list<Node> {
    NodeList() {}
    NodeList& operator=(NodeList&& that) noexcept {
      // Clear the moved-from rather than swap, for consistency with NodeMap
      clear_nodes();
      // Now invoke base class move operator without using static_cast
      boost::intrusive::list<Node>& this_parent = *this;
      boost::intrusive::list<Node>&& that_parent = std::move(that);
      this_parent = std::move(that_parent);
      return *this;
    }
    NodeList(NodeList&& that) noexcept { *this = std::move(that); }
    ~NodeList() {
      // Adds leak-free final destruction to the intrusive container
      clear_nodes();
    }

   private:
    void clear_nodes() {
      boost::intrusive::list<Node>::clear_and_dispose([](Node* ptr) {
        delete ptr;
      });
    }
  };

  struct KeyHasher : THash {
    static_assert(std::is_nothrow_copy_constructible_v<THash>);
    template <typename K>
    static inline constexpr bool nx =
        is_nothrow_invocable_v<THash const&, K const&>;

    using is_transparent = void;
    using folly_is_avalanching = IsAvalanchingHasher<THash, TKey>;

    using THash::THash;

    explicit KeyHasher(THash const& that) noexcept : THash(that) {}

    template <typename K>
    std::size_t operator()(const K& key) const noexcept(nx<K>) {
      return THash::operator()(key);
    }
    std::size_t operator()(const NodePtr& node) const noexcept(nx<TKey>) {
      return THash::operator()(node->pr.first);
    }
  };

  struct KeyValueEqual : private TKeyEqual {
    static_assert(std::is_nothrow_copy_constructible_v<TKeyEqual>);
    template <typename L, typename R>
    static inline constexpr bool nx =
        is_nothrow_invocable_v<TKeyEqual const&, L const&, R const&>;

    using is_transparent = void;

    using TKeyEqual::TKeyEqual;

    explicit KeyValueEqual(TKeyEqual const& that) noexcept : TKeyEqual(that) {}

    template <typename K>
    bool operator()(const K& lhs, const NodePtr& rhs) const
        noexcept(nx<K, TKey>) {
      return TKeyEqual::operator()(lhs, rhs->pr.first);
    }
    template <typename K>
    bool operator()(const NodePtr& lhs, const K& rhs) const
        noexcept(nx<TKey, K>) {
      return TKeyEqual::operator()(lhs->pr.first, rhs);
    }
    bool operator()(const NodePtr& lhs, const NodePtr& rhs) const
        noexcept(nx<TKey, TKey>) {
      return TKeyEqual::operator()(lhs->pr.first, rhs->pr.first);
    }
  };

  template <typename K>
  bool existsImpl(const K& key) const {
    return findInIndex(key) != nullptr;
  }

  template <typename K>
  TValue& getImpl(const K& key) {
    auto it = findImpl(*this, key);
    if (it == end()) {
      throw_exception<std::out_of_range>("Key does not exist");
    }
    return it->second;
  }

  template <typename Self>
  using self_iterator_t =
      std::conditional_t<std::is_const<Self>::value, const_iterator, iterator>;

  template <typename Self, typename K>
  static auto findImpl(Self& self, const K& key) {
    Node* ptr = self.findInIndex(key);
    if (!ptr) {
      return self.end();
    }
    self.lru_.splice(self.lru_.begin(), self.lru_, self.lru_.iterator_to(*ptr));
    return self_iterator_t<Self>(self.lru_.iterator_to(*ptr));
  }

  template <typename Self, typename K>
  static auto& getWithoutPromotionImpl(Self& self, const K& key) {
    auto it = self.findWithoutPromotion(key);
    if (it == self.end()) {
      throw_exception<std::out_of_range>("Key does not exist");
    }
    return it->second;
  }

  template <typename Self, typename K>
  static auto findWithoutPromotionImpl(Self& self, const K& key) {
    Node* ptr = self.findInIndex(key);
    return ptr
        ? self_iterator_t<Self>(self.lru_.iterator_to(*ptr))
        : self.end();
  }

  typename NodeList::iterator eraseImpl(
      Node* ptr,
      typename NodeList::const_iterator base_iter,
      PruneHookCall eraseHook) {
    std::unique_ptr<Node> node_owner(ptr);
    index_.erase(ptr);
    auto next_base_iter = lru_.erase(base_iter);
    if (eraseHook) {
      // NOTE: might throw, so we are in an exception-safe state
      eraseHook(ptr->pr.first, std::move(ptr->pr.second));
    }
    return next_base_iter;
  }

  template <typename K>
  bool eraseKeyImpl(const K& key, PruneHookCall eraseHook) {
    Node* ptr = findInIndex(key);
    if (ptr) {
      eraseImpl(ptr, lru_.iterator_to(*ptr), eraseHook);
      return true;
    }
    return false;
  }

  template <typename K>
  void setImpl(
      const K& key, TValue&& value, bool promote, PruneHookCall pruneHook) {
    Node* ptr = findInIndex(key);
    if (ptr) {
      ptr->pr.second = std::move(value);
      if (promote) {
        lru_.splice(lru_.begin(), lru_, lru_.iterator_to(*ptr));
      }
    } else {
      auto node = new Node(key, std::move(value));
      index_.insert(node);
      lru_.push_front(*node);

      // no evictions if maxSize_ is 0 i.e. unlimited capacity
      if (maxSize_ > 0 && size() > maxSize_) {
        prune(clearSize_, pruneHook);
      }
    }
  }

  template <typename K>
  auto insertImpl(const K& key, TValue&& value, PruneHookCall pruneHook) {
    auto node_owner = std::make_unique<Node>(key, std::move(value));
    return insertImpl<K>(std::move(node_owner), std::move(pruneHook));
  }

  template <typename K>
  auto insertImpl(std::unique_ptr<Node> nodeOwner, PruneHookCall pruneHook) {
    Node* node = nodeOwner.get();
    {
      auto pair = index_.insert(node);
      if (!pair.second) {
        // No change. Abandon/destroy new node.
        return std::pair<iterator, bool>(lru_.iterator_to(**pair.first), false);
      }

      // upcoming prune might invalidate iterator
      assert(*pair.first == node);
    }

    // Complete insertion
    lru_.push_front(*nodeOwner.release());

    // no evictions if maxSize_ is 0 i.e. unlimited capacity
    if (maxSize_ > 0 && size() > maxSize_) {
      prune(clearSize_, pruneHook);
    }

    return std::pair<iterator, bool>(lru_.iterator_to(*node), true);
  }

  template <typename K>
  Node* findInIndex(const K& key) const {
    auto it = index_.find(key);
    if (it != index_.end()) {
      return *it;
    } else {
      return nullptr;
    }
  }

  // A zero clear size doesn't make sense. If you want to disable clearing, set
  // maxSize to 0.
  static std::size_t clampClearSize(std::size_t clearSize) {
    return std::max(clearSize, std::size_t{1});
  }

  PruneHookCall pruneHook_;
  KeyHasher keyHash_;
  KeyValueEqual keyEqual_;
  NodeMap index_;
  NodeList lru_;
  std::size_t maxSize_;
  std::size_t clearSize_;
};

} // namespace folly
