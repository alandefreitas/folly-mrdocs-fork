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

#include <folly/AtomicIntrusiveLinkedList.h>
#include <folly/Memory.h>

namespace folly {

/**
 * A very simple atomic single-linked list primitive.
 *
 * Usage:
 *
 * AtomicLinkedList<MyClass> list;
 * list.insert(a);
 * list.sweep([] (MyClass& c) { doSomething(c); }
 */

template <class T>
class AtomicLinkedList {
 public:
  /// Construct an empty list.
  AtomicLinkedList() {}
  /// Copy construction is deleted; the list is not copyable.
  ///
  /// \param other The list that would be copied from.
  AtomicLinkedList(const AtomicLinkedList& other) = delete;
  /// Copy assignment is deleted; the list is not copyable.
  ///
  /// \param other The list that would be assigned from.
  /// \returns A reference to this list.
  AtomicLinkedList& operator=(const AtomicLinkedList& other) = delete;
  /// Move-construct from another list, leaving it empty.
  ///
  /// \param other The list to move from.
  AtomicLinkedList(AtomicLinkedList&& other) noexcept = default;
  /// Move-assign from another list, sweeping the current contents first.
  ///
  /// \param other The list to move from.
  /// \returns A reference to this list.
  AtomicLinkedList& operator=(AtomicLinkedList&& other) noexcept {
    list_.reverseSweepAndAssign(std::move(other.list_), [](Wrapper* node) {
      delete node;
    });
    return *this;
  }

  /// Destroy the list, sweeping and discarding any remaining elements.
  ~AtomicLinkedList() {
    sweep([](T&&) {});
  }

  /// Check whether the list is empty.
  ///
  /// \returns True if the list has no elements.
  bool empty() const { return list_.empty(); }

  /**
   * Atomically insert t at the head of the list.
   *
   * \param t The element to insert at the head of the list.
   * @return True if the inserted element is the only one in the list
   *         after the call.
   */
  bool insertHead(T t) {
    auto wrapper = std::make_unique<Wrapper>(std::move(t));

    return list_.insertHead(wrapper.release());
  }

  /**
   * Repeatedly pops element from head,
   * and calls func() on the removed elements in the order from tail to head.
   * Stops when the list is empty.
   *
   * \param func The callback invoked with each removed element.
   */
  template <typename F>
  void sweep(F&& func) {
    list_.sweep([&](Wrapper* wrapperPtr) mutable {
      std::unique_ptr<Wrapper> wrapper(wrapperPtr);

      func(std::move(wrapper->data));
    });
  }

  /**
   * Sweeps the list a single time, as a single point in time swap with the
   * current contents of the list.
   *
   * Unlike sweep() it does not loop to ensure the list is empty at some point
   * after the last invocation.
   *
   * Returns false if the list is empty.
   *
   * \param func The callback invoked with each removed element.
   * \returns True if any element was swept, false if the list was empty.
   */
  template <typename F>
  bool sweepOnce(F&& func) {
    return list_.sweepOnce([&](Wrapper* wrappedPtr) {
      std::unique_ptr<Wrapper> wrapper(wrappedPtr);
      func(std::move(wrapper->data));
    });
  }

  /**
   * Similar to sweep() but calls func() on elements in LIFO order.
   *
   * func() is called for all elements in the list at the moment
   * reverseSweep() is called.  Unlike sweep() it does not loop to ensure the
   * list is empty at some point after the last invocation.  This way callers
   * can reason about the ordering: elements inserted since the last call to
   * reverseSweep() will be provided in LIFO order.
   *
   * Example: if elements are inserted in the order 1-2-3, the callback is
   * invoked 3-2-1.  If the callback moves elements onto a stack, popping off
   * the stack will produce the original insertion order 1-2-3.
   *
   * \param func The callback invoked with each removed element in LIFO order.
   */
  template <typename F>
  void reverseSweep(F&& func) {
    list_.reverseSweep([&](Wrapper* wrapperPtr) mutable {
      std::unique_ptr<Wrapper> wrapper(wrapperPtr);

      func(std::move(wrapper->data));
    });
  }

 private:
  struct Wrapper {
    explicit Wrapper(T&& t) : data(std::move(t)) {}

    AtomicIntrusiveLinkedListHook<Wrapper> hook;
    T data;
  };
  AtomicIntrusiveLinkedList<Wrapper, &Wrapper::hook> list_;
};

} // namespace folly
