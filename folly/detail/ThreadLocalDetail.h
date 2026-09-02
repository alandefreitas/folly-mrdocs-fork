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

#include <limits.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <glog/logging.h>

#include <folly/CppAttributes.h>
#include <folly/Exception.h>
#include <folly/Function.h>
#include <folly/MapUtil.h>
#include <folly/Portability.h>
#include <folly/ScopeGuard.h>
#include <folly/SharedMutex.h>
#include <folly/Synchronized.h>
#include <folly/concurrency/container/atomic_grow_array.h>
#include <folly/container/F14Map.h>
#include <folly/container/Foreach.h>
#include <folly/detail/StaticSingletonManager.h>
#include <folly/detail/UniqueInstance.h>
#include <folly/lang/Exception.h>
#include <folly/memory/Malloc.h>
#include <folly/portability/PThread.h>
#include <folly/synchronization/MicroSpinLock.h>
#include <folly/synchronization/RelaxedAtomic.h>
#include <folly/system/AtFork.h>
#include <folly/system/ThreadId.h>

/// The Folly library.
namespace folly {

/// Selects which threads' thread-local instances are destroyed.
enum class TLPDestructionMode {
  THIS_THREAD, ///< Destroy only the current thread's instance.
  ALL_THREADS, ///< Destroy instances across all threads.
};
/// Tag selecting strict access mode for thread-local storage.
struct AccessModeStrict {};

/// Implementation details.
namespace threadlocal_detail {

/// Sentinel entry id representing an unallocated thread-local slot.
constexpr uint32_t kEntryIDInvalid = std::numeric_limits<uint32_t>::max();

/// Deleter that owns a shared_ptr and disposes it on thread-local destruction.
///
/// As a memory-usage optimization, try to make this deleter fit in-situ in
/// the deleter function storage rather than being heap-allocated separately.
///
/// For libstdc++, specialization below of std::__is_location_invariant.
///
/// TODO: ensure in-situ storage for other standard-library implementations
struct SharedPtrDeleter {
  /// The owned shared object kept alive until disposal.
  mutable std::shared_ptr<void> ts_;
  /// Constructs the deleter taking shared ownership of the given object.
  ///
  /// \param ts The shared object to own.
  explicit SharedPtrDeleter(std::shared_ptr<void> const& ts) noexcept;
  /// Copy constructor.
  ///
  /// \param that The deleter to copy.
  SharedPtrDeleter(SharedPtrDeleter const& that) noexcept;
  /// Copy assignment is disabled.
  ///
  /// \param that The deleter to copy.
  void operator=(SharedPtrDeleter const& that) = delete;
  /// Destroys the deleter and releases its shared ownership.
  ~SharedPtrDeleter();
  /// Disposes the pointed-to object.
  ///
  /// \param ptr The object to dispose.
  /// \param mode The destruction mode in effect.
  void operator()(void* ptr, folly::TLPDestructionMode mode) const;
};

} // namespace threadlocal_detail

} // namespace folly

#if defined(__GLIBCXX__)

namespace std {

template <>
struct __is_location_invariant<::folly::threadlocal_detail::SharedPtrDeleter>
    : std::true_type {};

} // namespace std

#endif

namespace folly {

namespace threadlocal_detail {

struct StaticMetaBase;
struct ThreadEntryList;

/**
 * POD wrapper around an element (a void*) and an associated deleter.
 * This must be POD, as we memset() it to 0 and memcpy() it around.
 */
struct ElementWrapper {
  /// Signature of a raw deleter function.
  using DeleterFunType = void(void*, TLPDestructionMode);
  /// Type-erased deleter callable.
  using DeleterObjType = std::function<DeleterFunType>;

  /// Bit flag marking that the deleter is an object rather than a function.
  static inline constexpr auto deleter_obj_mask = uintptr_t(0b01);
  /// Mask covering all deleter tag bits.
  static inline constexpr auto deleter_all_mask = uintptr_t(0) //
      | deleter_obj_mask //
      ;

  static_assert(alignof(DeleterObjType) > deleter_all_mask);

  /// Reinterprets a deleter function pointer as a tagged integer.
  ///
  /// Must be noinline and must launder: https://godbolt.org/z/bo6f7f6v6
  ///
  /// \param fun The deleter function pointer to convert.
  /// \returns The pointer value as an integer.
  FOLLY_NOINLINE static uintptr_t castForgetAlign(DeleterFunType* fun) noexcept;

