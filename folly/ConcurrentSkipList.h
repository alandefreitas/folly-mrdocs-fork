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

// A concurrent skip list (CSL) implementation.
// Ref: http://www.cs.tau.ac.il/~shanir/nir-pubs-web/Papers/OPODIS2006-BA.pdf

/*

This implements a sorted associative container that supports only
unique keys.  (Similar to std::set.)

Features:

  1. Small memory overhead: ~40% less memory overhead compared with
     std::set (1.6 words per node versus 3). It has an minimum of 4
     words (7 words if there nodes got deleted) per-list overhead
     though.

  2. Read accesses (count, find iterator, skipper) are lock-free and
     mostly wait-free (the only wait a reader may need to do is when
     the node it is visiting is in a pending stage, i.e. deleting,
     adding and not fully linked).  Write accesses (remove, add) need
     to acquire locks, but locks are local to the predecessor nodes
     and/or successor nodes.

  3. Good high contention performance, comparable single-thread
     performance.  In the multithreaded case (12 workers), CSL tested
     10x faster than a RWSpinLocked std::set for an averaged sized
     list (1K - 1M nodes).

     Comparable read performance to std::set when single threaded,
     especially when the list size is large, and scales better to
     larger lists: when the size is small, CSL can be 20-50% slower on
     find()/contains().  As the size gets large (> 1M elements),
     find()/contains() can be 30% faster.

     Iterating through a skiplist is similar to iterating through a
     linked list, thus is much (2-6x) faster than on a std::set
     (tree-based).  This is especially true for short lists due to
     better cache locality.  Based on that, it's also faster to
     intersect two skiplists.

  4. Lazy removal with GC support.  The removed nodes get deleted when
     the last Accessor to the skiplist is destroyed.

Caveats:

  1. Write operations are usually 30% slower than std::set in a single
     threaded environment.

  2. Need to have a head node for each list, which has a 4 word
     overhead.

  3. When the list is quite small (< 1000 elements), single threaded
     benchmarks show CSL can be 10x slower than std:set.

  4. The interface requires using an Accessor to access the skiplist.
    (See below.)

  5. Currently x64 only, due to use of MicroSpinLock.

  6. Freed nodes will not be reclaimed as long as there are ongoing
     uses of the list.

Sample usage:

     typedef ConcurrentSkipList<int> SkipListT;
     shared_ptr<SkipListT> sl(SkipListT::createInstance(init_head_height);
     {
       // It's usually good practice to hold an accessor only during
       // its necessary life cycle (but not in a tight loop as
       // Accessor creation incurs ref-counting overhead).
       //
       // Holding it longer delays garbage-collecting the deleted
       // nodes in the list.
       SkipListT::Accessor accessor(sl);
       accessor.insert(23);
       accessor.erase(2);
       for (auto &elem : accessor) {
         // use elem to access data
       }
       ... ...
     }

 Another useful type is the Skipper accessor.  This is useful if you
 want to skip to locations in the way std::lower_bound() works,
 i.e. it can be used for going through the list by skipping to the
 node no less than a specified key.  The Skipper keeps its location as
 state, which makes it convenient for things like implementing
 intersection of two sets efficiently, as it can start from the last
 visited position.

     {
       SkipListT::Accessor accessor(sl);
       SkipListT::Skipper skipper(accessor);
       skipper.to(30);
       if (skipper) {
         CHECK_LE(30, *skipper);
       }
       ...  ...
       // GC may happen when the accessor gets destructed.
     }
*/

#pragma once

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <type_traits>

#include <glog/logging.h>

#include <folly/ConcurrentSkipList-inl.h>
#include <folly/Likely.h>
#include <folly/Memory.h>
#include <folly/detail/Iterators.h>
#include <folly/memory/Allocator.h>
#include <folly/synchronization/MicroSpinLock.h>

