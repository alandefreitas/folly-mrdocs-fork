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
 * Improved thread local storage for non-trivial types (similar speed as
 * pthread_getspecific but only consumes a single pthread_key_t, and 4x faster
 * than boost::thread_specific_ptr).
 *
 * ThreadLocal objects can be grouped together logically under a tag. Within
 * a tag, each object has a unique id. The combination of tag and id is used to
 * locate the managed object corresponding to the current thread.
 *
 * Also includes an accessor interface to iterate all of the managed
 * objects owned by a ThreadLocal object, each corresponding to a
 * separate thread.  accessAllThreads() initializes an accessor
 * which holds
 * a lock *that blocks all creation and destruction of managed
 * objects managed by the ThreadLocal. The accessor can be used
 * as an iterable container. Note: for now, the accessor also happens to hold
 * other per tag global locks and hence calls to accessAllThreads() are
 * serialized at tag level.
 *
 * accessAllThreads() can race with destruction of thread-local elements. We
 * provide a strict mode which is dangerous because it requires the access lock
 * to be held while destroying thread-local elements which could cause
 * deadlocks. We gate this mode behind the AccessModeStrict template parameter.
 *
 * Intended use is for frequent write, infrequent read data access patterns such
 * as counters.
 *
 * There are two classes here - ThreadLocal and ThreadLocalPtr.  ThreadLocalPtr
 * has semantics similar to boost::thread_specific_ptr. ThreadLocal is a thin
 * wrapper around ThreadLocalPtr that manages allocation automatically.
 */

#pragma once

#include <iterator>
#include <thread>
#include <type_traits>
#include <utility>

#include <folly/Likely.h>
#include <folly/Portability.h>
#include <folly/ScopeGuard.h>
#include <folly/SharedMutex.h>
#include <folly/detail/ThreadLocalDetail.h>

namespace folly {

template <class T, class Tag, class AccessMode>
class ThreadLocalPtr;

/// Thread-local storage for a value of type `T`.
///
/// A thin wrapper around `ThreadLocalPtr` that manages allocation
/// automatically, constructing a distinct `T` for each thread on first
/// access. `Tag` groups instances that share a `pthread_key_t`, and
/// `AccessMode` selects the access mode for iterating all threads.
template <class T, class Tag = void, class AccessMode = void>
class ThreadLocal {
 public:
  /// Constructs a thread-local whose per-thread values are default-constructed.
  constexpr ThreadLocal() noexcept : constructor_([]() { return T(); }) {}

  /// Constructs a thread-local whose per-thread values are produced by
  /// `constructor`.
  ///
  /// \param constructor Callable invoked once per thread to produce the `T`.
  template <typename F>
    requires std::is_invocable_r_v<T, F>
  explicit ThreadLocal(F&& constructor)
      : constructor_(std::forward<F>(constructor)) {}

  /// Move-constructs from `that`, leaving it valueless.
  ///
  /// \param that The thread-local to move from.
  ThreadLocal(ThreadLocal&& that) noexcept
      : tlp_{std::move(that.tlp_)},
        constructor_{std::exchange(that.constructor_, {})} {}

  /// Move-assigns from `that`, leaving it valueless.
  ///
  /// \param that The thread-local to move from.
  /// \returns A reference to `*this`.
  ThreadLocal& operator=(ThreadLocal&& that) noexcept {
    assert(this != &that);
    tlp_ = std::exchange(that.tlp_, {});
    constructor_ = std::exchange(that.constructor_, {});
    return *this;
  }

  /// Returns the calling thread's value, constructing it on first access.
  ///
  /// \returns A pointer to the calling thread's `T`.
  FOLLY_ERASE T* get() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    auto const ptr = tlp_.get();
    return FOLLY_LIKELY(!!ptr) ? ptr : makeTlp();
  }