  /// Disposes the held element using its deleter.
  ///
  /// \param mode The destruction mode in effect.
  /// \returns True if an element was disposed, false if none was held.
  bool dispose(TLPDestructionMode mode) noexcept {
    if (ptr == nullptr) {
      return false;
    }

    DCHECK_NE(0, deleter);
    auto const deleter_masked = deleter & ~deleter_all_mask;
    if (deleter & deleter_obj_mask) {
      auto& obj = *reinterpret_cast<DeleterObjType*>(deleter_masked);
      obj(ptr, mode);
    } else {
      auto& fun = *reinterpret_cast<DeleterFunType*>(deleter_masked);
      fun(ptr, mode);
    }
    return true;
  }

  /// Releases ownership of the held element without disposing it.
  ///
  /// \returns The previously held pointer.
  void* release() {
    auto retPtr = ptr;

    if (ptr != nullptr) {
      cleanup();
    }

    return retPtr;
  }

  /// Stores a pointer using the default deleter for its type.
  ///
  /// \param p The pointer to store.
  template <class Ptr>
  void set(Ptr p) {
    DCHECK_EQ(static_cast<void*>(nullptr), ptr);
    DCHECK_EQ(0, deleter);

    if (!p) {
      return;
    }
    auto const fun = +[](void* pt, TLPDestructionMode) {
      delete static_cast<Ptr>(pt);
    };
    auto const raw = castForgetAlign(fun);
    if (raw & deleter_all_mask) {
      return set(p, std::ref(*fun));
    }
    DCHECK_EQ(0, raw & deleter_all_mask);
    deleter = raw;
    ptr = p;
  }

  /// Wraps a user deleter into a type-erased deleter callable.
  ///
  /// \param d The user-provided deleter.
  /// \returns A callable that casts the pointer and invokes the deleter.
  template <typename Ptr, typename Deleter>
  static auto makeDeleter(const Deleter& d) {
    return [d](void* pt, TLPDestructionMode mode) {
      d(static_cast<Ptr>(pt), mode);
    };
  }

  /// Returns the shared-pointer deleter unchanged.
  ///
  /// \param d The shared-pointer deleter.
  /// \returns The same deleter.
  template <typename Ptr>
  static decltype(auto) makeDeleter(const SharedPtrDeleter& d) {
    return d;
  }

  /// Stores a pointer together with a custom deleter.
  ///
  /// \param p The pointer to store.
  /// \param d The deleter to invoke on disposal.
  template <class Ptr, class Deleter>
  void set(Ptr p, const Deleter& d) {
    DCHECK_EQ(static_cast<void*>(nullptr), ptr);
    DCHECK_EQ(0, deleter);

    if (!p) {
      return;
    }

    auto guard = makeGuard([&] { d(p, TLPDestructionMode::THIS_THREAD); });
    auto const obj = new DeleterObjType(makeDeleter<Ptr>(d));
    guard.dismiss();
    auto const raw = reinterpret_cast<uintptr_t>(obj);
    DCHECK_EQ(0, raw & deleter_all_mask);
    deleter = raw | deleter_obj_mask;
    ptr = p;
  }

  /// Disposes any object deleter and resets the wrapper to empty.
  void cleanup() noexcept {
    if (deleter & deleter_obj_mask) {
      auto const deleter_masked = deleter & ~deleter_all_mask;
      auto const obj = reinterpret_cast<DeleterObjType*>(deleter_masked);
      delete obj;
    }
    ptr = nullptr;
    deleter = 0;
  }

  /// The stored element pointer.
  void* ptr;
  /// The tagged deleter, either a function pointer or an object pointer.
  uintptr_t deleter;

  /// Constructs an empty wrapper.
  ElementWrapper() : ptr(nullptr), deleter(0) {}
};

/**
 * Per-thread entry.  Each thread using a StaticMeta object has one.
 * This is written from the owning thread only (under the lock), read
 * from the owning thread (no lock necessary), and read from other threads
 * (under the lock).
 */
struct ThreadEntry {
  /// The per-thread array of element wrappers, indexed by entry id.
  ElementWrapper* elements{nullptr};
  /// The current capacity of the elements array.
  std::atomic<size_t> elementsCapacity{0};
  /// The list this entry belongs to.
  ThreadEntryList* list{nullptr};
  /// The next entry in the owning list.
  ThreadEntry* listNext{nullptr};
  /// The StaticMeta instance that owns this entry.
  StaticMetaBase* meta{nullptr};
  /// Whether this entry has been removed.
  bool removed_{false};
  /// The OS-level thread id.
  uint64_t tid_os{};
  /// Storage for the std::thread::id of the owning thread.
  aligned_storage_for_t<std::thread::id> tid_data{};

