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

#include <cassert>
#include <cstdint>

#include <folly/Optional.h>
#include <folly/Portability.h>
#include <folly/Utility.h>
#include <folly/synchronization/AtomicNotification.h>
#include <folly/synchronization/AtomicRef.h>

namespace folly {

/**
 * Tiny exclusive lock that uses 2 bits. It is stored as 1 byte and
 * has APIs for using the remaining 6 bits for storing user data.
 *
 * If you're not space-constrained, prefer std::mutex, which will
 * likely be faster, since it has more than two bits of information to
 * work with.
 *
 * You are free to put a MicroLock in a union with some other object.
 * If, for example, you want to use the bottom two bits of a pointer
 * as a lock, you can put a MicroLock in a union with the pointer,
 * which will use the two least-significant bits in the bottom byte.
 *
 * (Note that such a union is safe only because MicroLock is based on
 * a character type, and even under a strict interpretation of C++'s
 * aliasing rules, character types may alias anything.)
 *
 * Unused bits in the lock can be used to store user data via
 * lockAndLoad() and unlockAndStore(), or LockGuardWithData.
 *
 * The MaxSpins template parameter controls the number of times we
 * spin trying to acquire the lock.  MaxYields controls the number of
 * times we call sched_yield; once we've tried to acquire the lock
 * MaxSpins + MaxYields times, we sleep via folly::atomic_wait.
 * By adjusting these parameters, you can make MicroLock behave as
 * much or as little like a conventional spinlock as you'd like.
 *
 * Performance
 * -----------
 *
 * With the default template options, the timings for uncontended
 * acquire-then-release come out as follows on Intel(R) Xeon(R) CPU
 * E5-2660 0 @ 2.20GHz, in @mode/opt, as of the master tree at Tue, 01
 * Mar 2016 19:48:15.
 *
 * ========================================================================
 * folly/test/SmallLocksBenchmark.cpp          relative  time/iter  iters/s
 * ========================================================================
 * MicroSpinLockUncontendedBenchmark                       13.46ns   74.28M
 * PicoSpinLockUncontendedBenchmark                        14.99ns   66.71M
 * MicroLockUncontendedBenchmark                           27.06ns   36.96M
 * StdMutexUncontendedBenchmark                            25.18ns   39.72M
 * VirtualFunctionCall                                      1.72ns  579.78M
 * ========================================================================
 *
 * (The virtual dispatch benchmark is provided for scale.)
 *
 * While the uncontended case for MicroLock is competitive with
 * std::mutex, std::mutex is likely to be faster in the contended case:
 * std::mutex operates on a 32-bit word which can use the futex syscall
 * directly, while MicroLock's uint8_t forces atomic_wait through
 * folly's ParkingLot (a userspace hash table of mutex+condvar pairs).
 *
 * Make sure to benchmark your particular workload.
 *
 */

/// Base class holding the lock byte and the bit-packing helpers shared by
/// every MicroLock specialization.
class MicroLockCore {
 protected:
  /// The single byte holding both the lock bits and the packed data bits.
  uint8_t lock_{};
  /**
   * Mask for bit indicating that the flag is held.
   *
   * \returns The bit mask for the held bit.
   */
  unsigned heldBit() const noexcept;
  /**
   * Mask for bit indicating that there is a waiter that should be woken up.
   *
   * \returns The bit mask for the waiter bit.
   */
  unsigned waitBit() const noexcept;

  /// Acquire the lock along the contended path, spinning then yielding.
  ///
  /// \param oldWord The last observed value of the lock byte.
  /// \param maxSpins The maximum number of spins before yielding.
  /// \param maxYields The maximum number of yields before parking.
  /// \returns The lock byte value observed once the lock is held.
  uint8_t lockSlowPath(
      uint8_t oldWord, unsigned maxSpins, unsigned maxYields) noexcept;

