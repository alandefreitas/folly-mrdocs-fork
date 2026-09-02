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

#include <type_traits>

#include <glog/logging.h>

#include <folly/Portability.h>
#include <folly/synchronization/SmallLocks.h>

#if FOLLY_X64 || FOLLY_PPC64 || FOLLY_AARCH64
#define FOLLY_HAS_PACKED_SYNC_PTR 1
#else
#define FOLLY_HAS_PACKED_SYNC_PTR 0
#endif

#if FOLLY_HAS_PACKED_SYNC_PTR

/*
 * An 8-byte pointer with an integrated spin lock and 15-bit integer
 * (you can use this for a size of the allocation, if you want, or
 * something else, or nothing).
 *
 * This is using an x64-specific detail about the effective virtual
 * address space.  Long story short: the upper two bytes of all our
 * pointers will be zero in reality---and if you have a couple billion
 * such pointers in core, it makes pretty good sense to try to make
 * use of that memory.  The exact details can be perused here:
 *
 *    http://en.wikipedia.org/wiki/X86-64#Canonical_form_addresses
 *
 * This is not a "smart" pointer: nothing automagical is going on
 * here.  Locking is up to the user.  Resource deallocation is up to
 * the user.  Locks are never acquired or released outside explicit
 * calls to lock() and unlock().
 *
 * Change the value of the raw pointer with set(), but you must hold
 * the lock when calling this function if multiple threads could be
 * using this class.
 *
 * TODO(jdelong): should we use the low order bit for the lock, so we
 * get a whole 16-bits for our integer?  (There's also 2 more bits
 * down there if the pointer comes from malloc.)
 */

namespace folly {

/// An 8-byte pointer with an integrated spin lock and 15-bit integer.
///
/// This packs a raw pointer, a spin lock, and 15 bits of extra storage into a
/// single 8-byte word by reusing the always-zero upper bytes of a 64-bit
/// virtual address. It is not a smart pointer: locking and deallocation are up
/// to the user, and locks are only acquired or released through explicit
/// `lock()` and `unlock()` calls.
template <class T>
class PackedSyncPtr {
  // This just allows using this class even with T=void.  Attempting
  // to use the operator* or operator[] on a PackedSyncPtr<void> will
  // still properly result in a compile error.
  using reference = typename std::add_lvalue_reference<T>::type;

 public:
  /// Initializes a default-constructed instance before first use.
  ///
  /// A constructor is intentionally avoided so the class can be placed in
  /// packed structures, so this must be called before any other member.
  ///
  /// @param initialPtr The initial raw pointer to store.
  /// @param initialExtra The initial 15-bit extra value to store.
  void init(T* initialPtr = nullptr, uint16_t initialExtra = 0) {
    auto intPtr = reinterpret_cast<uintptr_t>(initialPtr);
    CHECK(!(intPtr >> 48));
    data_.init(intPtr << 16);
    setExtra(initialExtra);
  }

  /// Sets a new raw pointer.
  ///
  /// You must hold the lock when calling this, or otherwise guarantee that no
  /// other thread could be using this instance.
  ///
  /// @param t The new raw pointer to store.
  void set(T* t) {
    auto intPtr = reinterpret_cast<uintptr_t>(t);
    CHECK(!(intPtr >> 48));
    data_.setData((intPtr << 16) | uintptr_t(extra()));
  }

  /// Returns the stored raw pointer.
  ///
  /// This may be called without holding the lock, with the usual behavior of
  /// reading a pointer without locking.
  ///
  /// @return The stored raw pointer.
  T* get() const { return reinterpret_cast<T*>(data_.getData() >> 16); }
  /// Returns the stored raw pointer for member access.
  ///
  /// @return The stored raw pointer.
  T* operator->() const { return get(); }
  /// Dereferences the stored raw pointer.
  ///
  /// @return A reference to the pointed-to object.
  reference operator*() const { return *get(); }
  /// Accesses an element relative to the stored raw pointer.
  ///
  /// @param i The index offset from the stored pointer.
  /// @return A reference to the element at the given offset.
  reference operator[](std::ptrdiff_t i) const { return get()[i]; }

  // Synchronization (logically const, even though this mutates our
  // locked state: you can lock a const PackedSyncPtr<T> to read it).

  /// Acquires the integrated spin lock, blocking until it is held.
  void lock() const { data_.lock(); }
  /// Releases the integrated spin lock.
  void unlock() const { data_.unlock(); }
  /// Attempts to acquire the integrated spin lock without blocking.
  ///
  /// @return `true` if the lock was acquired, `false` otherwise.
  bool try_lock() const { return data_.try_lock(); }

  /// Returns the extra data stored in the unused bytes of the pointer.
  ///
  /// This may be called without holding the lock.
  ///
  /// @return The stored 15-bit extra value.
  uint16_t extra() const { return data_.getData() & 0xffff; }

  /// Stores extra data in the unused bytes of the pointer.
  ///
  /// The high bit must not be set, since it is reserved for the mutex, and the
  /// lock must be held when calling this.
  ///
  /// @param extra The 15-bit extra value to store.
  void setExtra(uint16_t extra) {
    CHECK(!(extra & 0x8000));
    auto ptr = data_.getData();
    data_.setData(uintptr_t(extra) | (ptr & (-1ull << 16)));
  }

 private:
  PicoSpinLock<uintptr_t, 15> data_;
};

static_assert(
    std::is_standard_layout<PackedSyncPtr<void>>::value &&
        std::is_trivial<PackedSyncPtr<void>>::value,
    "PackedSyncPtr must be kept a POD type.");
static_assert(
    sizeof(PackedSyncPtr<void>) == 8,
    "PackedSyncPtr should be only 8 bytes---something is "
    "messed up");

/// Writes a `PackedSyncPtr` to an output stream as `PackedSyncPtr(ptr, extra)`.
///
/// @param os The output stream to write to.
/// @param ptr The pointer to format.
/// @return The output stream `os`.
template <typename T>
std::ostream& operator<<(std::ostream& os, const PackedSyncPtr<T>& ptr) {
  os << "PackedSyncPtr(" << ptr.get() << ", " << ptr.extra() << ")";
  return os;
}

} // namespace folly

#endif