namespace folly {

/// A concurrent, sorted, unique-key associative container.
///
/// This is a lock-free-read skip list that stores unique keys in sorted
/// order, similar to `std::set`. Reads (find, count, iteration) are
/// lock-free; writes (add, remove) take locks local to the affected nodes.
/// Removed nodes are reclaimed lazily once the last Accessor is destroyed,
/// so all access goes through an Accessor or Skipper.
///
/// \tparam T The element (key) type stored in the list.
/// \tparam Comp The comparison functor used to order elements.
/// \tparam NodeAlloc The allocator used for nodes; it must be thread-safe.
/// \tparam MAX_HEIGHT The maximum number of skip-list levels, in [2, 64).
template <
    typename T,
    typename Comp = std::less<T>,
    // All nodes are allocated using provided SysAllocator,
    // it should be thread-safe.
    typename NodeAlloc = SysAllocator<char>,
    int MAX_HEIGHT = 24>
class ConcurrentSkipList {
  // MAX_HEIGHT needs to be at least 2 to suppress compiler
  // warnings/errors (Werror=uninitialized triggered due to preds_[1]
  // being treated as a scalar in the compiler).
  static_assert(
      MAX_HEIGHT >= 2 && MAX_HEIGHT < 64,
      "MAX_HEIGHT can only be in the range of [2, 64)");
  using ScopedLocker = std::unique_lock<folly::MicroSpinLock>;
  using SkipListType = ConcurrentSkipList<T, Comp, NodeAlloc, MAX_HEIGHT>;

 public:
  /// The internal node type holding a single element.
  using NodeType = detail::SkipListNode<T>;
  /// The element type stored in the list.
  using value_type = T;
  /// The key type used to order and look up elements.
  using key_type = T;

  /// Forward iterator over the elements of the list.
  using iterator = detail::csl_iterator<value_type, NodeType>;
  /// Read-only forward iterator over the elements of the list.
  using const_iterator = detail::csl_iterator<const value_type, NodeType>;

  class Accessor;
  class Skipper;

  /// Constructs a skip list whose head node has the given height.
  ///
  /// \param height The initial height of the head node.
  /// \param alloc The allocator used for nodes.
  explicit ConcurrentSkipList(int height, const NodeAlloc& alloc)
      : recycler_(alloc),
        head_(NodeType::create(recycler_.alloc(), height, value_type(), true)) {
  }

  /// Constructs a skip list whose head node has the given height.
  ///
  /// \param height The initial height of the head node.
  explicit ConcurrentSkipList(int height)
      : recycler_(),
        head_(NodeType::create(recycler_.alloc(), height, value_type(), true)) {
  }

  /// Creates a new instance and returns an Accessor to it.
  ///
  /// \param height The initial head height of the new list.
  /// \param alloc The allocator used for nodes.
  /// \returns An Accessor holding the newly created list.
  static Accessor create(int height, const NodeAlloc& alloc) {
    return Accessor(createInstance(height, alloc));
  }

  /// Creates a new instance and returns an Accessor to it.
  ///
  /// \param height The initial head height of the new list.
  /// \returns An Accessor holding the newly created list.
  static Accessor create(int height = 1) {
    return Accessor(createInstance(height));
  }

  /// Creates a shared skip list with the given initial head height.
  ///
  /// \param height The initial head height of the new list.
  /// \param alloc The allocator used for nodes.
  /// \returns A shared pointer owning the newly created list.
  static std::shared_ptr<SkipListType> createInstance(
      int height, const NodeAlloc& alloc) {
    return std::make_shared<ConcurrentSkipList>(height, alloc);
  }

  /// Creates a shared skip list with the given initial head height.
  ///
  /// \param height The initial head height of the new list.
  /// \returns A shared pointer owning the newly created list.
  static std::shared_ptr<SkipListType> createInstance(int height = 1) {
    return std::make_shared<ConcurrentSkipList>(height);
  }

  /// Returns the number of elements in the list.
  ///
  /// \returns The current element count.
  size_t size() const { return size_.load(std::memory_order_relaxed); }
  /// Returns whether the list has no elements.
  ///
  /// \returns `true` if the list is empty.
  bool empty() const { return size() == 0; }

  //===================================================================
  // Below are implementation details.
  // Please see ConcurrentSkipList::Accessor for stdlib-like APIs.
  //===================================================================

  /// Destroys the list and its nodes.
  ~ConcurrentSkipList() {
    if constexpr (NodeType::template DestroyIsNoOp<NodeAlloc>::value) {
      // Avoid traversing the list if using arena allocator.
      return;
    }
    for (NodeType* current = head_.load(std::memory_order_relaxed); current;) {
      NodeType* tmp = current->skip(0);
      NodeType::destroy(recycler_.alloc(), current);
      current = tmp;
    }
  }

