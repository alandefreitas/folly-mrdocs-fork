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

#include <atomic>
#include <memory>

#include <glog/logging.h>

#include <folly/CPortability.h>
#include <folly/Portability.h>
#include <folly/concurrency/CacheLocality.h>
#include <folly/synchronization/Hazptr-fwd.h>
#include <folly/synchronization/detail/HazptrUtils.h>

///
/// Classes related to objects protectable by hazard pointers.
///

namespace folly {

/**
 *  hazptr_obj
 *
 *  Private base class for objects protectable by hazard pointers.
 *
 *  Data members:
 *  - next_: link to next object in private singly linked lists.
 *  - reclaim_: reclamation function for this object.
 *  - cohort_tag_: A pointer to a cohort (where the object is to be
 *    pushed when retired). It can also be used as a tag (see
 *    below).
 *
 *  Cohorts, Tags, Tagged Objects, and Untagged Objects:
 *
 *  - Cohorts: Cohorts (instances of hazptr_obj_cohort) are sets of
 *    retired hazptr_obj-s. Cohorts are used to keep related objects
 *    together instead of being spread across thread local structures
 *    and/or mixed with unrelated objects.
 *
 *  - Tags: A tag is a unique identifier used for fast identification
 *    of related objects for synchronous reclamation. Tags are
 *    implemented as addresses of cohorts, with the lowest bit set (to
 *    save the space of separate cohort and tag data members and to
 *    differentiate from cohorts of untagged objects.
 *
 *  - Tagged objects: Objects are tagged for fast identification. The
 *    primary use case is for synchronous reclamation ((e.g., the
 *    destruction of all Key and Value objects that were part of a
 *    Folly ConcurrentHashMap instance). Member function
 *    set_cohort_tag makes an object tagged.
 *
 *  - Untagged objects: Objects that do not need to be identified
 *    separately from unrelated objects are not tagged (to keep tagged
 *    objects uncluttered). Untagged objects may or may not be
 *    associated with cohorts. An example of untagged objects
 *    associated with cohorts are Segment-s of Folly UnboundedQueue.
 *    Although such objects do not need synchronous reclamation,
 *    keeping them in cohorts helps avoid cases of a few missing
 *    objects delaying the reclamation of large numbers of
 *    link-counted objects. Objects are untagged either by default or
 *    after calling set_cohort_no_tag.
 *
 *  - Thread Safety: Member functions set_cohort_tag and
 *    set_cohort_no_tag are not thread-safe. Thread safety must be
 *    ensured by the calling thread.
 */
template <template <typename> class Atom>
class hazptr_obj {
  using Obj = hazptr_obj<Atom>;
  using ObjList = hazptr_obj_list<Atom>;
  using ReclaimFnPtr = void (*)(Obj*, ObjList&);

  friend class hazptr_domain<Atom>;
  template <typename, template <typename> class, typename>
  friend class hazptr_obj_base;
  template <typename, template <typename> class, typename>
  friend class hazptr_obj_base_linked;
  friend class hazptr_obj_list<Atom>;
  friend class hazptr_detail::linked_list<Obj>;
  friend class hazptr_detail::shared_head_only_list<Obj, Atom>;
  friend class hazptr_detail::shared_head_tail_list<Obj, Atom>;

  static constexpr uintptr_t kTagBit = 1u;

  ReclaimFnPtr reclaim_;
  Obj* next_;
  uintptr_t cohort_tag_;

 public:
  /** Constructors */
  /* All constructors set next_ to this in order to catch misuse bugs
     such as double retire. By default, objects are untagged and not
     associated with a cohort. */

  /// Constructs an untagged object not associated with any cohort.
  hazptr_obj() noexcept : next_(this), cohort_tag_(0) {}

  /// Copy-constructs an object, adopting the source's cohort tag.
  ///
  /// \param o The object to copy the cohort tag from.
  hazptr_obj(const Obj& o) noexcept : next_(this), cohort_tag_(o.cohort_tag_) {}

  /// Move-constructs an object, adopting the source's cohort tag.
  ///
  /// \param o The object to take the cohort tag from.
  hazptr_obj(Obj&& o) noexcept : next_(this), cohort_tag_(o.cohort_tag_) {}

