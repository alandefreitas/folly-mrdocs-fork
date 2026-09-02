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

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <folly/Likely.h>
#include <folly/Portability.h>
#include <folly/lang/Align.h>
#include <folly/synchronization/AtomicRef.h>

namespace folly {

// This file contains several classes that might be useful if you are
// trying to dynamically optimize cache locality: CacheLocality reads
// cache sharing information from procfs or sysfs to determine how CPUs
// should be grouped to minimize contention, Getcpu provides fast access
// to the current CPU via __vdso_getcpu, and AccessSpreader uses these two
// to optimally spread accesses among a predetermined number of stripes.
//
// AccessSpreader<>::current(n) microbenchmarks at 22 nanos, which is
// substantially less than the cost of a cache miss.  This means that we
// can effectively use it to reduce cache line ping-pong on striped data
// structures such as IndexedMemPool or statistics counters.
//
// Because CacheLocality looks at all of the cache levels, it can be
// used for different levels of optimization.  AccessSpreader(2) does
// per-chip spreading on a dual socket system.  AccessSpreader(numCpus)
// does perfect per-cpu spreading.  AccessSpreader(numCpus / 2) does
// perfect L1 spreading in a system with hyperthreading enabled.

/// Holds cache sharing topology for the current system.
///
/// Reads cache sharing information from procfs or sysfs to determine how CPUs
/// should be grouped to minimize contention between cache levels.
struct CacheLocality {
  /// 1 more than the maximum value that can be returned from sched_getcpu
  /// or getcpu.  This is the number of hardware thread contexts provided
  /// by the processors.
  size_t numCpus;

  /// NOTE: The information below may be a heuristic approximation based on the
  /// available mechanisms to parse cpu topology.

  /// Holds the number of caches present at each cache level (0 is
  /// the closest to the cpu).  This is the number of AccessSpreader
  /// stripes needed to avoid cross-cache communication at the specified
  /// layer.  numCachesByLevel.front() is the number of L1 caches and
  /// numCachesByLevel.back() is the number of last-level caches.
  std::vector<size_t> numCachesByLevel;

  /// A map from cpu (from sched_getcpu or getcpu) to an index in the
  /// range 0..numCpus-1, where neighboring locality indices are more
  /// likely to share caches then indices far away.  All of the members
  /// of a particular cache level be contiguous in their locality index.
  /// For example, if numCpus is 32 and numCachesByLevel.back() is 2,
  /// then cpus with a locality index < 16 will share one last-level
  /// cache and cpus with a locality index >= 16 will share the other.
  std::vector<size_t> localityIndexByCpu;

  /// For each cpu, a list of cache identifiers following the same layout as
  /// numCachesByLevel. The identifier itself is an arbitrary number: it only
  /// signifies that cpus with the same identifier share a cache at that level.
  std::vector<std::vector<size_t>> equivClassesByCpu;

  /// Returns the best CacheLocality information available for the current
  /// system, cached for fast access.  This will be loaded from procfs or
  /// sysfs if possible, otherwise it will be correct in the number of CPUs
  /// but not in their sharing structure.
  ///
  /// If you are into yo dawgs, this is a shared cache of the local
  /// locality of the shared caches.
  ///
  /// The template parameter here is used to allow injection of a
  /// repeatable CacheLocality structure during testing.  Rather than
  /// inject the type of the CacheLocality provider into every data type
  /// that transitively uses it, all components select between the default
  /// procfs/sysfs implementation and a deterministic implementation by keying
  /// off the type of the underlying atomic.  See DeterministicScheduler.
  ///
  /// \returns A reference to the cached CacheLocality for the current system.
  template <template <typename> class Atom = std::atomic>
  static const CacheLocality& system();

  /// Returns the best CacheLocality information available for the current
  /// system.  This will be loaded from procfs or sysfs if possible, otherwise
  /// it will be correct in the number of CPUs but not in their sharing
  /// structure.
  ///
  /// \returns The best CacheLocality information available for this system.
  static CacheLocality readSystemLocalityInfo();