 private:
  static bool greater(const value_type& data, const NodeType* node) {
    return node && Comp()(node->data(), data);
  }

  static bool less(const value_type& data, const NodeType* node) {
    return (node == nullptr) || Comp()(data, node->data());
  }

  static int findInsertionPoint(
      NodeType* cur,
      int cur_layer,
      const value_type& data,
      NodeType* preds[],
      NodeType* succs[]) {
    int foundLayer = -1;
    NodeType* pred = cur;
    NodeType* foundNode = nullptr;
    for (int layer = cur_layer; layer >= 0; --layer) {
      NodeType* node = pred->skip(layer);
      while (greater(data, node)) {
        pred = node;
        node = node->skip(layer);
      }
      if (foundLayer == -1 && !less(data, node)) { // the two keys equal
        foundLayer = layer;
        foundNode = node;
      }
      preds[layer] = pred;

      // if found, succs[0..foundLayer] need to point to the cached foundNode,
      // as foundNode might be deleted at the same time thus pred->skip() can
      // return nullptr or another node.
      succs[layer] = foundNode ? foundNode : node;
    }
    return foundLayer;
  }

  int height() const { return head_.load(std::memory_order_acquire)->height(); }

  int maxLayer() const { return height() - 1; }

  size_t incrementSize(int delta) {
    return size_.fetch_add(delta, std::memory_order_relaxed) + delta;
  }

  // Returns the node if found, nullptr otherwise.
  NodeType* find(const value_type& data) {
    auto ret = findNode(data);
    if (ret.second && !ret.first->markedForRemoval()) {
      return ret.first;
    }
    return nullptr;
  }

  // lock all the necessary nodes for changing (adding or removing) the list.
  // returns true if all the lock acquired successfully and the related nodes
  // are all validate (not in certain pending states), false otherwise.
  bool lockNodesForChange(
      int nodeHeight,
      ScopedLocker guards[MAX_HEIGHT],
      NodeType* preds[MAX_HEIGHT],
      NodeType* succs[MAX_HEIGHT],
      bool adding = true) {
    NodeType *pred, *succ, *prevPred = nullptr;
    bool valid = true;
    for (int layer = 0; valid && layer < nodeHeight; ++layer) {
      pred = preds[layer];
      DCHECK(pred != nullptr)
          << "layer=" << layer << " height=" << height()
          << " nodeheight=" << nodeHeight;
      succ = succs[layer];
      if (pred != prevPred) {
        guards[layer] = pred->acquireGuard();
        prevPred = pred;
      }
      valid = !pred->markedForRemoval() &&
          pred->skip(layer) == succ; // check again after locking

      if (adding) { // when adding a node, the succ shouldn't be going away
        valid = valid && (succ == nullptr || !succ->markedForRemoval());
      }
    }

    return valid;
  }

  // Returns a paired value:
  //   pair.first always stores the pointer to the node with the same input key.
  //     It could be either the newly added data, or the existed data in the
  //     list with the same key.
  //   pair.second stores whether the data is added successfully:
  //     0 means not added, otherwise returns the new size.
  template <typename U>
  std::pair<NodeType*, size_t> addOrGetData(U&& data) {
    NodeType *preds[MAX_HEIGHT], *succs[MAX_HEIGHT];
    NodeType* newNode;
    size_t newSize;
    while (true) {
      int max_layer = 0;
      int layer = findInsertionPointGetMaxLayer(data, preds, succs, &max_layer);

      if (layer >= 0) {
        NodeType* nodeFound = succs[layer];
        DCHECK(nodeFound != nullptr);
        if (nodeFound->markedForRemoval()) {
          continue; // if it's getting deleted retry finding node.
        }
        // wait until fully linked.
        while (FOLLY_UNLIKELY(!nodeFound->fullyLinked())) {
        }
        return std::make_pair(nodeFound, 0);
      }

      // need to capped at the original height -- the real height may have grown
      int nodeHeight =
          detail::SkipListRandomHeight::instance()->getHeight(max_layer + 1);

      ScopedLocker guards[MAX_HEIGHT];
      if (!lockNodesForChange(nodeHeight, guards, preds, succs)) {
        continue; // give up the locks and retry until all valid
      }

      // locks acquired and all valid, need to modify the links under the locks.
      newNode = NodeType::create(
          recycler_.alloc(), nodeHeight, std::forward<U>(data));
      for (int k = 0; k < nodeHeight; ++k) {
        newNode->setSkip(k, succs[k]);
        preds[k]->setSkip(k, newNode);
      }

      newNode->setFullyLinked();
      newSize = incrementSize(1);
      break;
    }

    int hgt = height();
    size_t sizeLimit =
        detail::SkipListRandomHeight::instance()->getSizeLimit(hgt);

    if (hgt < MAX_HEIGHT && newSize > sizeLimit) {
      growHeight(hgt + 1);
    }
    CHECK_GT(newSize, 0);
    return std::make_pair(newNode, newSize);
  }