  /// Number of low bits reserved for lock state.
  static constexpr unsigned kNumLockBits = 2;
  /// Mask selecting the bits reserved for lock state.
  static constexpr uint8_t kLockBits =
      static_cast<uint8_t>((1 << kNumLockBits) - 1);
  /// Mask selecting the bits available for user data.
  static constexpr uint8_t kDataBits = static_cast<uint8_t>(~kLockBits);
  /**
   * Decodes the value stored in the unused bits of the lock.
   *
   * \param lockByte The raw lock byte to decode.
   * \returns The user data extracted from the byte.
   */
  static constexpr uint8_t decodeDataFromByte(uint8_t lockByte) noexcept {
    return static_cast<uint8_t>(lockByte >> kNumLockBits);
  }
  /**
   * Encodes the value for the unused bits of the lock.
   *
   * \param data The user data to encode.
   * \returns The byte with the data shifted into the data bits.
   */
  static constexpr uint8_t encodeDataToByte(uint8_t data) noexcept {
    return static_cast<uint8_t>(data << kNumLockBits);
  }

  /// Decode the user data stored in the data bits of a lock word.
  ///
  /// \param word The lock word to decode.
  /// \returns The user data extracted from the word.
  static constexpr uint8_t decodeDataFromWord(uint8_t word) noexcept {
    return static_cast<uint8_t>(word >> kNumLockBits);
  }
  /// Encode user data into the data bits of a lock word, preserving lock bits.
  ///
  /// \param word The current lock word whose lock bits are preserved.
  /// \param value The user data to store in the data bits.
  /// \returns The updated lock word.
  static constexpr uint8_t encodeDataToWord(
      uint8_t word, uint8_t value) noexcept {
    const uint8_t preservedBits = word & ~(kDataBits);
    const uint8_t newBits = encodeDataToByte(value);
    return preservedBits | newBits;
  }

  /// Unlock the lock, applying a modifier to compute the stored word.
  ///
  /// \param modifier Callable mapping the old word to the new word.
  template <typename Func>
  void unlockAndStoreWithModifier(Func modifier) noexcept;

 public:
  /**
   * Loads the data stored in the unused bits of the lock atomically.
   *
   * \param order The memory order used for the atomic load.
   * \returns The user data stored in the data bits.
   */
  uint8_t load(
      std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return decodeDataFromWord(atomic_ref(lock_).load(order));
  }

  /**
   * Stores the data in the unused bits of the lock atomically. Since 2 bits are
   * used by the lock, the most significant 2 bits of the provided value will be
   * ignored.
   *
   * \param value The user data to store in the data bits.
   * \param order The memory order used for the atomic store.
   */
  void store(
      uint8_t value,
      std::memory_order order = std::memory_order_seq_cst) noexcept;

  /**
   * Unlocks the lock and stores the bits of the provided value into the data
   * bits. Since 2 bits are used by the lock, the most significant 2 bits of the
   * provided value will be ignored.
   *
   * \param value The user data to store in the data bits while unlocking.
   */
  void unlockAndStore(uint8_t value) noexcept;
  /// Release the lock without modifying the stored data bits.
  void unlock() noexcept;
};

inline unsigned MicroLockCore::heldBit() const noexcept {
  return 1U << 0;
}

inline unsigned MicroLockCore::waitBit() const noexcept {
  return 1U << 1;
}

inline void MicroLockCore::store(
    uint8_t value, std::memory_order order) noexcept {
  auto oldWord = atomic_ref(lock_).load(std::memory_order_relaxed);
  while (true) {
    auto newWord = encodeDataToWord(oldWord, value);
    if (atomic_ref(lock_).compare_exchange_weak(
            oldWord, newWord, order, std::memory_order_relaxed)) {
      break;
    }
  }
}

template <typename Func>
void MicroLockCore::unlockAndStoreWithModifier(Func modifier) noexcept {
  uint8_t oldWord;
  uint8_t newWord;

  oldWord = atomic_ref(lock_).load(std::memory_order_relaxed);
  do {
    assert(oldWord & heldBit());
    newWord = modifier(oldWord) & ~(heldBit() | waitBit());
  } while (!atomic_ref(lock_).compare_exchange_weak(
      oldWord, newWord, std::memory_order_release, std::memory_order_relaxed));

  if (oldWord & waitBit()) {
    atomic_notify_one(&atomic_ref(lock_).atomic());
  }
}

inline void MicroLockCore::unlockAndStore(uint8_t value) noexcept {
  unlockAndStoreWithModifier([value](uint8_t oldWord) {
    return encodeDataToWord(oldWord, value);
  });
}

inline void MicroLockCore::unlock() noexcept {
  unlockAndStoreWithModifier(identity);
}

/// A tiny one-byte spinlock with a configurable spin and yield budget.
///
/// \tparam MaxSpins The maximum number of spins before yielding.
/// \tparam MaxYields The maximum number of yields before parking.
template <unsigned MaxSpins = 1000, unsigned MaxYields = 0>
class MicroLockBase : public MicroLockCore {
 public:
  /**
   * Locks the lock and returns the data stored in the unused bits of the lock.
   * This is useful when you want to use the unused bits of the lock to store
   * data, in which case reading and locking should be done in one atomic
   * operation.
   *
   * \returns The user data stored in the data bits at the time of locking.
   */
  uint8_t lockAndLoad() noexcept;
  /// Acquire the lock, blocking until it is held.
  void lock() noexcept { lockAndLoad(); }
  /// Try to acquire the lock without blocking.
  ///
  /// \returns True if the lock was acquired.
  bool try_lock() noexcept;

