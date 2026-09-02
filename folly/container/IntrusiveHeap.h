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
 * Skew heap [1] implementation using top-down meld.
 *
 * [1] D.D. Sleator, R.E. Tarjan, Self-Adjusting Heaps,
 * SIAM Journal of Computing, 15(1): 52-69, 1986.
 * http://www.cs.cmu.edu/~sleator/papers/adjusting-heaps.pdf
 */

#pragma once

#include <functional>
#include <utility>

#include <boost/intrusive/parent_from_member.hpp>
#include <boost/noncopyable.hpp>
#include <glog/logging.h>
#include <folly/Portability.h>
#include <folly/lang/Builtin.h>

namespace folly {

/**
 * Base class for items to be inserted into IntrusiveHeap<..., Tag> storing
 * pointers for internal use.
 */
template <class Tag = void>
class IntrusiveHeapNode : private boost::noncopyable {
 public:
  /// Returns true if this node is currently linked into a heap.
  ///
  /// \returns True if the node is part of a heap.
  bool isLinked() const { return parent_ != kUnlinked; }

 private:
  template <class, class, class, class>
  friend class IntrusiveHeap;
  /// Test fixture granted access to node internals.
  friend class IntrusiveHeapTest;

  static IntrusiveHeapNode* const kUnlinked;

  /**
   * If this is in a heap, (parent_ == nullptr) <=> (this == heap_.root_).
   * Otherwise, parent_ is kUnlinked.
   */
  IntrusiveHeapNode* parent_ = kUnlinked;

  /**
   * If this is in a heap, left_ and right_ point to subheaps or nullptr.
   * Otherwise, these are undefined.
   */
  IntrusiveHeapNode* left_ = nullptr;
  IntrusiveHeapNode* right_ = nullptr;
};

template <class Tag>
IntrusiveHeapNode<Tag>* const IntrusiveHeapNode<Tag>::kUnlinked =
    reinterpret_cast<IntrusiveHeapNode*>(1);

/// Node traits for types that derive from IntrusiveHeapNode.
template <class T, class Tag>
struct DerivedNodeTraits {
  /// Returns the node base subobject of a value.
  ///
  /// \param x The value to convert.
  /// \returns The node pointer for `x`.
  static IntrusiveHeapNode<Tag>* asNode(T* x) { return x; }
  /// Returns the value that owns a node.
  ///
  /// \param n The node to convert.
  /// \returns The value pointer for `n`.
  static T* asT(IntrusiveHeapNode<Tag>* n) { return static_cast<T*>(n); }
};

/// Node traits for types that hold an IntrusiveHeapNode member.
template <class T, class Tag, IntrusiveHeapNode<Tag> T::* PtrToMember>
struct MemberNodeTraits {
  /// Returns the node member of a value.
  ///
  /// \param x The value to convert.
  /// \returns The node pointer for `x`.
  static IntrusiveHeapNode<Tag>* asNode(T* x) { return &(x->*PtrToMember); }
  /// Returns the value that owns a node.
  ///
  /// \param n The node to convert.
  /// \returns The value pointer for `n`.
  static T* asT(IntrusiveHeapNode<Tag>* n) {
    return boost::intrusive::get_parent_from_member(n, PtrToMember);
  }
};

/**
 * IntrusiveHeap implements a skew heap with intrusive pointers to provide
 * O(log(n)) operations on any node in the heap with no separately allocated
 * node type.
 *
 * - To be inserted into an IntrusiveHeap<T, Compare, Tag>, T must inherit from
 *   IntrusiveHeapNode<Tag>, or have a member of type IntrusiveHeapNode<Tag> and
 *   use MemberNodeTraits.
 *
 * - An instance of T may only be included in one IntrusiveHeap for each Tag
 *   type. It may be included in more than one IntrusiveHeap by inheriting from
 *   IntrusiveHeapNode again with a different tag type, or by using composition
 *   with different members.
 */
template <
    class T,
    class Compare = std::less<>,
    class Tag = void,
    class NodeTraitsType = DerivedNodeTraits<T, Tag>>
class IntrusiveHeap {
 public:
  /// The intrusive node type stored by elements of this heap.
  using Node = IntrusiveHeapNode<Tag>;
  /// The traits used to convert between values and nodes.
  using NodeTraits = NodeTraitsType;
  /// The element type stored in the heap.
  using Value = T;

  /// Constructs an empty heap.
  IntrusiveHeap() {}

  /// Deleted copy constructor; the heap is move-only.
  ///
  /// \param other The heap that would be copied.
  IntrusiveHeap(const IntrusiveHeap& other) = delete;
  /// Deleted copy assignment; the heap is move-only.
  ///
  /// \param other The heap that would be copied.
  /// \returns A reference to this heap.
  IntrusiveHeap& operator=(const IntrusiveHeap& other) = delete;