  bool remove(const value_type& data) {
    NodeType* nodeToDelete = nullptr;
    ScopedLocker nodeGuard;
    bool isMarked = false;
    int nodeHeight = 0;
    NodeType *preds[MAX_HEIGHT], *succs[MAX_HEIGHT];

    while (true) {
      int max_layer = 0;
      int layer = findInsertionPointGetMaxLayer(data, preds, succs, &max_layer);
      if (!isMarked && (layer < 0 || !okToDelete(succs[layer], layer))) {
        return false;
      }

      if (!isMarked) {
        nodeToDelete = succs[layer];
        nodeHeight = nodeToDelete->height();
        nodeGuard = nodeToDelete->acquireGuard();
        if (nodeToDelete->markedForRemoval()) {
          return false;
        }
        nodeToDelete->setMarkedForRemoval();
        isMarked = true;
      }

      // acquire pred locks from bottom layer up
      ScopedLocker guards[MAX_HEIGHT];
      if (!lockNodesForChange(nodeHeight, guards, preds, succs, false)) {
        continue; // this will unlock all the locks
      }

      for (int k = nodeHeight - 1; k >= 0; --k) {
        preds[k]->setSkip(k, nodeToDelete->skip(k));
      }

      incrementSize(-1);
      break;
    }
    recycle(nodeToDelete);
    return true;
  }

  const value_type* first() const {
    auto node = head_.load(std::memory_order_acquire)->skip(0);
    return node ? &node->data() : nullptr;
  }

  const value_type* last() const {
    NodeType* pred = head_.load(std::memory_order_acquire);
    NodeType* node = nullptr;
    for (int layer = maxLayer(); layer >= 0; --layer) {
      do {
        node = pred->skip(layer);
        if (node) {
          pred = node;
        }
      } while (node != nullptr);
    }
    return pred == head_.load(std::memory_order_relaxed)
        ? nullptr
        : &pred->data();
  }

  static bool okToDelete(NodeType* candidate, int layer) {
    DCHECK(candidate != nullptr);
    return candidate->fullyLinked() && candidate->maxLayer() == layer &&
        !candidate->markedForRemoval();
  }

  // find node for insertion/deleting
  int findInsertionPointGetMaxLayer(
      const value_type& data,
      NodeType* preds[],
      NodeType* succs[],
      int* max_layer) const {
    *max_layer = maxLayer();
    return findInsertionPoint(
        head_.load(std::memory_order_acquire), *max_layer, data, preds, succs);
  }

  // Find node for access. Returns a paired values:
  // pair.first = the first node that no-less than data value
  // pair.second = 1 when the data value is founded, or 0 otherwise.
  // This is like lower_bound, but not exact: we could have the node marked for
  // removal so still need to check that.
  std::pair<NodeType*, int> findNode(const value_type& data) const {
    return findNodeDownRight(data);
  }

  // Find node by first stepping down then stepping right. Based on benchmark
  // results, this is slightly faster than findNodeRightDown for better
  // locality on the skipping pointers.
  std::pair<NodeType*, int> findNodeDownRight(const value_type& data) const {
    NodeType* pred = head_.load(std::memory_order_acquire);
    int ht = pred->height();
    NodeType* node = nullptr;

    bool found = false;
    while (!found) {
      // stepping down
      for (; ht > 0 && less(data, node = pred->skip(ht - 1)); --ht) {
      }
      if (ht == 0) {
        return std::make_pair(node, 0); // not found
      }
      // node <= data now, but we need to fix up ht
      --ht;

      // stepping right
      while (greater(data, node)) {
        pred = node;
        node = node->skip(ht);
      }
      found = !less(data, node);
    }
    return std::make_pair(node, found);
  }