  /// Returns the current capacity of the elements array.
  ///
  /// \returns The number of element slots currently allocated.
  size_t getElementsCapacity() const noexcept {
    return elementsCapacity.load(std::memory_order_relaxed);
  }

  /// Sets the recorded capacity of the elements array.
  ///
  /// \param capacity The new capacity value.
  void setElementsCapacity(size_t capacity) noexcept {
    elementsCapacity.store(capacity, std::memory_order_relaxed);
  }

  /// Returns the std::thread::id of the owning thread.
  ///
  /// \returns A reference to the owning thread's id.
  std::thread::id& tid() {
    return *reinterpret_cast<std::thread::id*>(&tid_data);
  }

  /// Releases element from ThreadEntry::elements at index @id.
  ///
  /// \param id The index of the element to release.
  /// \returns The released element pointer.
  void* releaseElement(uint32_t id);

  /// Clean up element from ThreadEntry::elements at index @id.
  ///
  /// \param id The index of the element to clean up.
  void cleanupElement(uint32_t id);

  /// Resets the element at index @id without a deleter.
  ///
  /// Templated method for when a deleter is not provided.
  ///
  /// \param p The new pointer to store.
  /// \param id The index of the element to reset.
  template <class Ptr>
  void resetElement(Ptr p, uint32_t id);

  /// Resets the element at index @id with a deleter.
  ///
  /// Templated method for when a deleter is provided.
  ///
  /// \param p The new pointer to store.
  /// \param d The deleter to associate with the element.
  /// \param id The index of the element to reset.
  template <class Ptr, class Deleter>
  void resetElement(Ptr p, Deleter& d, uint32_t id);

  /// Installs a prepared element wrapper at index @id.
  ///
  /// \param element The prepared element wrapper.
  /// \param id The index of the element to set.
  void resetElementImplAfterSet(const ElementWrapper& element, uint32_t id);

  /// Checks that the cached set state matches the elements array for @id.
  ///
  /// \param id The index of the element to check.
  /// \returns True if the cached state is consistent.
  bool cachedInSetMatchesElementsArray(uint32_t id);
};

/// Intrusive list of ThreadEntry objects for a single thread.
struct ThreadEntryList {
  /// The first entry in the list.
  ThreadEntry* head{nullptr};
  /// The number of entries in the list.
  size_t count{0};
};

/**
 * Cache the ptr + deleter info in ThreadEntrySet too. This allows
 * accessAllThreads() to get to the per thread ptr without holding the
 * StaticMeta's lock_. Eventually, the deleter info will be
 * moved to the ThreadEntrySet alone, leaving only the ptr in the
 * ElementWrapper. For now, the ElementDisposeInfo tracked in ThreadEntrySet is
 * the same as ElementWrapper.
 */
using ElementDisposeInfo = ElementWrapper;

/// ThreadEntrySet is used to track all ThreadEntry that have a valid
/// ElementWrapper for a particular TL id. The class provides no internal
/// locking and caller must ensure safety of any access.
struct ThreadEntrySet {
  /// Pairs a thread entry with the dispose info cached for it.
  struct Element {
    /// Cached dispose info for the tracked element.
    ElementDisposeInfo wrapper;
    /// The tracked thread entry.
    ThreadEntry* threadEntry;

    /// Constructs an element, optionally binding a thread entry.
    ///
    /// \param entry The thread entry to track.
    /* implicit */ Element(ThreadEntry* entry = nullptr) : threadEntry(entry) {}
  };

  /// Vector of ThreadEntry for fast iteration during accessAllThreads.
  using ElementVector = std::vector<Element>;
  /// The tracked elements in insertion order.
  ElementVector threadElements;
  /// Map from ThreadEntry* to its slot in the threadElements vector to be able
  /// to remove an entry quickly.
  using EntryIndex = folly::F14FastMap<ThreadEntry*, ElementVector::size_type>;
  /// Index from thread entry to its slot in threadElements.
  EntryIndex entryToVectorSlot;

  /// Checks internal consistency of the set.
  ///
  /// \returns True if the set's invariants hold.
  bool basicSanity() const;

  /// Removes all tracked entries.
  void clear();

  /// Returns the slot index of the given thread entry.
  ///
  /// \param entry The thread entry to look up.
  /// \returns The slot index, or a negative value if not present.
  int64_t getIndexFor(ThreadEntry* entry) const;

  /**
   * Helper function for debugging checks. Fetch ptr for a given ThreadEnrtry.
   * Used to sanity check the value in ElementDisposeInfo wrapper matches the
   * ElementWrapper array used for fast access from the thread itself.
   *
   * \param entry The thread entry to fetch the pointer for.
   * \returns The element pointer tracked for the thread entry.
   */
  void* getPtrForThread(ThreadEntry* entry) const;