  /// Reads CacheLocality information from a tree structured like
  /// the sysfs filesystem.  The provided function will be evaluated
  /// for each sysfs file that needs to be queried.  The function
  /// should return a string containing the first line of the file
  /// (not including the newline), or an empty string if the file does
  /// not exist.  The function will be called with paths of the form
  /// /sys/devices/system/cpu/cpu*/cache/index*/{type,shared_cpu_list} .
  /// Throws an exception if no caches can be parsed at all.
  ///
  /// \param root Path prepended to each queried sysfs file path.
  /// \returns The CacheLocality parsed from the sysfs-like tree.
  static CacheLocality readFromSysfsTree(std::string_view root = "/");

  /// Reads CacheLocality information from the real sysfs filesystem.
  /// Throws an exception if no cache information can be loaded.
  ///
  /// \returns The CacheLocality parsed from the real sysfs filesystem.
  static CacheLocality readFromSysfs();

  /// readFromProcCpuinfo(), except input is taken from memory rather
  /// than the file system.
  ///
  /// \param lines The lines of a /proc/cpuinfo-formatted input.
  /// \returns The CacheLocality estimated from the given lines.
  static CacheLocality readFromProcCpuinfoLines(
      std::vector<std::string> const& lines);

  /// Returns an estimate of the CacheLocality information by reading
  /// /proc/cpuinfo.  This isn't as accurate as readFromSysfs(), but
  /// is a lot faster because the info isn't scattered across
  /// hundreds of files.  Throws an exception if no cache information
  /// can be loaded.
  ///
  /// \returns The CacheLocality estimated from /proc/cpuinfo.
  static CacheLocality readFromProcCpuinfo();

  /// Returns a usable (but probably not reflective of reality)
  /// CacheLocality structure with the specified number of cpus and a
  /// single cache level that associates one cpu per cache.
  ///
  /// \param numCpus The number of cpus to describe.
  /// \returns A uniform CacheLocality with one cpu per cache.
  static CacheLocality uniform(size_t numCpus);

 private:
  explicit CacheLocality(std::vector<std::vector<size_t>> equivClasses);
};

/// Knows how to derive a function pointer to the VDSO implementation of
/// getcpu(2), if available
struct Getcpu {
  /// Function pointer to a function with the same signature as getcpu(2).
  using Func = int (*)(unsigned* cpu, unsigned* node, void* unused);

  /// Returns a pointer to the VDSO implementation of getcpu(2), if
  /// available, or nullptr otherwise.  This function may be quite
  /// expensive, be sure to cache the result.
  ///
  /// \returns The VDSO getcpu(2) function pointer, or nullptr if unavailable.
  static Func resolveVdsoFunc();
};

/// Assigns thread ids from a monotonically increasing counter.
struct SequentialThreadId {
  /// Returns the sequential id bound to the calling thread.
  ///
  /// \returns A unique id for the current thread.
  static unsigned get();
};

/// Assigns thread ids by hashing the platform thread identifier.
struct HashingThreadId {
  /// Returns the hashed id for the calling thread.
  ///
  /// \returns A hashed id for the current thread.
  static unsigned get();
};

/// A class that lazily binds a unique (for each implementation of Atom)
/// identifier to a thread.  This is a fallback mechanism for the access
/// spreader if __vdso_getcpu can't be loaded
template <typename ThreadId>
struct FallbackGetcpu {
  /// Fills the thread id into the cpu and node out params (if they
  /// are non-null).  This method is intended to act like getcpu when a
  /// fast-enough form of getcpu isn't available or isn't desired
  ///
  /// \param cpu Out param that receives the thread id, if non-null.
  /// \param node Out param that receives the thread id, if non-null.
  /// \param unused Ignored, present to match the getcpu(2) signature.
  /// \returns Always 0.
  static int getcpu(unsigned* cpu, unsigned* node, void* unused) {
    auto id = ThreadId::get();
    if (cpu) {
      *cpu = id;
    }
    if (node) {
      *node = id;
    }
    return 0;
  }
};

/// The FallbackGetcpu specialization selected for the current platform.
using FallbackGetcpuType = FallbackGetcpu<
    conditional_t<kIsMobile, HashingThreadId, SequentialThreadId>>;

namespace detail {

class AccessSpreaderBase {
 public:
  /// If there are more cpus than this nothing will crash, but there
  /// might be unnecessary sharing
  enum {
    // Android phones with 8 cores exist today; 16 for future-proofing.
    kMaxCpus = kIsMobile ? 16 : 256,
  };