  // find node by first stepping right then stepping down.
  // We still keep this for reference purposes.
  std::pair<NodeType*, int> findNodeRightDown(const value_type& data) const {
    NodeType* pred = head_.load(std::memory_order_acquire);
    NodeType* node = nullptr;
    auto top = maxLayer();
    int found = 0;
    for (int layer = top; !found && layer >= 0; --layer) {
      node = pred->skip(layer);
      while (greater(data, node)) {
        pred = node;
        node = node->skip(layer);
      }
      found = !less(data, node);
    }
    return std::make_pair(node, found);
  }

  NodeType* lower_bound(const value_type& data) const {
    auto node = findNode(data).first;
    while (node != nullptr && node->markedForRemoval()) {
      node = node->skip(0);
    }
    return node;
  }

  void growHeight(int height) {
    NodeType* oldHead = head_.load(std::memory_order_acquire);
    if (oldHead->height() >= height) { // someone else already did this
      return;
    }

    NodeType* newHead =
        NodeType::create(recycler_.alloc(), height, value_type(), true);

    { // need to guard the head node in case others are adding/removing
      // nodes linked to the head.
      ScopedLocker g = oldHead->acquireGuard();
      newHead->copyHead(oldHead);
      NodeType* expected = oldHead;
      if (!head_.compare_exchange_strong(
              expected, newHead, std::memory_order_release)) {
        // if someone has already done the swap, just return.
        NodeType::destroy(recycler_.alloc(), newHead);
        return;
      }
      oldHead->setMarkedForRemoval();
    }
    recycle(oldHead);
  }

  void recycle(NodeType* node) { recycler_.add(node); }

  detail::NodeRecycler<NodeType, NodeAlloc> recycler_;
  std::atomic<NodeType*> head_;
  std::atomic<size_t> size_{0};
};

/// Provides stdlib-like access to a ConcurrentSkipList.
///
/// An Accessor keeps the underlying list alive and holds a reference that
/// defers garbage collection of removed nodes. All element access, lookup,
/// insertion, and removal go through an Accessor.
template <typename T, typename Comp, typename NodeAlloc, int MAX_HEIGHT>
class ConcurrentSkipList<T, Comp, NodeAlloc, MAX_HEIGHT>::Accessor {
  using NodeType = detail::SkipListNode<T>;
  using SkipListType = ConcurrentSkipList<T, Comp, NodeAlloc, MAX_HEIGHT>;

 public:
  /// The element type stored in the list.
  using value_type = T;
  /// The key type used to order and look up elements.
  using key_type = T;
  /// Reference to an element.
  using reference = T&;
  /// Pointer to an element.
  using pointer = T*;
  /// Reference to a const element.
  using const_reference = const T&;
  /// Pointer to a const element.
  using const_pointer = const T*;
  /// Unsigned integer type used for sizes.
  using size_type = size_t;
  /// The comparison functor type used to order keys.
  using key_compare = Comp;
  /// The comparison functor type used to order elements.
  using value_compare = Comp;

  /// Forward iterator over the elements of the list.
  using iterator = typename SkipListType::iterator;
  /// Read-only forward iterator over the elements of the list.
  using const_iterator = typename SkipListType::const_iterator;
  /// Skipper that traverses the list by jumping to lower-bound positions.
  using Skipper = typename SkipListType::Skipper;

  /// Constructs an Accessor that shares ownership of the list.
  ///
  /// \param skip_list The shared list to access.
  explicit Accessor(std::shared_ptr<ConcurrentSkipList> skip_list)
      : slHolder_(std::move(skip_list)) {
    sl_ = slHolder_.get();
    DCHECK(sl_ != nullptr);
    sl_->recycler_.addRef();
  }

  /// Constructs an Accessor over a non-owned list.
  ///
  /// The caller assumes the responsibility to keep `skip_list` valid during
  /// the whole life cycle of the Accessor.
  ///
  /// \param skip_list The list to access; must outlive the Accessor.
  explicit Accessor(ConcurrentSkipList* skip_list) : sl_(skip_list) {
    DCHECK(sl_ != nullptr);
    sl_->recycler_.addRef();
  }