  /// Checks whether the given thread entry is tracked.
  ///
  /// \param entry The thread entry to look up.
  /// \returns True if the entry is present.
  bool contains(ThreadEntry* entry) const;

  /// Inserts a thread entry into the set.
  ///
  /// \param entry The thread entry to insert.
  /// \returns True if the entry was newly inserted.
  bool insert(ThreadEntry* entry);

  /// Inserts a fully-formed element into the set.
  ///
  /// \param element The element to insert.
  /// \returns True if the element was newly inserted.
  bool insert(const Element& element);

  /// Removes the given thread entry from the set.
  ///
  /// \param entry The thread entry to remove.
  /// \returns The removed element.
  Element erase(ThreadEntry* entry);

  /// compressible
  ///
  /// If many elements have been removed, then size might be much less than
  /// capacity and it becomes possible to reduce memory usage.
  ///
  /// \returns True if the set can be compressed to save memory.
  bool compressible() const;

  /// compress
  ///
  /// Attempt to reduce the memory usage of the data structure.
  void compress();
};

/// Non-templated base holding the shared thread-local bookkeeping state.
struct StaticMetaBase {
  /// Whether native thread-local storage is used on this platform.
  ///
  /// In general, emutls cleanup is not guaranteed to play nice with the way
  /// StaticMeta mixes direct pthread calls and the use of __thread. This has
  /// caused problems on multiple platforms so don't use __thread there.
  ///
  /// XXX: Ideally we would instead determine if emutls is in use at runtime as
  /// it is possible to configure glibc on Linux to use emutls regardless.
  static constexpr bool kUseThreadLocal = !kIsMobile && !kIsApple && !kMscVer;

  /// Represents an ID of a thread local object. Initially set to the maximum
  /// uint. This representation allows us to avoid a branch in accessing TLS data
  /// (because if you test capacity > id if id = maxint then the test will always
  /// fail). It allows us to keep a constexpr constructor and avoid SIOF.
  class EntryID {
   public:
    /// The allocated slot id, or kEntryIDInvalid if unallocated.
    std::atomic<uint32_t> value;

    /// Constructs an unallocated entry id.
    constexpr EntryID() : value(kEntryIDInvalid) {}

    /// Move constructor; leaves the source unallocated.
    ///
    /// \param other The entry id to move from.
    EntryID(EntryID&& other) noexcept : value(other.value.load()) {
      other.value = kEntryIDInvalid;
    }

    /// Move assignment; leaves the source unallocated.
    ///
    /// \param other The entry id to move from.
    /// \returns A reference to this entry id.
    EntryID& operator=(EntryID&& other) noexcept {
      assert(this != &other);
      DCHECK(value.load() == kEntryIDInvalid);
      value = other.value.load();
      other.value = kEntryIDInvalid;
      return *this;
    }

    /// Copy construction is disabled.
    ///
    /// \param other The entry id to copy.
    EntryID(const EntryID& other) = delete;
    /// Copy assignment is disabled.
    ///
    /// \param other The entry id to copy.
    /// \returns A reference to this entry id.
    EntryID& operator=(const EntryID& other) = delete;

    /// Returns the current id without allocating.
    ///
    /// \returns The allocated id, or kEntryIDInvalid if unallocated.
    uint32_t getOrInvalid() { return value.load(std::memory_order_acquire); }

    /// Returns the current id, allocating one if needed.
    ///
    /// \param meta The owning StaticMetaBase used to allocate an id.
    /// \returns The allocated id.
    uint32_t getOrAllocate(StaticMetaBase& meta) {
      uint32_t id = getOrInvalid();
      if (id != kEntryIDInvalid) {
        return id;
      }
      // The lock inside allocate ensures that a single value is allocated
      return meta.allocate(this);
    }
  };

  /// Constructs the base with a thread-entry accessor and access mode.
  ///
  /// \param threadEntry Function returning the calling thread's entry.
  /// \param strict Whether strict access mode is enabled.
  StaticMetaBase(ThreadEntry* (*threadEntry)(), bool strict);

  /// Returns the calling thread's list of thread entries.
  ///
  /// \returns The per-thread ThreadEntryList.
  FOLLY_EXPORT static ThreadEntryList* getThreadEntryList();

  /// Allocates a new thread entry for the calling thread.
  ///
  /// \returns The newly allocated thread entry.
  ThreadEntry* allocateNewThreadEntry();

  /// Reports whether the process is in thread-local teardown.
  ///
  /// \returns True if thread-local objects are being destroyed at exit.
  static bool dying();