  /// Move-constructs a heap, taking ownership of another heap's contents.
  ///
  /// \param other The heap to move from; left empty afterwards.
  IntrusiveHeap(IntrusiveHeap&& other) noexcept
      : root_(std::exchange(other.root_, nullptr)) {}
  /// Deleted move assignment; the heap is move-only for construction.
  ///
  /// \param other The heap that would be moved from.
  /// \returns A reference to this heap.
  IntrusiveHeap& operator=(IntrusiveHeap&& other) = delete;

  /**
   * Returns a pointer to the maximum value in the heap, or nullptr if the heap
   * is empty.
   *
   * \returns The maximum value, or nullptr if the heap is empty.
   */
  T* top() const { return root_ != nullptr ? asT(root_) : nullptr; }

  /// Returns true if the heap contains no values.
  ///
  /// \returns True if the heap is empty.
  bool empty() const { return root_ == nullptr; }

  /**
   * Removes the maximum value from the heap and returns it, or nullptr if the
   * heap is empty.
   *
   * \returns The removed maximum value, or nullptr if the heap was empty.
   */
  T* pop() {
    if (root_ == nullptr) {
      return nullptr;
    }
    Node* top = root_;
    merge(top->left_, top->right_, nullptr, &root_);
    top->parent_ = Node::kUnlinked;
    return asT(top);
  }

  /**
   * Visits all items in the heap.
   *
   * \param visitor A callable invoked with each item in the heap.
   */
  template <class Visitor>
  void visit(const Visitor& visitor) const {
    visit(visitor, root_);
  }

  /**
   * Updates the heap to reflect a change in a given value.
   *
   * \param x The value whose ordering may have changed.
   */
  void update(T* x) {
    erase(x);
    push(x);
  }

  /// Inserts a value into the heap.
  ///
  /// \param x The value to insert; must not already be linked.
  void push(T* x) {
    DCHECK(x);
    auto n = asNode(x);
    DCHECK(!n->isLinked());
    n->parent_ = nullptr;
    n->left_ = nullptr;
    n->right_ = nullptr;
    merge(n, root_, nullptr, &root_);
  }

  /// Removes a value from the heap.
  ///
  /// \param x The value to remove; must be present in the heap.
  void erase(T* x) {
    auto n = asNode(x);
    DCHECK(n->isLinked());
    DCHECK(contains(x));
    auto parent = n->parent_;
    Node** out;
    if (parent == nullptr) {
      out = &root_;
    } else if (parent->left_ == n) {
      out = &parent->left_;
    } else {
      DCHECK_EQ(parent->right_, n);
      out = &parent->right_;
    }

    merge(n->left_, n->right_, parent, out);
    n->parent_ = Node::kUnlinked;
  }

  /**
   * Check whether this node is included in this heap. Primarily meant for
   * assertions, as containment should be externally tracked.
   *
   * \param x The value to test for membership.
   * \returns True if `x` is included in this heap.
   */
  bool contains(const T* x) const {
    DCHECK(x);
    auto n = asNode(x);
    while (n->parent_) {
      n = n->parent_;
    }
    return n == root_;
  }

  /**
   * Moves the contents of other into *this.
   *
   * \param other The heap whose contents are merged in.
   */
  void merge(IntrusiveHeap other) {
    merge(root_, other.root_, nullptr, &root_);
  }

 private:
  friend class IntrusiveHeapTest;

  template <class Visitor>
  static void visit(const Visitor& visitor, Node* x) {
    for (; x != nullptr; x = x->right_) {
      visitor(asT(x));
      visit(visitor, x->left_);
    }
  }

  /**
   * Merges two subtrees, assigns a parent, populates *out with new subtree.
   */
  FOLLY_ALWAYS_INLINE static void merge(
      Node* a, Node* b, Node* parent, Node** out) {
    DCHECK(out);
    if (a == nullptr || b == nullptr) {
      *out = a ? a : b;
      if (*out) {
        (*out)->parent_ = parent;
      }
      return;
    }
    do {
      Node* grandparent = parent;
      if (FOLLY_BUILTIN_UNPREDICTABLE(compare(a, b))) {
        parent = b;
      } else {
        parent = a;
        a = b;
      }
      b = parent->right_;
      *out = parent;
      out = &parent->left_;
      parent->right_ = parent->left_;
      parent->parent_ = grandparent;
      DCHECK(a);
    } while (b != nullptr);
    *out = a;
    a->parent_ = parent;
  }

  static Node* asNode(T* x) { return NodeTraits::asNode(x); }

  static const Node* asNode(const T* x) {
    return NodeTraits::asNode(const_cast<T*>(x));
  }

  static T* asT(Node* n) { return NodeTraits::asT(n); }

  static const T* asT(const Node* n) {
    return NodeTraits::asT(const_cast<Node*>(n));
  }

  static bool compare(const Node* a, const Node* b) {
    return Compare()(*asT(a), *asT(b));
  }

  Node* root_ = nullptr;
};

} // namespace folly