  /// Copy-assigns the object, leaving its retirement linkage unchanged.
  ///
  /// \param other The object to copy from.
  /// \returns A reference to this object.
  Obj& operator=(const Obj& other) noexcept { return *this; }

  /// Move-assigns the object, leaving its retirement linkage unchanged.
  ///
  /// \param other The object to move from.
  /// \returns A reference to this object.
  Obj& operator=(Obj&& other) noexcept { return *this; }

  /// Returns the raw cohort tag, including the tag bit.
  ///
  /// \returns The stored cohort tag value.
  uintptr_t cohort_tag() { return cohort_tag_; }

  /// Returns the cohort the object is associated with, if any.
  ///
  /// \returns A pointer to the cohort, or nullptr if untagged.
  hazptr_obj_cohort<Atom>* cohort() {
    uintptr_t btag = cohort_tag_;
    btag -= btag & kTagBit;
    return reinterpret_cast<hazptr_obj_cohort<Atom>*>(btag);
  }

  /// Reports whether the object is tagged.
  ///
  /// \returns true if the object carries the tag bit, otherwise false.
  bool tagged() { return (cohort_tag_ & kTagBit) == kTagBit; }

  /// Sets the cohort and makes the object tagged.
  ///
  /// \param cohort The cohort to associate with the object.
  void set_cohort_tag(hazptr_obj_cohort<Atom>* cohort) {
    cohort_tag_ = reinterpret_cast<uintptr_t>(cohort) + kTagBit;
  }

  /// Sets the cohort and makes the object untagged.
  ///
  /// \param cohort The cohort to associate with the object.
  void set_cohort_no_tag(hazptr_obj_cohort<Atom>* cohort) {
    cohort_tag_ = reinterpret_cast<uintptr_t>(cohort);
  }

 private:
  friend class hazptr_domain<Atom>;
  template <typename, template <typename> class, typename>
  friend class hazptr_obj_base;
  friend class hazptr_obj_cohort<Atom>;

  Obj* next() const noexcept { return next_; }

  void set_next(Obj* obj) noexcept { next_ = obj; }

  ReclaimFnPtr reclaim() noexcept { return reclaim_; }

  const void* raw_ptr() const { return this; }

  void pre_retire_check() noexcept {
    // Only for catching misuse bugs like double retire
    if (next_ != this) {
      pre_retire_check_fail();
    }
  }

  void push_obj(hazptr_domain<Atom>& domain) {
    auto coh = cohort();
    if (coh) {
      DCHECK(domain.is_default_domain());
      coh->push_obj(this);
    } else {
      push_to_retired(domain);
    }
  }

  void push_to_retired(hazptr_domain<Atom>& domain) {
    hazptr_obj_list<Atom> l(this);
    hazptr_domain_push_retired(l, domain);
  }

  FOLLY_NOINLINE void pre_retire_check_fail() noexcept {
    CHECK_EQ(next_, this);
  }
}; // hazptr_obj

/**
 *  hazptr_obj_list
 *
 *  List of hazptr_obj-s.
 */
template <template <typename> class Atom>
class hazptr_obj_list {
  using Obj = hazptr_obj<Atom>;
  using List = hazptr_detail::linked_list<Obj>;

  List l_;
  int count_;

 public:
  /// Constructs an empty list.
  hazptr_obj_list() noexcept : l_(nullptr, nullptr), count_(0) {}

  /// Constructs a list holding a single object.
  ///
  /// \param obj The object to place in the list.
  explicit hazptr_obj_list(Obj* obj) noexcept : l_(obj, obj), count_(1) {
    obj->set_next(nullptr);
  }

  /// Constructs a list from an explicit head, tail, and count.
  ///
  /// \param head The first object in the list.
  /// \param tail The last object in the list.
  /// \param count The number of objects in the list.
  explicit hazptr_obj_list(Obj* head, Obj* tail, int count) noexcept
      : l_(head, tail), count_(count) {}

  /// Returns the first object in the list.
  ///
  /// \returns A pointer to the head object, or nullptr if empty.
  Obj* head() const noexcept { return l_.head(); }