  /// Thread-exit callback that disposes the thread's entries.
  ///
  /// \param ptr The thread entry being torn down.
  static void onThreadExit(void* ptr);

  /// Helper to do final free and delete of ThreadEntry and ThreadEntryList
  /// structures.
  ///
  /// \param list The thread entry list to free.
  static void cleanupThreadEntriesAndList(ThreadEntryList* list);

  /// Returns the elementsCapacity for the current thread ThreadEntry struct.
  ///
  /// \returns The current thread's element capacity.
  uint32_t elementsCapacity() const;

  /// Allocates a slot id for the given entry.
  ///
  /// \param ent The entry to allocate an id for.
  /// \returns The allocated id.
  uint32_t allocate(EntryID* ent);

  /// Frees the slot id owned by the given entry.
  ///
  /// \param ent The entry to destroy.
  void destroy(EntryID* ent);

  /**
   * Reserve enough space in the ThreadEntry::elements for the item
   * @id to fit in.
   *
   * \param id The entry whose slot must be reserved.
   */
  void reserve(EntryID* id);

  /// Returns the element wrapper for the given entry on the current thread.
  ///
  /// \param ent The entry to look up.
  /// \returns A reference to the entry's element wrapper.
  ElementWrapper& getElement(EntryID* ent);

  /// A ThreadEntrySet guarded by a synchronization wrapper.
  using SynchronizedThreadEntrySet = folly::Synchronized<ThreadEntrySet>;

  /*
   * Helper inline methods to add/remove/clear ThreadEntry* from
   * allId2ThreadEntrySets_
   */

  /// Return true if given ThreadEntry is already present in the ThreadEntrySet
  /// for the given id.
  ///
  /// \param te The thread entry to look up.
  /// \param id The slot id whose set is queried.
  /// \returns True if the entry is present in the set.
  FOLLY_ALWAYS_INLINE bool isThreadEntryInSet(ThreadEntry* te, uint32_t id) {
    return allId2ThreadEntrySets_[id].rlock()->contains(te);
  }

  /// Ensure the given ThreadEntry* is present in the tracking set for the
  /// given id. Once added, we do not remove it until the thread exits or the
  /// whole set is reaped when the TL id itself is destroyed.
  ///
  /// Note: Call may drop and reacquire the read lock.
  /// If the provided entry is not already in the set, the given RLockedPtr will
  /// be released, entry added under a WLockedPtr, and RLockedPtr reacquired
  /// before returning.
  ///
  /// \param te The thread entry to add.
  /// \param set The synchronized set to add the entry to.
  /// \param rlock The held read lock, possibly dropped and reacquired.
  FOLLY_NOINLINE void ensureThreadEntryIsInSet(
      ThreadEntry* te,
      SynchronizedThreadEntrySet& set,
      SynchronizedThreadEntrySet::RLockedPtr& rlock);

  /// Remove a ThreadEntry* from the map of allId2ThreadEntrySets_
  /// for all slot @id's in ThreadEntry::elements that are
  /// used. This is essentially clearing out a ThreadEntry entirely
  /// from the allId2ThreadEntrySets_.
  ///
  /// \param te The thread entry to remove from all sets.
  FOLLY_ALWAYS_INLINE void removeThreadEntryFromAllInMap(ThreadEntry* te) {
    for (const auto ptr : getThreadEntrySetsPtrSpan()) {
      auto& set = *ptr;
      set.wlock()->erase(te);
    }
  }

  /// Pop current ThreadEntrySet and for each ThreadEntry in it, clear its
  /// ElementWrapper for the 'id' and return them in the accumulated vector. This
  /// is called when an TL object is destroyed. The ElementWrapper returned are
  /// the responsibility of the calling thread to dispose of.
  ///
  /// \param id The slot id whose set is popped and cleared.
  /// \returns The set of entries whose elements were cleared.
  ThreadEntrySet popThreadEntrySetAndClearElementPtrs(uint32_t id);

  /// Check if ThreadEntry* is present in the map for all slots of @ids.
  ///
  /// \param te The thread entry to check.
  /// \param needForkLock Whether the fork handler lock must be taken.
  /// \returns True if the entry is absent from every set.
  FOLLY_ALWAYS_INLINE bool isThreadEntryRemovedFromAllInMap(
      ThreadEntry* te, bool needForkLock) {
    std::shared_lock rlocked(forkHandlerLock_, std::defer_lock);
    if (needForkLock) {
      rlocked.lock();
    }
    for (const auto ptr : getThreadEntrySetsPtrSpan()) {
      auto& set = *ptr;
      if (set.rlock()->contains(te)) {
        return false;
      }
    }
    return true;
  }

