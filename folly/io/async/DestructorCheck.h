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

/// Root namespace for the Folly library.
namespace folly {

/**
 * DestructorCheck is a helper class that helps to detect if a tracked object
 * was deleted.
 * This is useful for objects that request callbacks from other components.
 *
 * Classes needing this functionality should:
 * - derive from DestructorCheck
 *
 * Callback context can be extended with an instance of DestructorCheck::Safety
 * object initialized with a reference to the object dereferenced from the
 * callback.  Once the callback is invoked, it can use this safety object to
 * check if the object was not deallocated yet before dereferencing it.
 *
 * DestructorCheck does not perform any locking.  It is intended to be used
 * only from a single thread.
 *
 * Example:
 *
 * class AsyncFoo : public DestructorCheck {
 *  public:
 *   ~AsyncFoo();
 *   // awesome async code with circuitous deletion paths
 *   void async1();
 *   void async2();
 * };
 *
 * righteousFunc(AsyncFoo& f) {
 *   DestructorCheck::Safety safety(f);
 *
 *   f.async1(); // might have deleted f, oh noes
 *   if (!safety.destroyed()) {
 *     // phew, still there
 *     f.async2();
 *   }
 * }
 */

class DestructorCheck {
 public:
  /// Destroys the object and marks every attached Safety as destroyed.
  virtual ~DestructorCheck() { rootGuard_.setAllDestroyed(); }

  class Safety;

  /// Intrusive linked-list node holding the next Safety guard.
  class ForwardLink {
    // These methods are mostly private because an outside caller could violate
    // the integrity of the linked list.
   private:
    void setAllDestroyed() {
      for (auto guard = next_; guard; guard = guard->next_) {
        guard->setDestroyed();
      }
    }

    // This is used to maintain the double-linked list. An intrusive list does
    // not require any heap allocations, like a standard container would. This
    // isolation of next_ in its own class means that the DestructorCheck can
    // easily hold a next_ pointer without needing to hold a prev_ pointer.
    // DestructorCheck never needs a prev_ pointer because it is the head node
    // and this is a special list where the head never moves and never has a
    // previous node.
    Safety* next_{nullptr};

    friend class DestructorCheck;
    friend class Safety;
  };

  /// Tracks whether a referenced DestructorCheck object was destroyed.
  class Safety : public ForwardLink {
   public:
    /// Registers this guard against the given object.
    ///
    /// \param destructorCheck The object whose destruction is tracked.
    explicit Safety(DestructorCheck& destructorCheck) {
      // Insert this node at the head of the list.
      prev_ = &destructorCheck.rootGuard_;
      next_ = prev_->next_;
      if (next_ != nullptr) {
        next_->prev_ = this;
      }
      prev_->next_ = this;
    }

    /// Unlinks this guard from the tracked object if still alive.
    ~Safety() {
      if (!destroyed()) {
        // Remove this node from the list.
        prev_->next_ = next_;
        if (next_ != nullptr) {
          next_->prev_ = prev_;
        }
      }
    }

    /// Deleted copy constructor.
    Safety(const Safety& other) = delete;
    /// Deleted move constructor.
    Safety(Safety&& goner) = delete;
    /// Deleted copy assignment.
    Safety& operator=(const Safety& other) = delete;
    /// Deleted move assignment.
    Safety& operator=(Safety&& other) = delete;

    /// Returns whether the tracked object has been destroyed.
    ///
    /// \returns `true` if the tracked object has been destroyed.
    bool destroyed() const { return prev_ == nullptr; }

   private:
    void setDestroyed() { prev_ = nullptr; }

    // This field is used to maintain the double-linked list. If the root has
    // been destroyed then the field is set to the nullptr sentinel value.
    ForwardLink* prev_;

    friend class ForwardLink;
  };

 private:
  ForwardLink rootGuard_;
};

} // namespace folly