  /// Returns the calling thread's value without constructing it.
  ///
  /// \returns A pointer to the calling thread's `T`, or null if it has not
  /// been constructed yet.
  FOLLY_ERASE T* get_existing() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return tlp_.get();
  }

  /// Accesses the calling thread's value, constructing it on first access.
  ///
  /// \returns A pointer to the calling thread's `T`.
  T* operator->() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] { return get(); }

  /// Accesses the calling thread's value, constructing it on first access.
  ///
  /// \returns A reference to the calling thread's `T`.
  T& operator*() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] { return *get(); }

  /// Replaces the calling thread's value, deleting the previous one.
  ///
  /// \param newPtr The pointer to take ownership of, or null to clear.
  void reset(T* newPtr = nullptr) { tlp_.reset(newPtr); }

  /// Accessor type used to iterate the values held for all threads.
  using Accessor = typename ThreadLocalPtr<T, Tag, AccessMode>::Accessor;

  /// Returns an accessor that iterates the values held for all threads.
  ///
  /// \returns An `Accessor` holding the per-tag lock for the iteration.
  Accessor accessAllThreads() const { return tlp_.accessAllThreads(); }

 private:
  // non-copyable
  ThreadLocal(const ThreadLocal&) = delete;
  ThreadLocal& operator=(const ThreadLocal&) = delete;

  FOLLY_NOINLINE T* makeTlp() const {
    auto const ptr = new T(constructor_());
    tlp_.reset(ptr);
    return ptr;
  }

  mutable ThreadLocalPtr<T, Tag, AccessMode> tlp_;
  std::function<T()> constructor_;
};

/*
 * The idea here is that __thread is faster than pthread_getspecific, so we
 * keep a __thread array of pointers to objects (ThreadEntry::elements) where
 * each array has an index for each unique instance of the ThreadLocalPtr
 * object.  Each ThreadLocalPtr object has a unique id that is an index into
 * these arrays so we can fetch the correct object from thread local storage
 * very efficiently.
 *
 * In order to prevent unbounded growth of the id space and thus huge
 * ThreadEntry::elements, arrays, for example due to continuous creation and
 * destruction of ThreadLocalPtr objects, we keep a set of all active
 * instances.  When an instance is destroyed we remove it from the active
 * set and insert the id into freeIds_ for reuse.  These operations require a
 * global mutex, but only happen at construction and destruction time.
 *
 * We use a single global pthread_key_t per Tag to manage object destruction and
 * memory cleanup upon thread exit because there is a finite number of
 * pthread_key_t's available per machine.
 *
 * NOTE: Apple platforms don't support the same semantics for __thread that
 *       Linux does (and it's only supported at all on i386). For these, use
 *       pthread_setspecific()/pthread_getspecific() for the per-thread
 *       storage.  Windows (MSVC and GCC) does support the same semantics
 *       with __declspec(thread)
 */

/// Thread-local owning pointer to a value of type `T`.
///
/// Has semantics similar to `boost::thread_specific_ptr`: each thread sees an
/// independent pointer, and the pointed-to object is deleted when the thread
/// exits or the pointer is reset. `Tag` groups instances that share a
/// `pthread_key_t`, and `AccessMode` selects the access mode for iterating all
/// threads.
template <class T, class Tag = void, class AccessMode = void>
class ThreadLocalPtr {
 private:
  using StaticMeta = threadlocal_detail::StaticMeta<Tag, AccessMode>;

  using AccessAllThreadsEnabled = std::negation<std::is_same<Tag, void>>;

 public:
  /// Constructs a thread-local pointer that is null on every thread.
  constexpr ThreadLocalPtr() noexcept : id_() {}

  /// Move-constructs from `other`, leaving it valueless.
  ///
  /// \param other The pointer to move from.
  ThreadLocalPtr(ThreadLocalPtr&& other) noexcept : id_(std::move(other.id_)) {}

  /// Move-assigns from `other`, destroying any values held by `*this` first.
  ///
  /// \param other The pointer to move from.
  /// \returns A reference to `*this`.
  ThreadLocalPtr& operator=(ThreadLocalPtr&& other) noexcept {
    assert(this != &other);
    destroy(); // user-provided dtors invoked within here must not throw
    id_ = std::move(other.id_);
    return *this;
  }

  /// Destroys the pointer, deleting the values held for all threads.
  ~ThreadLocalPtr() { destroy(); }