  /// Static helper method to reallocate the ThreadEntry::elements.
  ///
  /// Returns != nullptr if the ThreadEntry::elements was reallocated,
  /// nullptr if the ThreadEntry::elements was just extended,
  /// and throws std::bad_alloc if memory cannot be allocated.
  ///
  /// \param threadEntry The thread entry whose elements are reallocated.
  /// \param idval The slot id that must fit after reallocation.
  /// \param newCapacity In/out capacity, updated to the new capacity.
  /// \returns The reallocated array, or nullptr if only extended.
  static ElementWrapper* reallocate(
      ThreadEntry* threadEntry, uint32_t idval, size_t& newCapacity);

  /// Returns a span over the pointers to all active thread entry sets.
  ///
  /// \returns A span of pointers to the currently allocated sets.
  std::span<SynchronizedThreadEntrySet* const> getThreadEntrySetsPtrSpan() {
    return allId2ThreadEntrySets_.as_view().as_ptr_span(nextId_.load());
  }

  /// The next slot id to allocate.
  relaxed_atomic_uint32_t nextId_;
  /// The pool of freed slot ids available for reuse.
  std::vector<uint32_t> freeIds_;
  /// The lock_ is used to protect the freeIds_ list as well as synchronize
  /// reallocation of a thread's private array of ElementWrappers. The freeIds_
  /// vector is manipulated on TL object id allocation and destroy. Resize of
  /// ElementWrappers array can only be done by its owner thread but other
  /// threads may try to be accessing the array at the same time if in the middle
  /// of destroying a TL object.
  std::mutex lock_;
  /// Guards accessAllThreads() against concurrent per-thread resets.
  mutable SharedMutex accessAllThreadsLock_;
  // As part of handling fork, we need to ensure no locks used by ThreadLocal
  // implementation are held by threads other than the one forking. The total
  // number of locks involved is large due to the per ThreadEntrySet lock. TSAN
  // builds have to track each lock acquire and release. TSAN also has its own
  // fork handler. Using a lot of locks in fork handler can end up deadlocking
  // TSAN. To avoid that behavior, we the forkHandlerLock_. All code paths that
  // acquire a lock on any ThreadEntrySet (accessAllThreads() or reset() calls)
  // must also acquire a shared lock on forkHandlerLock_.
  // Fork handler will acquire an exclusive lock on forkHandlerLock_,
  // along with exclusive locks on accessAllThreadsLock_ and lock_.
  /// Serializes fork handling against all thread-entry-set locking.
  mutable SharedMutex forkHandlerLock_;
  /// The pthread key backing per-thread entry storage.
  pthread_key_t pthreadKey_;
  /// Function returning the calling thread's entry.
  ThreadEntry* (*threadEntry_)();
  /// Whether strict access mode is enabled.
  bool strict_;
  /// Total size of ElementWrapper arrays across all threads. This is meant
  /// to surface the overhead of thread local tracking machinery since the array
  /// can be sparse when there are lots of thread local variables under the same
  /// tag.
  relaxed_atomic_int64_t totalElementWrappers_{0};
  /// This is a map of all thread entries mapped to index i with active
  /// elements[i];
  folly::atomic_grow_array<SynchronizedThreadEntrySet> allId2ThreadEntrySets_;

  // Note on locking rules. There are 4 locks involved in managing StaticMeta:
  // fork handler lock (getStaticMetaGlobalForkMutex(),
  // access all threads lock (accessAllThreadsLock_),
  // per thread entry set lock implicit in SynchronizedThreadEntrySet and
  // meta lock (lock_)
  //
  // If multiple locks need to be acquired in a call path, the above is also
  // the order in which they should be acquired. Additionally, if per
  // ThreadEntrySet locks are the only ones that are acquired in a path, it
  // must also acquire shared lock on the fork handler lock.
};

/// No-op stand-in for UniqueInstance used when the tag is void.
struct FakeUniqueInstance {
  /// Constructs the stand-in, ignoring all tag arguments.
  ///
  /// \param typeTag Tag identifying the instantiated template.
  /// \param keyTag Tag carrying the key types.
  /// \param mappedTag Tag carrying the mapped types.
  template <template <typename...> class Z, typename... Key, typename... Mapped>
  FOLLY_ERASE constexpr explicit FakeUniqueInstance(
      tag_t<Z<Key..., Mapped...>> typeTag,
      tag_t<Key...> keyTag,
      tag_t<Mapped...> mappedTag) noexcept {}
};

/*
 * Resets element from ThreadEntry::elements at index @id.
 * call set() on the element to reset it.
 * This is a templated method for when a deleter is not provided.
 */
template <class Ptr>
void ThreadEntry::resetElement(Ptr p, uint32_t id) {
  ElementWrapper element;
  element.set(p);
  resetElementImplAfterSet(element, id);
}

/*
 * Resets element from ThreadEntry::elements at index @id.
 * call set() on the element to reset it.
 * This is a templated method for when a deleter is provided.
 */
template <class Ptr, class Deleter>
void ThreadEntry::resetElement(Ptr p, Deleter& d, uint32_t id) {
  ElementWrapper element;
  element.set(p, d);
  resetElementImplAfterSet(element, id);
}

/// Held in a singleton to track our global instances.
/// We have one of these per "Tag", by default one for the whole system
/// (Tag=void).
///
/// Creating and destroying ThreadLocalPtr objects, as well as thread exit
/// for threads that use ThreadLocalPtr objects collide on a lock inside
/// StaticMeta; you can specify multiple Tag types to break that lock.
template <class Tag, class AccessMode>
struct FOLLY_EXPORT StaticMeta final : StaticMetaBase {
 private:
  static constexpr bool IsTagVoid = std::is_void_v<Tag>;
  static constexpr bool IsAccessModeStrict =
      std::is_same_v<AccessMode, AccessModeStrict>;
  static_assert(!IsTagVoid || !IsAccessModeStrict);

