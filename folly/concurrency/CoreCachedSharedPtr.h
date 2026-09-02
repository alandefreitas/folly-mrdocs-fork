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

#include <array>
#include <atomic>
#include <memory>

#include <folly/CppAttributes.h>
#include <folly/Portability.h>
#include <folly/Unit.h>
#include <folly/concurrency/CacheLocality.h>
#include <folly/synchronization/Hazptr.h>

namespace folly {

/// Default maximum number of core-local slots used by the cached pointers.
///
/// On mobile we do not expect high concurrency, and memory is more important, so
/// use more conservative caching.
constexpr size_t kCoreCachedSharedPtrDefaultMaxSlots = kIsMobile ? 4 : 64;

/// Implementation details for the core-cached shared pointer classes.
///
/// \implementationdefined
namespace core_cached_shared_ptr_detail {

template <size_t kMaxSlots>
class SlotsConfig {
 public:
  FOLLY_EXPORT static void initialize() {
    [[maybe_unused]] static const Unit _ = [] {
      // We need at most as many slots as the number of L1 caches, so we can
      // avoid wasting memory if more slots are requested.
      const auto l1Caches = CacheLocality::system().numCachesByLevel.front();
      num_ = std::min(std::max<size_t>(1, l1Caches), kMaxSlots);
      return unit;
    }();
  }

  static size_t num() { return num_.load(std::memory_order_relaxed); }

 private:
  static std::atomic<size_t> num_;
};

// Initialize with a valid num so that get() always returns a valid stripe, even
// if initialize() has not been called yet.
template <size_t kMaxSlots>
std::atomic<size_t> SlotsConfig<kMaxSlots>::num_{1};

template <size_t kMaxSlots, class T>
void makeSlots(std::shared_ptr<T> p, folly::Range<std::shared_ptr<T>*> slots) {
  // Allocate each holder and its control block in a different CoreAllocator
  // stripe to prevent false sharing.
  for (size_t i = 0; i < slots.size(); ++i) {
    CoreAllocatorGuard guard(slots.size(), i);
    auto holder = std::allocate_shared<std::shared_ptr<T>>(
        CoreAllocator<std::shared_ptr<T>>{});
    auto ptr = p.get();
    if (i != slots.size() - 1) {
      *holder = p;
    } else {
      *holder = std::move(p);
    }
    slots[i] = std::shared_ptr<T>(std::move(holder), ptr);
  }
}

// Check whether a shared_ptr is equivalent to default-constructed. Because of
// aliasing constructors, there can be both nullptr with a managed object, and
// non-nullptr with no managed object, so we need to check both.
template <class T>
bool isDefault(const std::shared_ptr<T>& p) {
  return p == nullptr && p.use_count() == 0;
}

} // namespace core_cached_shared_ptr_detail

/**
 * This class creates core-local caches for a given shared_ptr, to
 * mitigate contention when acquiring/releasing it.
 *
 * It has the same thread-safety guarantees as shared_ptr: it is safe
 * to concurrently call get(), but reset()s must be synchronized with
 * reads and other reset()s.
 */
template <class T, size_t kMaxSlots = kCoreCachedSharedPtrDefaultMaxSlots>
class CoreCachedSharedPtr {
  using SlotsConfig = core_cached_shared_ptr_detail::SlotsConfig<kMaxSlots>;

 public:
  /// Constructs an empty core-cached shared pointer.
  CoreCachedSharedPtr() = default;
  /// Constructs a core-cached shared pointer holding the given pointer.
  ///
  /// \param p The shared pointer to cache across cores.
  explicit CoreCachedSharedPtr(std::shared_ptr<T> p) { reset(std::move(p)); }

  /// Replaces the held pointer, repopulating the core-local caches.
  ///
  /// \param p The new shared pointer to cache, or null to clear.
  void reset(std::shared_ptr<T> p = nullptr) {
    SlotsConfig::initialize();

    folly::Range<std::shared_ptr<T>*> slots{slots_.data(), SlotsConfig::num()};
    for (auto& slot : slots) {
      slot = {};
    }
    if (!core_cached_shared_ptr_detail::isDefault(p)) {
      core_cached_shared_ptr_detail::makeSlots<kMaxSlots>(std::move(p), slots);
    }
  }

  /// Returns the shared pointer cached for the current core.
  ///
  /// \returns A copy of the held shared pointer.
  std::shared_ptr<T> get() const {
    return slots_[AccessSpreader<>::cachedCurrent(SlotsConfig::num())];
  }

 private:
  template <class, size_t>
  friend class CoreCachedWeakPtr;

  std::array<std::shared_ptr<T>, kMaxSlots> slots_;
};

/// Core-local cache of weak pointers to a shared object.
///
/// Mirrors the core-local sharding of CoreCachedSharedPtr for weak
/// references, reducing contention when observing the pointer.
template <class T, size_t kMaxSlots = kCoreCachedSharedPtrDefaultMaxSlots>
class CoreCachedWeakPtr {
  using SlotsConfig = core_cached_shared_ptr_detail::SlotsConfig<kMaxSlots>;