  /// Returns the calling thread's pointer.
  ///
  /// \returns The calling thread's `T*`, or null if none is set.
  T* get() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    threadlocal_detail::ElementWrapper& w = StaticMeta::get(&id_);
    return static_cast<T*>(w.ptr);
  }

  /// Accesses the calling thread's value.
  ///
  /// \returns The calling thread's `T*`.
  T* operator->() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] { return get(); }

  /// Accesses the calling thread's value.
  ///
  /// \returns A reference to the calling thread's `T`.
  T& operator*() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] { return *get(); }

  /// Releases ownership of the calling thread's value without deleting it.
  ///
  /// \returns The calling thread's `T*`; the caller takes ownership.
  T* release() {
    auto rlocked = getForkGuard();
    threadlocal_detail::ThreadEntry* te = StaticMeta::getThreadEntry(&id_);
    auto id = id_.getOrInvalid();
    // Only valid index into the elements array
    DCHECK_NE(id, threadlocal_detail::kEntryIDInvalid);
    return static_cast<T*>(te->releaseElement(id));
  }

  /// Replaces the calling thread's value, deleting the previous one.
  ///
  /// \param newPtr The pointer to take ownership of, or null to clear.
  void reset(T* newPtr = nullptr) {
    auto rlocked = getForkGuard();
    auto guard = makeGuard([&] { delete newPtr; });
    threadlocal_detail::ThreadEntry* te = StaticMeta::getThreadEntry(&id_);
    uint32_t id = id_.getOrInvalid();
    // Only valid index into the elements array
    DCHECK_NE(id, threadlocal_detail::kEntryIDInvalid);
    te->resetElement(newPtr, id);
    guard.dismiss();
  }

  /// Tests whether the calling thread's value is set.
  ///
  /// \returns `true` if the calling thread's pointer is non-null.
  explicit operator bool() const { return get() != nullptr; }

  /// Replaces the calling thread's value, transferring ownership from a
  /// `unique_ptr` and preserving its deleter.
  ///
  /// \param source The smart pointer to take ownership from.
  template <
      typename SourceT,
      typename Deleter,
      typename = typename std::enable_if<
          std::is_convertible<SourceT*, T*>::value>::type>
  void reset(std::unique_ptr<SourceT, Deleter> source) {
    auto deleter =
        [delegate = source.get_deleter()](T* ptr, TLPDestructionMode) {
          delegate(ptr);
        };
    reset(source.release(), deleter);
  }

  /// Replaces the calling thread's value, transferring ownership from a
  /// `unique_ptr` with the default deleter.
  ///
  /// \param source The smart pointer to take ownership from.
  template <
      typename SourceT,
      typename = typename std::enable_if<
          std::is_convertible<SourceT*, T*>::value>::type>
  void reset(std::unique_ptr<SourceT> source) {
    reset(source.release());
  }

  /// Replaces the calling thread's value with a custom deleter.
  ///
  /// The deleter is invoked as `deleter(T* ptr, TLPDestructionMode mode)`,
  /// where `mode` is `ALL_THREADS` when destroying this `ThreadLocalPtr` (and
  /// thus deleting pointers for all threads) and `THIS_THREAD` when deleting
  /// the value for a single thread (on thread exit or `reset()`). Invoking the
  /// deleter must not throw.
  ///
  /// \param newPtr The pointer to take ownership of, or null to clear.
  /// \param deleter The callable used to destroy the owned value.
  template <class Deleter>
  void reset(T* newPtr, const Deleter& deleter) {
    auto guard = makeGuard([&] {
      if (newPtr) {
        deleter(newPtr, TLPDestructionMode::THIS_THREAD);
      }
    });

    auto rlocked = getForkGuard();
    threadlocal_detail::ThreadEntry* te = StaticMeta::getThreadEntry(&id_);
    uint32_t id = id_.getOrInvalid();
    // Only valid index into the elements array
    DCHECK_NE(id, threadlocal_detail::kEntryIDInvalid);
    te->resetElement(newPtr, deleter, id);
    guard.dismiss();
  }

  /// Replaces the calling thread's value, sharing ownership with a
  /// `shared_ptr`.
  ///
  /// \param newPtr The shared pointer whose ownership is shared.
  void reset(const std::shared_ptr<T>& newPtr) {
    reset(newPtr.get(), threadlocal_detail::SharedPtrDeleter{newPtr});
  }

  /// Iterable view over the values held for all threads.
  ///
  /// Holds a global lock for iteration through all thread-local child objects
  /// and can be used as an iterable container. Use `accessAllThreads()` to
  /// obtain one.
  class Accessor {
    friend class ThreadLocalPtr<T, Tag, AccessMode>;

    threadlocal_detail::StaticMetaBase& meta_ =
        threadlocal_detail::StaticMeta<Tag, AccessMode>::instance();
    std::unique_lock<SharedMutex> accessAllThreadsLock_;
    std::shared_lock<SharedMutex> forkHandlerLock_;
    uint32_t id_ = 0;

    // Prevent the entry set from changing while we are iterating over it.
    // reset() calls to populate will acquire shared lock on the id's set.
    threadlocal_detail::StaticMetaBase::SynchronizedThreadEntrySet::WLockedPtr
        wlockedThreadEntrySet_;

   public:
    class Iterator;
    friend class Iterator;

    /// Bidirectional iterator over the values held for all threads.
    ///
    /// The iterators obtained from `Accessor` are bidirectional iterators.
    class Iterator {
      friend class Accessor;
      const Accessor* accessor_{nullptr};
      using InnerVector = threadlocal_detail::ThreadEntrySet::ElementVector;
      using InnerIterator = InnerVector::iterator;

      InnerVector& vec_;
      InnerIterator iter_;

      void increment() {
        if (iter_ != vec_.end()) {
          ++iter_;
          incrementToValid();
        }
      }

      void decrement() {
        if (iter_ != vec_.begin()) {
          --iter_;
          decrementToValid();
        }
      }

      const T& dereference() const {
        return *static_cast<T*>(iter_->wrapper.ptr);
      }

      T& dereference() { return *static_cast<T*>(iter_->wrapper.ptr); }

      bool equal(const Iterator& other) const {
        return (accessor_->id_ == other.accessor_->id_ && iter_ == other.iter_);
      }

      void setToEnd() { iter_ = vec_.end(); }

      explicit Iterator(const Accessor* accessor, bool toEnd = false)
          : accessor_(accessor),
            vec_(accessor_->wlockedThreadEntrySet_->threadElements),
            iter_(vec_.begin()) {
        if (toEnd) {
          setToEnd();
        } else {
          incrementToValid();
        }
      }

      // we just need to check the ptr since it can be set to nullptr
      // even if the entry is part of the list
      bool valid() const { return (iter_ != vec_.end() && iter_->wrapper.ptr); }

      void incrementToValid() {
        for (; iter_ != vec_.end() && !valid(); ++iter_) {
        }
      }

      void decrementToValid() {
        for (; iter_ != vec_.begin() && !valid(); --iter_) {
        }
      }

     public:
      /// Signed distance type between two iterators.
      using difference_type = ssize_t;
      /// Type of the values iterated over.
      using value_type = T;
      /// Reference type yielded when dereferencing.
      using reference = T const&;
      /// Pointer type yielded by `operator->`.
      using pointer = T const*;
      /// Iterator category tag: bidirectional.
      using iterator_category = std::bidirectional_iterator_tag;

      /// Constructs a past-the-end iterator not bound to any accessor.
      Iterator() = default;

      /// Advances to the next value.
      ///
      /// \returns A reference to `*this`.
      Iterator& operator++() {
        increment();
        return *this;
      }

      /// Advances to the next value.
      ///
      /// \param dummy Unused tag selecting the post-increment form.
      /// \returns A copy of the iterator before advancing.
      Iterator& operator++(int dummy) {
        Iterator copy(*this);
        increment();
        return copy;
      }

      /// Moves to the previous value.
      ///
      /// \returns A reference to `*this`.
      Iterator& operator--() {
        decrement();
        return *this;
      }

      /// Moves to the previous value.
      ///
      /// \param dummy Unused tag selecting the post-decrement form.
      /// \returns A copy of the iterator before moving back.
      Iterator& operator--(int dummy) {
        Iterator copy(*this);
        decrement();
        return copy;
      }

      /// Dereferences to the current value.
      ///
      /// \returns A reference to the current thread's `T`.
      T& operator*() { return dereference(); }

      /// Dereferences to the current value.
      ///
      /// \returns A const reference to the current thread's `T`.
      T const& operator*() const { return dereference(); }

      /// Accesses the current value.
      ///
      /// \returns A pointer to the current thread's `T`.
      T* operator->() { return &dereference(); }

      /// Accesses the current value.
      ///
      /// \returns A const pointer to the current thread's `T`.
      T const* operator->() const { return &dereference(); }

      /// Tests whether two iterators refer to the same position.
      ///
      /// \param rhs The iterator to compare against.
      /// \returns `true` if both iterators are equal.
      bool operator==(Iterator const& rhs) const { return equal(rhs); }

      /// Tests whether two iterators refer to different positions.
      ///
      /// \param rhs The iterator to compare against.
      /// \returns `true` if the iterators differ.
      bool operator!=(Iterator const& rhs) const { return !equal(rhs); }

      /// Returns the C++ thread id owning the current value.
      ///
      /// \returns The `std::thread::id` of the owning thread.
      std::thread::id getThreadId() const { return iter_->threadEntry->tid(); }

      /// Returns the OS-level thread id owning the current value.
      ///
      /// \returns The OS thread id of the owning thread.
      uint64_t getOSThreadId() const { return iter_->threadEntry->tid_os; }
    };

    /// Destroys the accessor, releasing the locks it holds.
    ~Accessor() { release(); }

    /// Returns an iterator to the first value held for any thread.
    ///
    /// \returns An `Iterator` to the beginning of the range.
    Iterator begin() const { return Iterator(this); }

    /// Returns a past-the-end iterator.
    ///
    /// \returns An `Iterator` one past the last value.
    Iterator end() const { return Iterator(this, true); }

    /// Deleted copy constructor; an accessor is not copyable.
    /// \param other The accessor that would be copied from.
    Accessor(const Accessor& other) = delete;
    /// Deleted copy assignment; an accessor is not copyable.
    /// \param other The accessor that would be assigned from.
    /// \returns Nothing; this operator is deleted.
    Accessor& operator=(const Accessor& other) = delete;

    /// Move-constructs from `other`, transferring its locks.
    ///
    /// \param other The accessor to move from.
    Accessor(Accessor&& other) noexcept
        : meta_(other.meta_),
          accessAllThreadsLock_(std::move(other.accessAllThreadsLock_)),
          forkHandlerLock_(std::move(other.forkHandlerLock_)),
          id_(std::exchange(other.id_, 0)) {
      wlockedThreadEntrySet_ = std::move(other.wlockedThreadEntrySet_);
    }

    /// Move-assigns from `other`, transferring its locks.
    ///
    /// \param other The accessor to move from.
    /// \returns A reference to `*this`.
    Accessor& operator=(Accessor&& other) noexcept {
      // Each Tag has its own unique meta, and accessors with different Tags
      // have different types.  So either *this is empty, or this and other
      // have the same tag.  But if they have the same tag, they have the same
      // meta (and lock), so they'd both hold the lock at the same time,
      // which is impossible, which leaves only one possible scenario --
      // *this is empty.  Assert it.
      assert(&meta_ == &other.meta_);
      using std::swap;
      swap(accessAllThreadsLock_, other.accessAllThreadsLock_);
      swap(forkHandlerLock_, other.forkHandlerLock_);
      swap(id_, other.id_);
      wlockedThreadEntrySet_.unlock();
      swap(wlockedThreadEntrySet_, other.wlockedThreadEntrySet_);
    }

    /// Constructs an empty accessor that holds no locks.
    Accessor() = default;

   private:
    explicit Accessor(uint32_t id)
        : accessAllThreadsLock_(meta_.accessAllThreadsLock_, std::defer_lock),
          forkHandlerLock_(meta_.forkHandlerLock_, std::defer_lock),
          id_(id) {
      forkHandlerLock_.lock();
      accessAllThreadsLock_.lock();
      wlockedThreadEntrySet_ = meta_.allId2ThreadEntrySets_[id_].wlock();
    }

    void release() {
      if (accessAllThreadsLock_) {
        wlockedThreadEntrySet_.unlock();
        accessAllThreadsLock_.unlock();
        DCHECK(forkHandlerLock_);
        forkHandlerLock_.unlock();
        id_ = 0;
      }
    }
  };

  /// Returns an accessor that iterates the values held for all threads.
  ///
  /// The accessor lets a client iterate through all thread-local child
  /// elements of this instance and holds a global lock for each `Tag`.
  /// Requires a unique `Tag` (not `void`).
  ///
  /// \returns An `Accessor` holding the per-tag lock for the iteration.
  Accessor accessAllThreads() const {
    static_assert(
        AccessAllThreadsEnabled::value,
        "Must use a unique Tag to use the accessAllThreads feature");
    return Accessor(id_.getOrAllocate(StaticMeta::instance()));
  }

 private:
  void destroy() noexcept {
    auto const val = id_.value.load(std::memory_order_relaxed);
    if (val == threadlocal_detail::kEntryIDInvalid) {
      return;
    }
    StaticMeta::instance().destroy(&id_);
    // User provided destructors should not cause the TL to have its id
    // reallocated.
    DCHECK(
        id_.value.load(std::memory_order_relaxed) ==
        threadlocal_detail::kEntryIDInvalid);
  }

  // non-copyable
  ThreadLocalPtr(const ThreadLocalPtr&) = delete;
  ThreadLocalPtr& operator=(const ThreadLocalPtr&) = delete;

  static auto getForkGuard() {
    auto& mutex = StaticMeta::instance().forkHandlerLock_;
    return std::shared_lock{mutex};
  }

  mutable typename StaticMeta::EntryID id_;
};

} // namespace folly