 protected:
  using CompactStripe = uint8_t;

  static_assert(
      (kMaxCpus & (kMaxCpus - 1)) == 0,
      "kMaxCpus should be a power of two so modulo is fast");
  static_assert(
      kMaxCpus - 1 <= std::numeric_limits<CompactStripe>::max(),
      "stripeByCpu element type isn't wide enough");

  using CompactStripeTable = CompactStripe[kMaxCpus + 1][kMaxCpus];

  struct GlobalState {
    /// For each level of splitting up to kMaxCpus, maps the cpu (mod
    /// kMaxCpus) to the stripe.  Rather than performing any inequalities
    /// or modulo on the actual number of cpus, we just fill in the entire
    /// array.
    /// Keep as the first field to avoid extra + in the fastest path.
    mutable CompactStripeTable table;

    /// Points to the getcpu-like function we are using to obtain the
    /// current cpu. It should not be assumed that the returned cpu value
    /// is in range.
    std::atomic<Getcpu::Func> getcpu; // nullptr -> not initialized
  };

  /// Always claims to be on CPU zero, node zero
  static int degenerateGetcpu(unsigned* cpu, unsigned* node, void*);

  static bool initialize(
      GlobalState& out, Getcpu::Func (&)(), const CacheLocality& (&)());
};

} // namespace detail

/// AccessSpreader arranges access to a striped data structure in such a
/// way that concurrently executing threads are likely to be accessing
/// different stripes.  It does NOT guarantee uncontended access.
/// Your underlying algorithm must be thread-safe without spreading, this
/// is merely an optimization.  AccessSpreader::current(n) is typically
/// much faster than a cache miss (12 nanos on my dev box, tested fast
/// in both 2.6 and 3.2 kernels).
///
/// If available (and not using the deterministic testing implementation)
/// AccessSpreader uses the getcpu system call via VDSO and the
/// precise locality information retrieved from sysfs by CacheLocality.
/// This provides optimal anti-sharing at a fraction of the cost of a
/// cache miss.
///
/// When there are not as many stripes as processors, we try to optimally
/// place the cache sharing boundaries.  This means that if you have 2
/// stripes and run on a dual-socket system, your 2 stripes will each get
/// all of the cores from a single socket.  If you have 16 stripes on a
/// 16 core system plus hyperthreading (32 cpus), each core will get its
/// own stripe and there will be no cache sharing at all.
///
/// AccessSpreader has a fallback mechanism for when __vdso_getcpu can't be
/// loaded, or for use during deterministic testing.  Using sched_getcpu
/// or the getcpu syscall would negate the performance advantages of
/// access spreading, so we use a thread-local value and a shared atomic
/// counter to spread access out.  On systems lacking both a fast getcpu()
/// and TLS, we hash the thread id to spread accesses.
///
/// AccessSpreader is templated on the template type that is used
/// to implement atomics, as a way to instantiate the underlying
/// heuristics differently for production use and deterministic unit
/// testing.  See DeterministicScheduler for more.  If you aren't using
/// DeterministicScheduler, you can just use the default template parameter
/// all of the time.
template <template <typename> class Atom = std::atomic>
struct AccessSpreader : private detail::AccessSpreaderBase {
 private:
  struct GlobalState : detail::AccessSpreaderBase::GlobalState {};
  static_assert(
      std::is_trivially_destructible<GlobalState>::value,
      "unsuitable for global state");