  /// Returns the last object in the list.
  ///
  /// \returns A pointer to the tail object, or nullptr if empty.
  Obj* tail() const noexcept { return l_.tail(); }

  /// Returns the number of objects in the list.
  ///
  /// \returns The list's object count.
  int count() const noexcept { return count_; }

  /// Sets the cached object count.
  ///
  /// \param val The new object count.
  void set_count(int val) { count_ = val; }

  /// Reports whether the list is empty.
  ///
  /// \returns true if the list has no objects, otherwise false.
  bool empty() const noexcept { return head() == nullptr; }

  /// Pushes an object onto the list.
  ///
  /// \param obj The object to add.
  void push(Obj* obj) {
    l_.push(obj);
    ++count_;
  }

  /// Moves all objects from another list into this one.
  ///
  /// \param l The list to splice from; left empty afterwards.
  void splice(hazptr_obj_list<Atom>& l) {
    if (l.count() == 0) {
      return;
    }
    l_.splice(l.l_);
    count_ += l.count();
    l.clear();
  }

  /// Empties the list without reclaiming any objects.
  void clear() {
    l_.clear();
    count_ = 0;
  }
}; // hazptr_obj_list

/**
 *  hazptr_obj_cohort
 *
 *  List of retired objects. For objects to be retred to a cohort,
 *  either of the hazptr_obj member functions set_cohort_tag or
 *  set_cohort_no_tag needs to be called before the object is retired.
 *
 *  See description of hazptr_obj for notes on cohorts, tags, and
 *  tageed and untagged objects.
 *
 *  [Note: For now supports only the default domain.]
 */
template <template <typename> class Atom>
class hazptr_obj_cohort {
  using Obj = hazptr_obj<Atom>;
  using List = hazptr_detail::linked_list<Obj>;
  using SharedList = hazptr_detail::shared_head_tail_list<Obj, Atom>;

  static constexpr int kThreshold = 20;

  SharedList l_;
  Atom<int> count_;
  Atom<bool> active_;
  Atom<bool> pushed_to_domain_tagged_;
  Atom<Obj*> safe_list_top_;

 public:
  /** Constructor */
  hazptr_obj_cohort() noexcept
      : l_(),
        count_(0),
        active_(true),
        pushed_to_domain_tagged_{false},
        safe_list_top_{nullptr} {}

  /// Deleted copy constructor; cohorts are not copyable.
  hazptr_obj_cohort(const hazptr_obj_cohort& o) = delete;
  /// Deleted move constructor; cohorts are not movable.
  hazptr_obj_cohort(hazptr_obj_cohort&& o) = delete;
  /// Deleted copy assignment; cohorts are not copyable.
  hazptr_obj_cohort& operator=(const hazptr_obj_cohort&& o) = delete;
  /// Deleted move assignment; cohorts are not movable.
  hazptr_obj_cohort& operator=(hazptr_obj_cohort&& o) = delete;

  /** Destructor */
  ~hazptr_obj_cohort() {
    if (active()) {
      shutdown_and_reclaim();
    }
    DCHECK(!active());
    DCHECK(l_.empty());
  }

  /** shutdown_and_reclaim */
  void shutdown_and_reclaim() {
    DCHECK(active());
    clear_active();
    if (pushed_to_domain_tagged_.load(std::memory_order_relaxed)) {
      default_hazptr_domain<Atom>().cleanup_cohort_tag(this);
    }
    reclaim_safe_list();
    if (!l_.empty()) {
      List l = l_.pop_all();
      clear_count();
      Obj* obj = l.head();
      reclaim_list(obj);
    }
    DCHECK(l_.empty());
  }

 private:
  friend class hazptr_domain<Atom>;
  friend class hazptr_obj<Atom>;

  bool active() { return active_.load(std::memory_order_relaxed); }

  void clear_active() { active_.store(false, std::memory_order_relaxed); }

  int count() const noexcept { return count_.load(std::memory_order_acquire); }

  void clear_count() noexcept { count_.store(0, std::memory_order_release); }

  void inc_count() noexcept { count_.fetch_add(1, std::memory_order_release); }

  bool cas_count(int& expected, int newval) noexcept {
    return count_.compare_exchange_weak(
        expected, newval, std::memory_order_acq_rel, std::memory_order_acquire);
  }