 public:
  /// Constructs an empty core-cached weak pointer.
  CoreCachedWeakPtr() = default;
  /// Constructs a core-cached weak pointer observing the given cached pointer.
  ///
  /// \param p The core-cached shared pointer to observe.
  explicit CoreCachedWeakPtr(const CoreCachedSharedPtr<T, kMaxSlots>& p) {
    reset(p);
  }

  /// Clears all core-local weak references.
  void reset() { *this = {}; }
  /// Repopulates the core-local weak references from a cached shared pointer.
  ///
  /// \param p The core-cached shared pointer to observe.
  void reset(const CoreCachedSharedPtr<T, kMaxSlots>& p) {
    SlotsConfig::initialize();
    for (size_t i = 0; i < SlotsConfig::num(); ++i) {
      slots_[i] = p.slots_[i];
    }
  }

  /// Returns the weak pointer cached for the current core.
  ///
  /// \returns A copy of the held weak pointer.
  std::weak_ptr<T> get() const {
    return slots_[AccessSpreader<>::cachedCurrent(SlotsConfig::num())];
  }

  /// Locks the core-local weak pointer into a shared pointer.
  ///
  /// Faster than get().lock(), as it avoids one weak count cycle.
  ///
  /// \returns A shared pointer to the object, or null if it has expired.
  std::shared_ptr<T> lock() const {
    return slots_[AccessSpreader<>::cachedCurrent(SlotsConfig::num())].lock();
  }

 private:
  std::array<std::weak_ptr<T>, kMaxSlots> slots_;
};

/**
 * This class creates core-local caches for a given shared_ptr, to
 * mitigate contention when acquiring/releasing it.
 *
 * All methods are threadsafe.  Hazard pointers are used to avoid
 * use-after-free for concurrent reset() and get() operations.
 *
 * Concurrent reset()s are sequenced with respect to each other: the
 * sharded shared_ptrs will always all be set to the same value.
 * get()s will never see a newer pointer on one core, and an older
 * pointer on another after a subsequent thread migration.
 */
template <class T, size_t kMaxSlots = kCoreCachedSharedPtrDefaultMaxSlots>
class AtomicCoreCachedSharedPtr {
  using SlotsConfig = core_cached_shared_ptr_detail::SlotsConfig<kMaxSlots>;

 public:
  /// Constructs an empty atomic core-cached shared pointer.
  AtomicCoreCachedSharedPtr() = default;
  /// Constructs an atomic core-cached shared pointer holding the given pointer.
  ///
  /// \param p The shared pointer to cache across cores.
  explicit AtomicCoreCachedSharedPtr(std::shared_ptr<T> p) {
    reset(std::move(p));
  }

  /// Move-constructs from another instance, leaving it empty.
  ///
  /// \param other The instance to move from.
  AtomicCoreCachedSharedPtr(AtomicCoreCachedSharedPtr&& other) noexcept
      : slots_(other.slots_.load(std::memory_order_relaxed)) {
    other.slots_.store(nullptr, std::memory_order_relaxed);
  }
  /// Move assignment is deleted.
  ///
  /// \param other The instance to move from.
  /// \returns A reference to this instance.
  AtomicCoreCachedSharedPtr& operator=(AtomicCoreCachedSharedPtr&& other) =
      delete;

  /// Destroys the pointer and releases the core-local caches.
  ~AtomicCoreCachedSharedPtr() {
    // Delete of AtomicCoreCachedSharedPtr must be synchronized, no
    // need for slots->retire().
    delete slots_.load(std::memory_order_acquire);
  }

  /// Atomically replaces the held pointer, repopulating the core-local caches.
  ///
  /// \param p The new shared pointer to cache, or null to clear.
  void reset(std::shared_ptr<T> p = nullptr) {
    SlotsConfig::initialize();
    std::unique_ptr<Slots> newslots;
    if (!core_cached_shared_ptr_detail::isDefault(p)) {
      newslots = std::make_unique<Slots>();
      core_cached_shared_ptr_detail::makeSlots<kMaxSlots>(
          std::move(p), {newslots->slots.data(), SlotsConfig::num()});
    }

    if (auto oldslots = slots_.exchange(newslots.release())) {
      oldslots->retire();
    }
  }

  /// Returns the shared pointer cached for the current core.
  ///
  /// \returns A copy of the held shared pointer, or null if empty.
  std::shared_ptr<T> get() const {
    // Avoid the hazptr cost if empty.
    auto slots = slots_.load(std::memory_order_relaxed);
    if (slots == nullptr) {
      return nullptr;
    }

    folly::hazptr_local<1> hazptr;
    while (!hazptr[0].try_protect(slots, slots_)) {
      // Lost the update race, retry.
    }
    if (slots == nullptr) { // Need to check again, try_protect reloads slots.
      return nullptr;
    }
    return slots->slots[AccessSpreader<>::cachedCurrent(SlotsConfig::num())];
  }

 private:
  struct Slots : folly::hazptr_obj_base<Slots> {
    std::array<std::shared_ptr<T>, kMaxSlots> slots;
  };
  std::atomic<Slots*> slots_{nullptr};
};

} // namespace folly