 public:
  /// Returns the lazily initialized global state for this Atom.
  ///
  /// \returns A reference to the shared GlobalState, initializing it if needed.
  FOLLY_EXPORT static GlobalState& state() {
    static constinit GlobalState state{};
    if (FOLLY_UNLIKELY(!state.getcpu.load(std::memory_order_acquire))) {
      initialize(state);
    }
    return state;
  }

  /// Returns the stripe associated with the current CPU.  The returned
  /// value will be < numStripes.
  ///
  /// \param numStripes The number of stripes to spread across.
  /// \param s The global state holding the stripe table and getcpu function.
  /// \returns The stripe index for the current CPU, less than numStripes.
  static size_t current(size_t numStripes, const GlobalState& s = state()) {
    // s.table[0] will actually work okay (all zeros), but
    // something's wrong with the caller
    assert(numStripes > 0);

    unsigned cpu;
    s.getcpu.load(std::memory_order_relaxed)(&cpu, nullptr, nullptr);
    cpu = cpu % kMaxCpus;
    auto& ref = s.table[std::min(size_t(kMaxCpus), numStripes)][cpu];
    return make_atomic_ref(ref).load(std::memory_order_relaxed);
  }

  /// Returns the stripe associated with the current CPU.  The returned
  /// value will be < numStripes.
  /// This function caches the current cpu in a thread-local variable for a
  /// certain small number of calls, which can make the result imprecise, but
  /// it is more efficient (amortized 2 ns on my dev box, compared to 12 ns for
  /// current()).
  ///
  /// \param numStripes The number of stripes to spread across.
  /// \param s The global state holding the stripe table and getcpu function.
  /// \returns The stripe index for the cached current CPU, less than numStripes.
  static size_t cachedCurrent(
      size_t numStripes, const GlobalState& s = state()) {
    if (kIsMobile) {
      return current(numStripes, s);
    }
    unsigned cpu = cpuCache().cpu(s);
    auto& ref = s.table[std::min(size_t(kMaxCpus), numStripes)][cpu];
    return make_atomic_ref(ref).load(std::memory_order_relaxed);
  }

  /// Forces the next cachedCurrent() call in this thread to re-probe the
  /// current CPU.
  static void invalidateCachedCurrent() {
    if (kIsMobile) {
      return;
    }
    cpuCache().invalidate();
  }

  /// Returns a canonical index in [0, maxLocalityIndexValue()) for each
  /// stripe. This can be used to share global data structures accessed with
  /// different stripings. For optimal spread, it is best for numStripes to be a
  /// divisor of the number of L1 caches.
  ///
  /// \param numStripes The number of stripes in the current striping.
  /// \param stripe The stripe to map, less than numStripes.
  /// \returns A canonical locality index in [0, maxLocalityIndexValue()).
  static size_t localityIndexForStripe(size_t numStripes, size_t stripe) {
    assert(stripe < numStripes);
    return stripe *
        std::min(size_t(kMaxCpus), CacheLocality::system<Atom>().numCpus) /
        numStripes;
  }

  /// Returns the maximum stripe value that can be returned under any
  /// dynamic configuration, based on the current compile-time platform
  ///
  /// \returns The maximum stripe value for this platform.
  static constexpr size_t maxStripeValue() { return kMaxCpus; }

  /// Returns the maximum locality index value that can be returned under any
  /// dynamic configuration, based on the current compile-time platform
  ///
  /// \returns The maximum locality index value for this platform.
  static constexpr size_t maxLocalityIndexValue() { return kMaxCpus; }

 private:
  /// Caches the current CPU and refreshes the cache every so often.
  class CpuCache {
   public:
    unsigned cpu(GlobalState const& s) {
      if (FOLLY_UNLIKELY(cachedCpuUses_-- == 0)) {
        unsigned cpu;
        s.getcpu.load(std::memory_order_relaxed)(&cpu, nullptr, nullptr);
        cachedCpu_ = cpu % kMaxCpus;
        cachedCpuUses_ = kMaxCachedCpuUses - 1;
      }
      return cachedCpu_;
    }