  Obj* safe_list_top() const noexcept {
    return safe_list_top_.load(std::memory_order_acquire);
  }

  bool cas_safe_list_top(Obj*& expected, Obj* newval) noexcept {
    return safe_list_top_.compare_exchange_weak(
        expected, newval, std::memory_order_acq_rel, std::memory_order_relaxed);
  }

  /** push_obj */
  void push_obj(Obj* obj) {
    if (active()) {
      l_.push(obj);
      inc_count();
      check_threshold_push();
      if (safe_list_top()) {
        reclaim_safe_list();
      }
    } else {
      obj->set_next(nullptr);
      reclaim_list(obj);
    }
  }

  /** push_safe_obj */
  void push_safe_obj(Obj* obj) noexcept {
    while (true) {
      Obj* top = safe_list_top();
      obj->set_next(top);
      if (cas_safe_list_top(top, obj)) {
        return;
      }
    }
  }

  /** reclaim_list */
  void reclaim_list(Obj* obj) {
    while (obj) {
      hazptr_obj_list<Atom> children;
      while (obj) {
        Obj* next = obj->next();
        (*(obj->reclaim()))(obj, children);
        obj = next;
      }
      if (!children.empty()) {
        if (active()) {
          hazptr_domain_push_retired<Atom>(children);
        } else {
          obj = children.head();
        }
      }
    }
  }

  /** check_threshold_push */
  void check_threshold_push() {
    auto c = count();
    while (c >= kThreshold) {
      if (cas_count(c, 0)) {
        List ll = l_.pop_all();
        if (ll.head() && ll.head()->tagged()) {
          pushed_to_domain_tagged_.store(true, std::memory_order_relaxed);
        }
        if (kIsDebug) {
          Obj* p = ll.head();
          for (int i = 1; p; ++i, p = p->next()) {
            DCHECK_EQ(reinterpret_cast<uintptr_t>(p) & 7, uintptr_t{0})
                << p << " " << i;
          }
        }
        hazptr_obj_list<Atom> l(ll.head(), ll.tail(), c);
        hazptr_domain_push_retired<Atom>(l);
        return;
      }
    }
  }

  /** reclaim_safe_list */
  void reclaim_safe_list() {
    Obj* top = safe_list_top_.exchange(nullptr, std::memory_order_acq_rel);
    reclaim_list(top);
  }
}; // hazptr_obj_cohort

/**
 *  hazptr_obj_retired_list
 *
 *  Used for maintaining lists of retired objects in domain
 *  structure. Locked operations are used for lists of tagged
 *  objects. Unlocked operations are used for the untagged list.
 */
/** hazptr_obj_retired_list */
template <template <typename> class Atom>
class hazptr_obj_retired_list {
  using Obj = hazptr_obj<Atom>;
  using List = hazptr_detail::linked_list<Obj>;
  using RetiredList = hazptr_detail::shared_head_only_list<Obj, Atom>;

  alignas(hardware_destructive_interference_size) RetiredList retired_;
  Atom<int> count_;

 public:
  /// Push flag requesting that the retired list also be locked.
  static constexpr bool kAlsoLock = RetiredList::kAlsoLock;
  /// Push flag requesting that the retired list not be locked.
  static constexpr bool kDontLock = RetiredList::kDontLock;
  /// Push flag indicating the retired list may already be locked.
  static constexpr bool kMayBeLocked = RetiredList::kMayBeLocked;
  /// Push flag indicating the retired list may not be locked.
  static constexpr bool kMayNotBeLocked = RetiredList::kMayNotBeLocked;

 public:
  /// Constructs an empty retired list.
  hazptr_obj_retired_list() noexcept : count_(0) {}

  /// Pushes a list of retired objects, optionally locking.
  ///
  /// \param l The list of retired objects to add.
  /// \param may_be_locked Whether the retired list may already be locked.
  void push(hazptr_obj_list<Atom>& l, bool may_be_locked) noexcept {
    List ll(l.head(), l.tail());
    retired_.push(ll, may_be_locked);
    add_count(l.count());
  }