  /// Constructs a copy that references the same list.
  ///
  /// \param accessor The Accessor to copy.
  Accessor(const Accessor& accessor)
      : sl_(accessor.sl_), slHolder_(accessor.slHolder_) {
    sl_->recycler_.addRef();
  }

  /// Assigns from another Accessor, referencing the same list.
  ///
  /// \param accessor The Accessor to copy from.
  /// \returns A reference to this Accessor.
  Accessor& operator=(const Accessor& accessor) {
    if (this != &accessor) {
      slHolder_ = accessor.slHolder_;
      sl_->recycler_.releaseRef();
      sl_ = accessor.sl_;
      sl_->recycler_.addRef();
    }
    return *this;
  }

  /// Destroys the Accessor and releases its reference to the list.
  ~Accessor() { sl_->recycler_.releaseRef(); }

  /// Returns whether the list has no elements.
  ///
  /// \returns `true` if the list is empty.
  bool empty() const { return sl_->size() == 0; }
  /// Returns the number of elements in the list.
  ///
  /// \returns The current element count.
  size_t size() const { return sl_->size(); }
  /// Returns the largest number of elements the list can hold.
  ///
  /// \returns The maximum possible element count.
  size_type max_size() const { return std::numeric_limits<size_type>::max(); }

  /// Finds the element equal to the given key.
  ///
  /// The returned data stays valid as long as the Accessor is held.
  ///
  /// \param value The key to look up.
  /// \returns An iterator to the element, or end() if not found.
  iterator find(const key_type& value) { return iterator(sl_->find(value)); }
  /// Finds the element equal to the given key.
  ///
  /// \param value The key to look up.
  /// \returns A const iterator to the element, or end() if not found.
  const_iterator find(const key_type& value) const {
    return iterator(sl_->find(value));
  }
  /// Counts the elements equal to the given key.
  ///
  /// \param data The key to count.
  /// \returns 1 if the key is present, 0 otherwise.
  size_type count(const key_type& data) const { return contains(data); }

  /// Returns an iterator to the first element.
  ///
  /// \returns An iterator to the beginning of the list.
  iterator begin() const {
    NodeType* head = sl_->head_.load(std::memory_order_acquire);
    return iterator(head->next());
  }
  /// Returns an iterator past the last element.
  ///
  /// \returns An iterator to the end of the list.
  iterator end() const { return iterator(nullptr); }
  /// Returns a const iterator to the first element.
  ///
  /// \returns A const iterator to the beginning of the list.
  const_iterator cbegin() const { return begin(); }
  /// Returns a const iterator past the last element.
  ///
  /// \returns A const iterator to the end of the list.
  const_iterator cend() const { return end(); }

  /// Inserts an element if its key is not already present.
  ///
  /// \tparam U The type of the value being inserted, convertible to T.
  /// \param data The value to insert.
  /// \returns A pair of an iterator to the element and a bool that is `true`
  ///     if a new element was inserted.
  template <
      typename U,
      typename =
          typename std::enable_if<std::is_convertible<U, T>::value>::type>
  std::pair<iterator, bool> insert(U&& data) {
    auto ret = sl_->addOrGetData(std::forward<U>(data));
    return std::make_pair(iterator(ret.first), ret.second);
  }
  /// Removes the element with the given key.
  ///
  /// \param data The key to erase.
  /// \returns The number of elements removed (0 or 1).
  size_t erase(const key_type& data) { return remove(data); }

  /// Finds the first element not ordered before the given key.
  ///
  /// \param data The key to bound.
  /// \returns An iterator to the lower bound, or end() if none.
  iterator lower_bound(const key_type& data) const {
    return iterator(sl_->lower_bound(data));
  }

  /// Returns the current height of the list.
  ///
  /// \returns The number of skip-list levels currently in use.
  size_t height() const { return sl_->height(); }