    void invalidate() { cachedCpuUses_ = 0; }

   private:
    static constexpr unsigned kMaxCachedCpuUses = 32;

    unsigned cachedCpu_ = 0;
    unsigned cachedCpuUses_ = 0;
  };

  FOLLY_EXPORT FOLLY_ALWAYS_INLINE static CpuCache& cpuCache() {
    static thread_local CpuCache cpuCache;
    return cpuCache;
  }

  /// Returns the best getcpu implementation for Atom
  static Getcpu::Func pickGetcpuFunc() {
    auto best = Getcpu::resolveVdsoFunc();
    return best ? best : &FallbackGetcpuType::getcpu;
  }

  // The function to call for fast lookup of getcpu is a singleton, as
  // is the precomputed table of locality information.  AccessSpreader
  // is used in very tight loops, however (we're trying to race an L1
  // cache miss!), so the normal singleton mechanisms are noticeably
  // expensive.  Even a not-taken branch guarding access to getcpuFunc
  // slows AccessSpreader::current from 12 nanos to 14.  As a result, we
  // populate the static members with simple (but valid) values that can
  // be filled in by the linker, and then follow up with a normal static
  // initializer call that puts in the proper version.  This means that
  // when there are initialization order issues we will just observe a
  // zero stripe.  Once a sanitizer gets smart enough to detect this as
  // a race or undefined behavior, we can annotate it.

  static bool initialize(GlobalState& state) {
    return detail::AccessSpreaderBase::initialize(
        state, pickGetcpuFunc, CacheLocality::system<Atom>);
  }
};

/// Similar to AccessSpreader, but it has exactly one stripe for each last-level
/// cache that is accessible by the current process.
///
/// Only supported on Linux; on other systems, numStripes() always returns 1
/// and current() always returns 0.
class LLCAccessSpreader {
  struct PrivateTag {};

 public:
  /// Returns the process-wide LLCAccessSpreader singleton.
  ///
  /// \returns A reference to the shared LLCAccessSpreader instance.
  static LLCAccessSpreader& get();

  /// Constructs the spreader, populating the per-cpu stripe table.
  ///
  /// \param tag Private construction tag.
  explicit LLCAccessSpreader(PrivateTag tag);

  /// Returns the last-level-cache stripe for the current CPU.
  ///
  /// \returns The stripe index for the current CPU, or 0 if unsupported.
  size_t current() const;

  /// Returns the number of last-level-cache stripes.
  ///
  /// \returns The number of stripes, or 1 if unsupported.
  size_t numStripes() const;

 private:
  Getcpu::Func getcpu_;
  size_t numStripes_;
  std::vector<size_t> stripeByCpu_;
};

/**
 * An allocator that can be used with AccessSpreader to allocate core-local
 * memory.
 *
 * There is actually nothing special about the memory itself (it is not bound to
 * NUMA nodes or anything), but the allocator guarantees that memory allocatd
 * from the same stripe will only come from cache lines also allocated to the
 * same stripe, for the given numStripes.  This means multiple things using
 * AccessSpreader can allocate memory in smaller-than cacheline increments, and
 * be assured that it won't cause more false sharing than it otherwise would.
 *
 * Allocations smaller than sizeof(void*) (typically 8 bytes) are automatically
 * rounded up to ensure correct internal bookkeeping.
 *
 * Note that allocation and deallocation takes a per-size-class lock.
 *
 * Memory allocated with coreMalloc() must be freed with coreFree().
 *
 * \param size Number of bytes to allocate.
 * \param numStripes Total number of stripes the allocation is spread across.
 * \param stripe Stripe the allocation is bound to, less than numStripes.
 * \returns A pointer to the allocated core-local memory.
 */
void* coreMalloc(size_t size, size_t numStripes, size_t stripe);

/// Frees memory allocated with coreMalloc().
///
/// \param ptr Pointer returned by coreMalloc().
void coreFree(void* ptr);

namespace detail {
void* coreMallocFromGuard(size_t size);
}

/**
 * An C++ allocator adapter for coreMalloc/Free. The allocator is stateless, to
 * avoid increasing the footprint of the container that uses it, so the stripe
 * needs to be passed out of band: allocate() can only be called while there is
 * an active CoreAllocatorGuard. deallocate() can instead be called at any
 * point.
 *
 * This makes CoreAllocator unsuitable for containers that can grow, and it is
 * meant for container where all allocations happen at construction time.
 */
template <typename T>
class CoreAllocator : private std::allocator<T> {
 public:
  /// The type of the allocated elements.
  using value_type = T;