  /**
   * A lock guard which allows reading and writing to the unused bits of the
   * lock as data.
   */
  struct LockGuardWithData {
    /// Acquire the lock and capture the data bits observed at lock time.
    ///
    /// \param lock The lock to acquire for the lifetime of the guard.
    explicit LockGuardWithData(MicroLockBase<MaxSpins, MaxYields>& lock)
        : lock_(lock) {
      loadedValue_ = lock_.lockAndLoad();
    }

    /// Release the lock, storing back a new value if one was set.
    ~LockGuardWithData() noexcept {
      if (storedValue_) {
        lock_.unlockAndStore(*storedValue_);
      } else {
        lock_.unlock();
      }
    }

    /**
     * The stored data bits at the time of locking.
     *
     * \returns The user data captured when the lock was acquired.
     */
    uint8_t loadedValue() const noexcept { return loadedValue_; }

    /**
     * The value that will be stored back into data bits when it is unlocked.
     *
     * \param value The user data to store back on unlock.
     */
    void storeValue(uint8_t value) noexcept { storedValue_ = value; }

   private:
    MicroLockBase<MaxSpins, MaxYields>& lock_;
    uint8_t loadedValue_;
    folly::Optional<uint8_t> storedValue_;
  };
};

template <unsigned MaxSpins, unsigned MaxYields>
bool MicroLockBase<MaxSpins, MaxYields>::try_lock() noexcept {
  // N.B. You might think that try_lock is just the fast path of lock,
  // but you'd be wrong.  Keep in mind that other parts of our host
  // word might be changing while we take the lock!  We're not allowed
  // to fail spuriously if the lock is in fact not held, even if other
  // people are concurrently modifying other parts of the word.
  //
  // We need to loop until we either see firm evidence that somebody
  // else has the lock (by looking at heldBit) or see our CAS succeed.
  // A failed CAS by itself does not indicate lock-acquire failure.

  uint8_t oldWord = atomic_ref(lock_).load(std::memory_order_relaxed);
  do {
    if (oldWord & heldBit()) {
      return false;
    }
  } while (!atomic_ref(lock_).compare_exchange_weak(
      oldWord,
      oldWord | heldBit(),
      std::memory_order_acquire,
      std::memory_order_relaxed));

  return true;
}

template <unsigned MaxSpins, unsigned MaxYields>
uint8_t MicroLockBase<MaxSpins, MaxYields>::lockAndLoad() noexcept {
  static_assert(MaxSpins + MaxYields < (unsigned)-1, "overflow");

  uint8_t oldWord;
  oldWord = atomic_ref(lock_).load(std::memory_order_relaxed);
  if ((oldWord & heldBit()) == 0 &&
      atomic_ref(lock_).compare_exchange_weak(
          oldWord,
          to_narrow(oldWord | heldBit()),
          std::memory_order_acquire,
          std::memory_order_relaxed)) {
    // Fast uncontended case: memory_order_acquire above is our barrier
    return decodeDataFromWord(to_narrow(oldWord | heldBit()));
  } else {
    // lockSlowPath doesn't call waitBit(); it just shifts the input bit.  Make
    // sure its shifting produces the same result a call to waitBit would.
    assert(heldBit() << 1 == waitBit());
    // lockSlowPath emits its own memory barrier
    return lockSlowPath(oldWord, MaxSpins, MaxYields);
  }
}

/// Default MicroLock specialization with the standard spin and yield budget.
using MicroLock = MicroLockBase<>;
} // namespace folly