  /// Returns a pointer to the first element, or nullptr if empty.
  ///
  /// Because concurrent writes can happen, this is not guaranteed to be the
  /// minimum element and may be invalid, so it is named differently from
  /// front().
  ///
  /// \returns A pointer to the first element, or nullptr.
  const key_type* first() const { return sl_->first(); }
  /// Returns a pointer to the last element, or nullptr if empty.
  ///
  /// Because concurrent writes can happen, this is not guaranteed to be the
  /// maximum element and may be invalid, so it is named differently from
  /// tail().
  ///
  /// \returns A pointer to the last element, or nullptr.
  const key_type* last() const { return sl_->last(); }

  /// Tries to remove the last element in the list.
  ///
  /// \returns `true` if an element was removed, `false` if the list is empty
  ///     or a race caused the last element to already be removed.
  bool pop_back() {
    auto last = sl_->last();
    return last ? sl_->remove(*last) : false;
  }

  /// Inserts the key if absent and returns a pointer to the stored data.
  ///
  /// \param data The key to add or look up.
  /// \returns A pair of a pointer to the stored data and a bool that is
  ///     `true` if a new element was inserted.
  std::pair<key_type*, bool> addOrGetData(const key_type& data) {
    auto ret = sl_->addOrGetData(data);
    return std::make_pair(&ret.first->data(), ret.second);
  }

  /// Returns the underlying skip list.
  ///
  /// \returns A pointer to the list this Accessor references.
  SkipListType* skiplist() const { return sl_; }

  // legacy interfaces
  // TODO:(xliu) remove these.
  /// Returns whether the key is present in the list.
  ///
  /// \param data The key to look for.
  /// \returns `true` if the key is present.
  bool contains(const key_type& data) const { return sl_->find(data); }
  /// Adds the key if it is not already present.
  ///
  /// \param data The key to add.
  /// \returns `true` if the key was newly added.
  bool add(const key_type& data) { return sl_->addOrGetData(data).second; }
  /// Removes the key from the list.
  ///
  /// \param data The key to remove.
  /// \returns `true` if the key was present and removed.
  bool remove(const key_type& data) { return sl_->remove(data); }

 private:
  SkipListType* sl_;
  std::shared_ptr<SkipListType> slHolder_;
};

// implements forward iterator concept.
template <typename ValT, typename NodeT>
class detail::csl_iterator
    : public detail::IteratorFacade<
          csl_iterator<ValT, NodeT>,
          ValT,
          std::forward_iterator_tag> {
 public:
  using value_type = ValT;
  using reference = value_type&;
  using pointer = value_type*;
  using difference_type = ptrdiff_t;

  explicit csl_iterator(NodeT* node = nullptr) : node_(node) {}

  template <typename OtherVal, typename OtherNode>
  csl_iterator(
      const csl_iterator<OtherVal, OtherNode>& other,
      typename std::enable_if<
          std::is_convertible<OtherVal*, ValT*>::value>::type* = nullptr)
      : node_(other.node_) {}

  size_t nodeSize() const {
    return node_ == nullptr
        ? 0
        : node_->height() * sizeof(NodeT*) + sizeof(*this);
  }

  bool good() const { return node_ != nullptr; }

 private:
  template <class, class>
  friend class csl_iterator;
  friend class detail::
      IteratorFacade<csl_iterator, ValT, std::forward_iterator_tag>;

  void increment() { node_ = node_->next(); }
  bool equal(const csl_iterator& other) const { return node_ == other.node_; }
  value_type& dereference() const { return node_->data(); }

  NodeT* node_;
};

/// Traverses a ConcurrentSkipList by skipping to lower-bound positions.
///
/// A Skipper keeps its current location as state, so it can advance forward
/// or jump to the node no less than a given key starting from where it last
/// stopped. This makes it convenient for operations like set intersection.
template <typename T, typename Comp, typename NodeAlloc, int MAX_HEIGHT>
class ConcurrentSkipList<T, Comp, NodeAlloc, MAX_HEIGHT>::Skipper {
  using NodeType = detail::SkipListNode<T>;
  using SkipListType = ConcurrentSkipList<T, Comp, NodeAlloc, MAX_HEIGHT>;
  using Accessor = typename SkipListType::Accessor;

 public:
  /// The element type stored in the list.
  using value_type = T;
  /// Reference to an element.
  using reference = T&;
  /// Pointer to an element.
  using pointer = T*;
  /// Signed integer type used for iterator differences.
  using difference_type = ptrdiff_t;