  using UniqueInstance =
      conditional_t<IsTagVoid, FakeUniqueInstance, detail::UniqueInstance>;
  static UniqueInstance unique;

 public:
  /// Constructs the singleton and registers its fork handlers.
  StaticMeta()
      : StaticMetaBase(&StaticMeta::getThreadEntrySlow, IsAccessModeStrict) {
    AtFork::registerHandler(
        this,
        /*prepare*/ &StaticMeta::preFork,
        /*parent*/ &StaticMeta::onForkParent,
        /*child*/ &StaticMeta::onForkChild);
  }

  /// Returns the process-wide singleton for this tag and access mode.
  ///
  /// \returns The unique StaticMeta instance.
  static StaticMeta<Tag, AccessMode>& instance() {
    (void)unique; // force the object not to be thrown out as unused
    // Leak it on exit, there's only one per process and we don't have to
    // worry about synchronization with exiting threads.
    return detail::createGlobal<StaticMeta<Tag, AccessMode>, void>();
  }

  /// Per-thread cache of the current thread entry and its capacity.
  struct LocalCache {
    /// The cached thread entry.
    ThreadEntry* threadEntry;
    /// The cached element capacity.
    size_t capacity;
  };
  static_assert(std::is_standard_layout_v<LocalCache>);
  static_assert(std::is_trivial_v<LocalCache>);

  /// Returns the calling thread's local cache.
  ///
  /// \returns A reference to the thread-local cache.
  FOLLY_EXPORT FOLLY_ALWAYS_INLINE static LocalCache& getLocalCache() {
    static thread_local LocalCache instance;
    return instance;
  }

  /// Returns the element wrapper for the given entry on the current thread.
  ///
  /// \param ent The entry to look up.
  /// \returns A reference to the entry's element wrapper.
  FOLLY_ALWAYS_INLINE static ElementWrapper& get(EntryID* ent) {
    // Eliminate as many branches and as much extra code as possible in the
    // cached fast path, leaving only one branch here and one indirection
    // below.

    ThreadEntry* te = getThreadEntry(ent);
    uint32_t id = ent->getOrInvalid();
    // Only valid index into the the elements array
    DCHECK_NE(id, kEntryIDInvalid);
    DCHECK(te->cachedInSetMatchesElementsArray(id));
    return te->elements[id];
  }

  /// Returns the calling thread's entry for the given id.
  ///
  /// In order to facilitate adding/clearing ThreadEntry* to
  /// StaticMetaBase::allId2ThreadEntrySets_ during ThreadLocalPtr
  /// reset()/release() we need access to the ThreadEntry* directly. This allows
  /// for direct interaction with StaticMetaBase::allId2ThreadEntrySets_. We keep
  /// StaticMetaBase::allId2ThreadEntrySets_ updated with ThreadEntry* whenever a
  /// ThreadLocal is set/released.
  ///
  /// \param ent The entry to resolve.
  /// \returns The current thread's entry.
  FOLLY_ALWAYS_INLINE static ThreadEntry* getThreadEntry(EntryID* ent) {
    if (!kUseThreadLocal) {
      return getThreadEntrySlowReserve(ent);
    }

    // Eliminate as many branches and as much extra code as possible in the
    // cached fast path, leaving only one branch here and one indirection below.
    uint32_t id = ent->getOrInvalid();
    auto& cache = getLocalCache();
    if (FOLLY_UNLIKELY(cache.capacity <= id)) {
      getSlowReserveAndCache(ent, cache);
    }
    return cache.threadEntry;
  }