  /// Constructs a stateless CoreAllocator.
  CoreAllocator() = default;

  /// Constructs a CoreAllocator from one bound to a different element type.
  ///
  /// \param other Allocator bound to element type U.
  template <class U>
  /* implicit */ CoreAllocator(const CoreAllocator<U>& other) {}

  /// Allocates storage for n elements from the active CoreAllocatorGuard.
  ///
  /// \param n Number of elements to allocate.
  /// \returns A pointer to storage for n objects of type T.
  T* allocate(std::size_t n) {
    return reinterpret_cast<T*>(detail::coreMallocFromGuard(n * sizeof(T)));
  }

  /// Deallocates storage previously obtained from allocate().
  ///
  /// \param p Pointer to the storage to free.
  /// \param n Number of elements the storage was allocated for.
  void deallocate(T* p, std::size_t n) { coreFree(p); }

  /// Compares two allocators for equality.
  ///
  /// \param lhs First allocator to compare.
  /// \param rhs Second allocator to compare.
  /// \returns Always true, since the allocator is stateless.
  friend bool operator==(
      const CoreAllocator& lhs, const CoreAllocator& rhs) noexcept {
    return true;
  }
  /// Compares two allocators for inequality.
  ///
  /// \param lhs First allocator to compare.
  /// \param rhs Second allocator to compare.
  /// \returns Always false, since the allocator is stateless.
  friend bool operator!=(
      const CoreAllocator& lhs, const CoreAllocator& rhs) noexcept {
    return false;
  }

  /// Rebinds the allocator to a different element type.
  template <typename U>
  struct rebind {
    /// The CoreAllocator specialization for element type U.
    using other = CoreAllocator<U>;
  };
};

/// Scope guard that binds CoreAllocator allocations to a stripe.
///
/// While a guard is alive, CoreAllocator::allocate() draws memory for the
/// given stripe and numStripes.
class [[nodiscard]] CoreAllocatorGuard {
 public:
  /// Activates core-local allocation for the given stripe.
  ///
  /// \param numStripes Total number of stripes.
  /// \param stripe Stripe that allocations are bound to, less than numStripes.
  CoreAllocatorGuard(size_t numStripes, size_t stripe);

  /// Ends the guarded allocation scope.
  ~CoreAllocatorGuard();

  /// Deleted copy constructor.
  ///
  /// \param other Guard to copy from.
  CoreAllocatorGuard(const CoreAllocatorGuard& other) = delete;

  /// Deleted copy assignment.
  ///
  /// \param other Guard to copy from.
  /// \returns A reference to this guard.
  CoreAllocatorGuard& operator=(const CoreAllocatorGuard& other) = delete;

  /// Deleted move constructor.
  ///
  /// \param other Guard to move from.
  CoreAllocatorGuard(CoreAllocatorGuard&& other) = delete;

  /// Deleted move assignment.
  ///
  /// \param other Guard to move from.
  /// \returns A reference to this guard.
  CoreAllocatorGuard& operator=(CoreAllocatorGuard&& other) = delete;

 private:
  friend void* detail::coreMallocFromGuard(size_t size);

  size_t numStripes_;
  size_t stripe_;
};

} // namespace folly