  /// Constructs a Skipper over the given shared list.
  ///
  /// \param skipList The shared list to traverse.
  Skipper(std::shared_ptr<SkipListType> skipList)
      : accessor_(std::move(skipList)) {
    init();
  }

  /// Constructs a Skipper that reuses an existing Accessor.
  ///
  /// \param accessor The Accessor whose list is traversed.
  Skipper(const Accessor& accessor) : accessor_(accessor) { init(); }

  /// Initializes the Skipper state at the head of the list.
  void init() {
    // need to cache the head node
    NodeType* head_node = head();
    headHeight_ = head_node->height();
    for (int i = 0; i < headHeight_; ++i) {
      preds_[i] = head_node;
      succs_[i] = head_node->skip(i);
    }
    int max_layer = maxLayer();
    for (int i = 0; i < max_layer; ++i) {
      hints_[i] = uint8_t(i + 1);
    }
    hints_[max_layer] = max_layer;
  }

  /// Advances to the next node in the list.
  ///
  /// \returns A reference to this Skipper.
  Skipper& operator++() {
    preds_[0] = succs_[0];
    succs_[0] = preds_[0]->skip(0);
    int height = curHeight();
    for (int i = 1; i < height && preds_[0] == succs_[i]; ++i) {
      preds_[i] = succs_[i];
      succs_[i] = preds_[i]->skip(i);
    }
    return *this;
  }

  /// Returns the Accessor backing this Skipper.
  ///
  /// \returns A reference to the backing Accessor.
  Accessor& accessor() { return accessor_; }
  /// Returns the Accessor backing this Skipper.
  ///
  /// \returns A const reference to the backing Accessor.
  const Accessor& accessor() const { return accessor_; }

  /// Returns whether the Skipper currently points at a valid element.
  ///
  /// \returns `true` if positioned on an element.
  bool good() const { return succs_[0] != nullptr; }

  /// Returns the index of the highest level in the list.
  ///
  /// \returns The maximum layer index (head height minus one).
  int maxLayer() const { return headHeight_ - 1; }

  /// Returns the effective height at the current position.
  ///
  /// \returns The current node height, capped at the cached head height.
  int curHeight() const {
    // need to cap the height to the cached head height, as the current node
    // might be some newly inserted node and also during the time period the
    // head height may have grown.
    return succs_[0] ? std::min(headHeight_, succs_[0]->height()) : 0;
  }

  /// Returns the element at the current position.
  ///
  /// \returns A const reference to the current element.
  const value_type& data() const {
    DCHECK(succs_[0] != nullptr);
    return succs_[0]->data();
  }

  /// Returns the element at the current position.
  ///
  /// \returns A reference to the current element.
  value_type& operator*() const {
    DCHECK(succs_[0] != nullptr);
    return succs_[0]->data();
  }

  /// Returns a pointer to the element at the current position.
  ///
  /// \returns A pointer to the current element.
  value_type* operator->() const {
    DCHECK(succs_[0] != nullptr);
    return &succs_[0]->data();
  }

  /// Skips to the first position whose data is no less than the argument.
  ///
  /// This is the lower_bound of the given key.
  ///
  /// \param data The key to skip to.
  /// \returns `true` if an equal element was found, `false` otherwise.
  bool to(const value_type& data) {
    int layer = curHeight() - 1;
    if (layer < 0) {
      return false; // reaches the end of the list
    }

    int lyr = hints_[layer];
    int max_layer = maxLayer();
    while (SkipListType::greater(data, succs_[lyr]) && lyr < max_layer) {
      ++lyr;
    }
    hints_[layer] = lyr; // update the hint

    int foundLayer = SkipListType::findInsertionPoint(
        preds_[lyr], lyr, data, preds_, succs_);
    if (foundLayer < 0) {
      return false;
    }

    DCHECK(succs_[0] != nullptr)
        << "lyr=" << lyr << "; max_layer=" << max_layer;
    return !succs_[0]->markedForRemoval();
  }

 private:
  NodeType* head() const {
    return accessor_.skiplist()->head_.load(std::memory_order_acquire);
  }

  Accessor accessor_;
  int headHeight_;
  NodeType *succs_[MAX_HEIGHT], *preds_[MAX_HEIGHT];
  uint8_t hints_[MAX_HEIGHT];
};

} // namespace folly