  /// Reserves the entry's slot and refreshes the local cache.
  ///
  /// \param ent The entry to reserve.
  /// \param cache The local cache to update.
  FOLLY_NOINLINE static void getSlowReserveAndCache(
      EntryID* ent, LocalCache& cache) {
    auto threadEntry = getThreadEntrySlowReserve(ent);
    cache.capacity = threadEntry->getElementsCapacity();
    cache.threadEntry = threadEntry;
  }

  /// Returns the thread entry, reserving capacity for the entry's slot.
  ///
  /// \param ent The entry to reserve.
  /// \returns The current thread's entry with sufficient capacity.
  FOLLY_NOINLINE static ThreadEntry* getThreadEntrySlowReserve(EntryID* ent) {
    uint32_t id = ent->getOrInvalid();

    auto& inst = instance();
    auto threadEntry = inst.threadEntry_();
    if (FOLLY_UNLIKELY(threadEntry->getElementsCapacity() <= id)) {
      inst.reserve(ent);
      id = ent->getOrInvalid();
    }
    assert(threadEntry->getElementsCapacity() > id);
    return threadEntry;
  }

  /// Returns the calling thread's entry, allocating it on first use.
  ///
  /// \returns The current thread's entry.
  FOLLY_EXPORT FOLLY_NOINLINE static ThreadEntry* getThreadEntrySlow() {
    auto& meta = instance();
    auto key = meta.pthreadKey_;
    ThreadEntry* threadEntry =
        static_cast<ThreadEntry*>(pthread_getspecific(key));
    if (!threadEntry) {
      threadEntry = meta.allocateNewThreadEntry();
      int ret = pthread_setspecific(key, threadEntry);
      checkPosixError(ret, "pthread_setspecific failed");
    }
    return threadEntry;
  }

  /// Fork prepare handler; acquires the locks needed across fork.
  ///
  /// \returns True if all required locks were acquired.
  [[FOLLY_ATTR_CLANG_NO_THREAD_SAFETY_ANALYSIS]]
  static bool preFork() {
    auto& meta = instance();
    bool gotLock = meta.forkHandlerLock_.try_lock(); // Make sure it's created
    if (!gotLock) {
      return false;
    }
    meta.accessAllThreadsLock_.lock();
    meta.lock_.lock();
    // Okay to not lock each set in meta.allId2ThreadEntrySets
    // as accessAllThreadsLock_ in held by calls to reset() and
    // accessAllThreads.
    return true;
  }

  /// Fork parent handler; releases the locks taken in preFork().
  [[FOLLY_ATTR_CLANG_NO_THREAD_SAFETY_ANALYSIS]]
  static void onForkParent() {
    auto& meta = instance();
    meta.lock_.unlock();
    meta.accessAllThreadsLock_.unlock();
    meta.forkHandlerLock_.unlock();
  }

  /// Fork child handler; keeps only the surviving thread's entries.
  [[FOLLY_ATTR_CLANG_NO_THREAD_SAFETY_ANALYSIS]]
  static void onForkChild() {
    auto& meta = instance();
    // only the current thread survives
    meta.lock_.unlock();
    meta.accessAllThreadsLock_.unlock();
    auto threadEntry = meta.threadEntry_();
    // Loop through allId2ThreadEntrySets_; Only keep ThreadEntry* in the map
    // for ThreadEntry::elements that are still in use by the current thread.
    // Evict all of the ThreadEntry* from other threads.
    for (const auto ptr : meta.getThreadEntrySetsPtrSpan()) {
      auto& set = *ptr;
      auto wlockedSet = set.wlock();
      auto slot = wlockedSet->getIndexFor(threadEntry);
      if (slot >= 0) {
        auto element = wlockedSet->threadElements[slot];
        wlockedSet->clear();
        wlockedSet->insert(element);
      } else {
        wlockedSet->clear();
      }
    }
    meta.forkHandlerLock_.unlock();
  }
};

FOLLY_PUSH_WARNING
FOLLY_CLANG_DISABLE_WARNING("-Wglobal-constructors")
template <typename Tag, typename AccessMode>
typename StaticMeta<Tag, AccessMode>::UniqueInstance
    StaticMeta<Tag, AccessMode>::unique{
        tag<StaticMeta>, tag<Tag>, tag<AccessMode>};
FOLLY_POP_WARNING

} // namespace threadlocal_detail
} // namespace folly