  /// Pushes a list of retired objects and releases the lock.
  ///
  /// \param l The list of retired objects to add.
  void push_unlock(hazptr_obj_list<Atom>& l) noexcept {
    List ll(l.head(), l.tail());
    retired_.push_unlock(ll);
    auto count = l.count();
    if (count) {
      add_count(count);
    }
  }

  /// Returns the number of retired objects.
  ///
  /// \returns The current retired object count.
  int count() const noexcept { return count_.load(std::memory_order_acquire); }

  /// Reports whether the retired list is empty.
  ///
  /// \returns true if there are no retired objects, otherwise false.
  bool empty() { return retired_.empty(); }

  /// Zeroes the count if it meets or exceeds the given threshold.
  ///
  /// \param thresh The threshold to compare against.
  /// \returns true if the count was reset to zero, otherwise false.
  bool check_threshold_try_zero_count(int thresh) {
    auto oldval = count();
    while (oldval >= thresh) {
      if (cas_count(oldval, 0)) {
        return true;
      }
    }
    return false;
  }

  /// Removes and returns all retired objects.
  ///
  /// \param lock Whether to lock the retired list while popping.
  /// \returns The head of the popped list of objects.
  Obj* pop_all(bool lock) { return retired_.pop_all(lock); }

  /// Reports whether the retired list is currently locked.
  ///
  /// \returns true if the list is locked, otherwise false.
  bool check_lock() { return retired_.check_lock(); }

 private:
  void add_count(int val) { count_.fetch_add(val, std::memory_order_release); }

  bool cas_count(int& expected, int newval) {
    return count_.compare_exchange_weak(
        expected, newval, std::memory_order_acq_rel, std::memory_order_acquire);
  }
}; // hazptr_obj_retired_list

/**
 *  hazptr_deleter
 *
 *  For empty base optimization.
 */
template <typename T, typename D>
class hazptr_deleter {
  D deleter_;

 public:
  /// Sets the deleter used to reclaim objects.
  ///
  /// \param d The deleter to store.
  void set_deleter(D d = {}) { deleter_ = std::move(d); }

  /// Deletes an object using the stored deleter.
  ///
  /// \param p The object to delete.
  void delete_obj(T* p) { deleter_(p); }
};

/// Specialization of `hazptr_deleter` for the default deleter.
template <typename T>
class hazptr_deleter<T, std::default_delete<T>> {
 public:
  /// Accepts and ignores a default deleter.
  ///
  /// \param unused Placeholder default deleter argument.
  void set_deleter(std::default_delete<T> unused = {}) {}

  /// Deletes an object with the `delete` expression.
  ///
  /// \param p The object to delete.
  void delete_obj(T* p) { delete p; }
};

/**
 *  hazptr_obj_base
 *
 *  Base template for objects protected by hazard pointers.
 */
template <typename T, template <typename> class Atom, typename D>
class hazptr_obj_base : public hazptr_obj<Atom>, public hazptr_deleter<T, D> {
 public:
  /// Retire a removed object and pass the responsibility for
  /// reclaiming it to the hazptr library.
  ///
  /// \param deleter The deleter used to reclaim the object.
  /// \param domain The hazard pointer domain to retire into.
  void retire(
      D deleter = {},
      hazptr_domain<Atom>& domain = default_hazptr_domain<Atom>()) {
    pre_retire(std::move(deleter));
    set_reclaim();
    this->push_obj(domain); // defined in hazptr_obj
  }

  /// Retire a removed object into a specific domain with the default deleter.
  ///
  /// \param domain The hazard pointer domain to retire into.
  void retire(hazptr_domain<Atom>& domain) { retire({}, domain); }

 private:
  void pre_retire(D deleter) {
    this->pre_retire_check(); // defined in hazptr_obj
    this->set_deleter(std::move(deleter));
  }

  void set_reclaim() {
    this->reclaim_ = [](hazptr_obj<Atom>* p, hazptr_obj_list<Atom>&) {
      auto hobp = static_cast<hazptr_obj_base<T, Atom, D>*>(p);
      auto obj = static_cast<T*>(hobp);
      hobp->delete_obj(obj);
    };
  }
}; // hazptr_obj_base

} // namespace folly
