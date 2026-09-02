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

#include <folly/synchronization/Hazptr.h>

namespace folly {

/// A lock-free LIFO stack that reclaims popped nodes with hazard pointers.
template <typename T, template <typename> class Atom = std::atomic>
class HazptrLockFreeLIFO {
  struct Node;

  Atom<Node*> head_;

 public:
  /// Construct an empty stack.
  HazptrLockFreeLIFO() : head_(nullptr) {}

  /// Destroy the stack and retire any remaining nodes for reclamation.
  ~HazptrLockFreeLIFO() {
    Node* next;
    for (auto node = head(); node; node = next) {
      next = node->next();
      node->retire();
    }
    hazptr_cleanup<Atom>();
  }

  /// Push a value onto the top of the stack.
  ///
  /// \param val The value to push.
  void push(T val) {
    auto node = new Node(val, head());
    while (!cas_head(node->next_, node)) {
      /* try again */;
    }
  }

  /// Pop the top value off the stack.
  ///
  /// \param val Set to the popped value when the stack was not empty.
  /// \returns `true` if a value was popped, `false` if the stack was empty.
  bool pop(T& val) {
    hazptr_local<1, Atom> h;
    hazptr_holder<Atom>& hptr = h[0];
    Node* node;
    while (true) {
      node = hptr.protect(head_);
      if (node == nullptr) {
        return false;
      }
      auto next = node->next();
      if (cas_head(node, next)) {
        break;
      }
    }
    hptr.reset_protection();
    val = node->value();
    node->retire();
    return true;
  }

 private:
  Node* head() { return head_.load(std::memory_order_acquire); }

  bool cas_head(Node*& expected, Node* newval) {
    return head_.compare_exchange_weak(
        expected, newval, std::memory_order_acq_rel, std::memory_order_acquire);
  }

  struct Node : public hazptr_obj_base<Node, Atom> {
    T value_;
    Node* next_;

    Node(T v, Node* n) : value_(v), next_(n) {}

    Node* next() { return next_; }

    T value() { return value_; }
  };
};

} // namespace folly
