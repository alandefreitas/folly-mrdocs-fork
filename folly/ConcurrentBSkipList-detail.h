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

// ConcurrentBSkipList-detail.h — Internal seqlock primitive, node types, and
// allocator support for ConcurrentBSkipList. Included by ConcurrentBSkipList.h.
// Do NOT include directly.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include <glog/logging.h>
#include <folly/Portability.h>
#include <folly/Utility.h>
#include <folly/lang/Align.h>
#include <folly/portability/Asm.h>
#include <folly/synchronization/RWSpinLock.h>
#include <folly/synchronization/RelaxedAtomic.h>
#include <folly/synchronization/SanitizeThread.h>

namespace folly {
/// Selects how leaf key/payload data is laid out in memory.
///
/// Separate (SoA): parallel keys_[] and payloads_[] arrays.
/// Inline (AoS): interleaved {key, payload} records per slot.
enum class LeafStoragePolicy {
  Separate, ///< Parallel keys_[] and payloads_[] arrays (structure of arrays).
  Inline, ///< Interleaved {key, payload} records per slot (array of structs).
};

/// Selects how Skipper reads a key slot under concurrent writes.
///
/// Default per T:
///   trivially-copyable + lock-free hardware atomic (≤8B) -> RelaxedAtomic
///   otherwise                                            -> Locked
enum class KeyReadPolicy {
  /// Acquire the leaf shared mutex on every read. Always correct.
  Locked,
  /// Atomic load (relaxed). T must be hardware-atomic + trivially copyable
  /// (e.g. uint32_t, uint64_t, 16B struct on x86 with cmpxchg16b).
  RelaxedAtomic,
};

/// Concurrent B-skip-list container; declared here for friend declarations.
template <
    typename,
    typename,
    int,
    folly::KeyReadPolicy,
    folly::LeafStoragePolicy,
    typename>
class ConcurrentBSkipList;
} // namespace folly

/// Internal implementation details for ConcurrentBSkipList.
namespace folly::bskip_detail {

// ---------------------------------------------------------------------------
// Seqlock for ConcurrentBSkipList OLC reader/writer bracketing.
//
// Standard seqlock protocol:
//   Writer: store(version+1, relaxed) → fence(release) → write data →
//           store(version+2, release)
//   Reader: load(version, acquire) → read data → fence(acquire) →
//           load(version, relaxed) → compare
//
// Key design choices:
//   - Typed field access (AtomicSlot<T>) instead of byte-at-a-time
//     memcpy. A 16-byte key read is one vmovdqa/ldp instruction.
//     Possible because we restrict keys to hardware-atomic types
//     (static_assert in InternalTraits).
//   - Spin-retry on odd version (writer in flight) before returning
//     to the caller, sized to absorb brief alloc-free insert epochs.
//   - RAII WriteGuard tied to the node's seqlock lifetime.
//
// Writers must be externally synchronized (node mutex held).
// ---------------------------------------------------------------------------

/// Seqlock used to bracket OLC reader/writer access to a node's fields.
class Seqlock {
 public:
  /// RAII guard that opens a write epoch on construction and closes it on
  /// destruction.
  class WriteGuard : folly::NonCopyableNonMovable {
   public:
    /// Opens a write epoch on the given seqlock.
    /// \param seq The seqlock to bracket for the guard's lifetime.
    explicit WriteGuard(Seqlock& seq) : seq_(seq) { seq_.beginWrite(); }
    /// Closes the write epoch opened by the constructor.
    ~WriteGuard() { seq_.endWrite(); }

   private:
    Seqlock& seq_;
  };

  /// Returns an RAII guard that brackets a write epoch on this seqlock.
  ///
  /// NOTE: caller must hold the node mutex for the lifetime of this guard.
  /// Only one writer may increment the seqlock at a time.
  /// \returns A WriteGuard bound to this seqlock.
  [[nodiscard]] WriteGuard writeGuard() { return WriteGuard{*this}; }

  /// Begins an optimistic read and returns the observed even version.
  /// \tparam kReadSpins Spin budget to wait out an in-flight writer epoch.
  /// \returns The version snapshot to pass to a later validate() call.
  template <int kReadSpins>
  uint32_t readBegin() const {
    if constexpr (kReadSpins > 0) {
      for (int spin = 0; spin < kReadSpins; ++spin) {
        uint32_t v = version_.load(std::memory_order_acquire);
        if (!(v & 1)) {
          return v;
        }
        folly::asm_volatile_pause();
      }
    }
    return version_.load(std::memory_order_acquire);
  }

  /// Validates that no writer epoch crossed the read since readBegin().
  /// \param v The version snapshot returned by readBegin().
  /// \returns true if the read was consistent, false if it must be retried.
  bool validate(uint32_t v) const {
    std::atomic_thread_fence(std::memory_order_acquire);
    return version_.load(std::memory_order_relaxed) == v;
  }

  /// Reports whether a writer epoch is currently open.
  /// \returns true if the version is odd (writer in flight).
  bool isInWriteEpoch() const noexcept {
    return (version_.load(std::memory_order_relaxed) & 1u) != 0;
  }

  /// Opens a write epoch by making the version odd.
  ///
  /// Writer is externally synchronized (node mutex held). Relaxed store is
  /// sufficient; the release fence orders it before subsequent data writes.
  void beginWrite() {
    uint32_t prev = version_.load(std::memory_order_relaxed);
    DCHECK_EQ(prev & 1u, 0u) << "beginWrite while already in write epoch";
    DCHECK_LT(prev, std::numeric_limits<uint32_t>::max() - 1)
        << "version overflow";
    version_.store(prev + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
  }

  /// Closes a write epoch by making the version even again.
  void endWrite() {
    version_.store(
        version_.load(std::memory_order_relaxed) + 1,
        std::memory_order_release);
  }

  /// Registers the version field with the thread sanitizer as a benign race.
  /// \param file Source file of the annotation site.
  /// \param line Source line of the annotation site.
  void annotateVersionField(const char* file, int line) const {
    annotate_benign_race_sized(
        &version_,
        sizeof(version_),
        "BSkipList OLC: seqlock version field",
        file,
        line);
  }

 private:
  mutable std::atomic<uint32_t> version_;
};

// ---------------------------------------------------------------------------
// Node types, storage primitives, and allocator support
// ---------------------------------------------------------------------------

/// Zero-size payload slot for [[no_unique_address]] in void-payload templates.
using Empty = std::monostate;

/// Test-hook event points fired along the concurrent read/split paths.
enum class BSkipTestHookEvent {
  ProbeNextLeafLoadNext, ///< About to load next pointer while probing the next leaf.
  ProbeNextLeafValidateNext, ///< About to validate next pointer while probing the next leaf.
  OlcLoadNext, ///< About to load next pointer on the optimistic (OLC) path.
  LeafKeyPostLoadValidate, ///< About to validate a leaf key just loaded optimistically.
  SplitPublishComplete, ///< A split sibling has just been published.
};

/// Function-pointer type for a BSkipList test hook.
///
/// Signature defined unconditionally so callers compile regardless of whether
/// FOLLY_BSKIP_TEST_HOOKS is set; only bodies depend on the macro.
using BSkipTestHook = void (*)(
    BSkipTestHookEvent event,
    std::memory_order order,
    const void* node,
    const void* peer);

#ifdef FOLLY_BSKIP_TEST_HOOKS
inline std::atomic<BSkipTestHook> gBSkipTestHook{nullptr};

FOLLY_ALWAYS_INLINE void setBSkipTestHook(BSkipTestHook hook) {
  gBSkipTestHook.store(hook, std::memory_order_relaxed);
}

FOLLY_ALWAYS_INLINE void invokeBSkipTestHook(
    BSkipTestHookEvent event,
    std::memory_order order,
    const void* node,
    const void* peer) {
  if (auto hook = gBSkipTestHook.load(std::memory_order_relaxed)) {
    hook(event, order, node, peer);
  }
}
#else
/// Installs the global BSkipList test hook (no-op unless test hooks are built).
/// \param hook The hook to install.
FOLLY_ALWAYS_INLINE void setBSkipTestHook(BSkipTestHook hook) {}
/// Fires the global BSkipList test hook (no-op unless test hooks are built).
/// \param event The event point being reported.
/// \param order The memory order of the associated access.
/// \param node The primary node involved in the event.
/// \param peer The peer node involved in the event, if any.
FOLLY_ALWAYS_INLINE void invokeBSkipTestHook(
    BSkipTestHookEvent event, std::memory_order order, const void* node, const void* peer) {}
#endif

/// Tuning knobs separated from InternalTraits so they can be adjusted without
/// touching the Traits template parameter set.
struct BSkipTuning {
  /// Same-leaf optimistic-miss retries before broader escalation. Sized to
  /// absorb a brief writer epoch.
  static constexpr int kSeqlockRetries = 3;
  /// Cached-path OLC + one root-reload retry before falling back to locked HOH.
  static constexpr int kOlcAttempts = 2;
  /// Spin budget for an active writer before AdaptiveReadGuard takes the
  /// shared lock. ARM: fewer spins because isb is heavier than x86 pause.
  static constexpr int kReadSpins = folly::kIsArchAArch64 ? 48 : 256;
};

// ---------------------------------------------------------------------------
// Storage primitive for optimistic-read paths. Locked-mode uses plain T
// (leaf mutex provides exclusion); optimistic mode uses AtomicSlot<T>.
// ---------------------------------------------------------------------------

/// Lock-free read slot for hardware-atomic T.
///
/// The atomic load instruction IS the bracket: a writer's store either fully
/// precedes or fully follows the reader's load and never tears. Readers need
/// no surrounding protocol.
///
/// Wraps folly::relaxed_atomic<T>; synthesized copy/move ops let
/// std::array element-assignment compile in splitKeys/moveChildren (raw
/// std::atomic isn't copyable).
///
/// Alignment inherits from std::atomic<T> and is STRICTER than alignof(T)
/// when the lock-free instruction is wider than T's natural alignment
/// (e.g. 16-byte cmpxchg16b on an 8-byte-aligned 16-byte struct). Do NOT
/// replace this wrapper with std::atomic_ref<T> over raw T storage:
/// atomic_ref surrenders that alignment guarantee back to the caller AND
/// regressed children_ traversal on 16B-key benchmarks (misaligned loads).
/// \tparam T The stored hardware-atomic value type.
template <typename T>
struct AtomicSlot {
  /// Constructs an empty slot.
  AtomicSlot() = default;
  /// Destroys the slot.
  ~AtomicSlot() = default;
  /// Constructs a slot holding a copy of the given value.
  /// \param v The value to store.
  /* implicit */ AtomicSlot(const T& v) : val_(v) {}
  /// Constructs a slot from another slot's current value.
  /// \param o The slot to copy.
  AtomicSlot(const AtomicSlot& o) : val_(o.val_.load()) {}
  /// Constructs a slot from another slot's current value.
  /// \param o The slot to move from.
  AtomicSlot(AtomicSlot&& o) noexcept : val_(o.val_.load()) {}
  /// Stores another slot's current value into this slot.
  /// \param o The slot to copy.
  /// \returns A reference to this slot.
  AtomicSlot& operator=(const AtomicSlot& o) {
    val_.store(o.val_.load());
    return *this;
  }
  /// Stores another slot's current value into this slot.
  /// \param o The slot to move from.
  /// \returns A reference to this slot.
  AtomicSlot& operator=(AtomicSlot&& o) noexcept {
    val_.store(o.val_.load());
    return *this;
  }
  /// Stores a raw value into this slot.
  /// \param v The value to store.
  /// \returns A reference to this slot.
  AtomicSlot& operator=(const T& v) {
    val_.store(v);
    return *this;
  }
  /// Loads the current value from this slot.
  /// \returns The stored value.
  /* implicit */ operator T() const { return val_.load(); }

  /// The underlying relaxed atomic storage.
  folly::relaxed_atomic<T> val_;
};

/// Interleaved {key, payload} record for inline (AoS) leaf storage.
///
/// Non-void PayloadT only (InternalTraits gates instantiation).
/// \tparam KeyStorage The key slot storage type.
/// \tparam PayloadT The payload value type.
template <typename KeyStorage, typename PayloadT>
struct InlineLeafRecord {
  /// The key slot.
  KeyStorage key{};
  /// The payload value.
  PayloadT payload{};
};

/// Detects whether a type qualifies as a hardware-atomic key/payload type.
namespace detail_hw_atomic {
/// Satisfied when T is trivially copyable and always lock-free as an atomic.
///
/// Concept so the && short-circuits: std::atomic<void>::is_always_lock_free
/// is ill-formed, but trivially_copyable<void> fails first.
/// \tparam T The type under test.
template <typename T>
concept Check =
    std::is_trivially_copyable_v<T> && std::atomic<T>::is_always_lock_free;
} // namespace detail_hw_atomic

/// True when T is a hardware-atomic (lock-free, trivially copyable) type.
/// \tparam T The type under test.
template <typename T>
inline constexpr bool kIsHardwareAtomic = detail_hw_atomic::Check<T>;

/// Default key-read policy for T: RelaxedAtomic if hardware-atomic, else Locked.
///
/// Optimistic readers may call Comp on stale-but-complete key values
/// (hw-atomic guarantees no tearing). The comparator must be safe on any
/// complete value — no pointer dereferences, no side effects.
/// \tparam T The key type.
template <typename T>
inline constexpr folly::KeyReadPolicy kDefaultReadPolicy = kIsHardwareAtomic<T>
    ? folly::KeyReadPolicy::RelaxedAtomic
    : folly::KeyReadPolicy::Locked;

/// Picks the leaf-slot storage type for a key/value type T.
///
///   !kOptimistic  → plain T (read under leaf mutex)
///   kOptimistic   → AtomicSlot<T> (single-instruction relaxed atomic load)
/// \tparam T The stored value type.
/// \tparam kOptimistic Whether the optimistic (lock-free) read path is used.
template <typename T, bool kOptimistic>
using KeyStorage = std::conditional_t<!kOptimistic, T, AtomicSlot<T>>;

/// Same as KeyStorage but Empty when there's no payload at all.
/// \tparam T The payload value type.
/// \tparam kHasValue Whether a payload exists.
/// \tparam kOptimistic Whether the optimistic (lock-free) read path is used.
template <typename T, bool kHasValue, bool kOptimistic>
using PayloadStorage =
    std::conditional_t<!kHasValue, Empty, KeyStorage<T, kOptimistic>>;

/// Derived compile-time traits for a ConcurrentBSkipList instantiation.
/// \tparam T The key type.
/// \tparam Comp The key comparator.
/// \tparam B The node fanout.
/// \tparam P The promotion probability denominator.
/// \tparam ReadPolicy The key-read policy.
/// \tparam PayloadT The payload type (void for a set).
/// \tparam StoragePolicy The leaf storage layout policy.
template <
    typename T,
    typename Comp,
    int B,
    int P,
    folly::KeyReadPolicy ReadPolicy = kDefaultReadPolicy<T>,
    typename PayloadT = void,
    folly::LeafStoragePolicy StoragePolicy = folly::LeafStoragePolicy::Separate>
struct InternalTraits {
  static_assert(B > 1, "B (fanout) must be > 1");
  static_assert(P > 1, "P (promotion denominator) must be > 1");
  static_assert(
      std::numeric_limits<T>::is_specialized,
      "T must have std::numeric_limits specialization for sentinel values");
  static_assert(
      std::is_void_v<PayloadT> || std::is_trivially_copyable_v<PayloadT>,
      "PayloadType must be void or trivially copyable");
  static_assert(
      StoragePolicy != folly::LeafStoragePolicy::Inline ||
          !std::is_void_v<PayloadT>,
      "Inline storage requires a non-void PayloadType");
  static_assert(
      ReadPolicy != folly::KeyReadPolicy::RelaxedAtomic || kIsHardwareAtomic<T>,
      "RelaxedAtomic requires a hardware-atomic key type "
      "(e.g. uint32_t, uint64_t, 16B struct on x86)");
  static_assert(
      ReadPolicy == folly::KeyReadPolicy::Locked || std::is_void_v<PayloadT> ||
          kIsHardwareAtomic<PayloadT>,
      "Optimistic mode with payload requires a hardware-atomic payload type");
  /// The key type.
  using key_type = T;
  /// The payload type (void for a set).
  using payload_type = PayloadT;
  /// True when the list carries a non-void payload.
  static constexpr bool kHasPayload = !std::is_void_v<PayloadT>;
  /// Pointer to a stored payload, or std::nullptr_t when there is none.
  using PayloadPtr =
      std::conditional_t<kHasPayload, const PayloadT*, std::nullptr_t>;
  /// Maximum keys per node (the fanout B).
  static constexpr uint8_t kMaxKeys = B;
  /// Inverse promotion probability (the denominator P).
  static constexpr uint8_t kPromotionProbInverse = P;
  /// True when reads use the optimistic (lock-free) path.
  static constexpr bool kOptimistic =
      ReadPolicy != folly::KeyReadPolicy::Locked;

  /// Leaf key slot storage type.
  using KeyStorage = bskip_detail::KeyStorage<T, kOptimistic>;
  /// Leaf payload slot storage type.
  using PayloadStorage =
      bskip_detail::PayloadStorage<PayloadT, kHasPayload, kOptimistic>;
  /// True when payloads are stored inline (AoS) with keys.
  static constexpr bool kInlinePayload =
      kHasPayload && StoragePolicy == folly::LeafStoragePolicy::Inline;
  /// True when payloads are stored in a separate (SoA) array.
  static constexpr bool kHasSeparatePayload =
      kHasPayload && StoragePolicy == folly::LeafStoragePolicy::Separate;
  /// Unsigned word type wide enough to hold one tombstone bit per key slot.
  using TombstoneWord = std::conditional_t<
      (B <= 8),
      uint8_t,
      std::conditional_t<
          (B <= 16),
          uint16_t,
          std::conditional_t<(B <= 32), uint32_t, uint64_t>>>;
  static_assert(kMaxKeys <= sizeof(TombstoneWord) * 8);

  /// Concrete "negative infinity" under Comp.
  static inline const key_type kMinSentinel =
      Comp{}(std::numeric_limits<T>::min(), std::numeric_limits<T>::max())
      ? std::numeric_limits<T>::min()
      : std::numeric_limits<T>::max();
  /// Concrete "positive infinity" under Comp.
  static inline const key_type kMaxSentinel =
      Comp{}(std::numeric_limits<T>::min(), std::numeric_limits<T>::max())
      ? std::numeric_limits<T>::max()
      : std::numeric_limits<T>::min();

  /// Reports whether a sorts before b under Comp.
  /// \tparam L The left operand type.
  /// \tparam R The right operand type.
  /// \param a The left operand.
  /// \param b The right operand.
  /// \returns true if a < b under Comp.
  template <typename L, typename R>
  FOLLY_ALWAYS_INLINE static bool less(const L& a, const R& b) {
    return Comp{}(a, b);
  }
  /// Reports whether a and b are equivalent under Comp.
  /// \tparam L The left operand type.
  /// \tparam R The right operand type.
  /// \param a The left operand.
  /// \param b The right operand.
  /// \returns true if a and b are equivalent under Comp.
  template <typename L, typename R>
  FOLLY_ALWAYS_INLINE static bool equal(const L& a, const R& b) {
    if constexpr (std::is_same_v<L, R> && requires(const L& x) { x == x; }) {
      return a == b;
    } else {
      return !Comp{}(a, b) && !Comp{}(b, a);
    }
  }
};

/// Reads a slot optimistically, then validates the read.
///
/// Returns nullopt if a writer epoch crossed the read (torn copy is
/// discarded).
/// \tparam Value The value type to extract.
/// \tparam Storage The slot storage type.
/// \tparam Validator The seqlock validation callable type.
/// \param storage The slot to read.
/// \param validate The callable that confirms the read was consistent.
/// \returns The value if the read validated, otherwise std::nullopt.
template <typename Value, typename Storage, typename Validator>
FOLLY_ALWAYS_INLINE std::optional<Value> loadValidated(
    const Storage& storage, Validator&& validate) {
  Value value = static_cast<Value>(storage);
  if (!validate()) {
    return std::nullopt;
  }
  return value;
}

/// Node allocator wrapper that rebinds a byte allocator per node type.
/// \tparam NodeAlloc The underlying byte allocator.
template <typename NodeAlloc = std::allocator<char>>
struct BSkipAllocator {
  /// Constructs an allocator with a default-constructed underlying allocator.
  BSkipAllocator() = default;
  /// Constructs an allocator wrapping the given underlying allocator.
  /// \param a The underlying allocator to copy.
  explicit BSkipAllocator(const NodeAlloc& a) : alloc(a) {}

  /// Allocates and constructs one node of type U.
  /// \tparam U The node type to allocate.
  /// \returns A pointer to the newly constructed node.
  template <typename U>
  U* allocate() {
    using AllocType =
        typename std::allocator_traits<NodeAlloc>::template rebind_alloc<U>;
    AllocType typedAlloc(alloc);
    U* ptr = std::allocator_traits<AllocType>::allocate(typedAlloc, 1);
    std::allocator_traits<AllocType>::construct(typedAlloc, ptr);
    return ptr;
  }

  /// Destroys and deallocates one node of type U.
  /// \tparam U The node type to deallocate.
  /// \param ptr The node to destroy and free.
  template <typename U>
  void deallocate(U* ptr) {
    using AllocType =
        typename std::allocator_traits<NodeAlloc>::template rebind_alloc<U>;
    AllocType typedAlloc(alloc);
    std::allocator_traits<AllocType>::destroy(typedAlloc, ptr);
    std::allocator_traits<AllocType>::deallocate(typedAlloc, ptr, 1);
  }

  /// The underlying byte allocator.
  [[no_unique_address]] NodeAlloc alloc;
};

template <typename Traits>
struct BSkipNode;
template <typename Traits>
class BSkipNodeLeaf;
template <typename Traits>
class BSkipNodeInternal;
template <typename Traits, int>
class AdaptiveReadGuard;

// Friend declaration for ConcurrentBSkipList; #undef at end of file.
#define FOLLY_BSKIP_FRIEND_LIST \
  template <                    \
      typename,                 \
      typename,                 \
      int,                      \
      folly::KeyReadPolicy,     \
      folly::LeafStoragePolicy, \
      typename>                 \
  friend class ::folly::ConcurrentBSkipList
/// Common base of leaf and internal nodes: next pointer, seqlock, and mutex.
/// \tparam Traits The InternalTraits for the list.
template <typename Traits>
struct BSkipNode {
  /// The key type.
  using T = typename Traits::key_type;
  /// The key slot storage type.
  using KeyStorage = typename Traits::KeyStorage;
  /// The seqlock type used to bracket concurrent access.
  using Seq = Seqlock;

  /// Constructs a node and registers its fields with the thread sanitizer.
  BSkipNode() { annotateBaseRaces(); }

  /// Loads the minimum key of the successor node.
  ///
  /// Extracts T out of KeyStorage (AtomicSlot<T> or plain T).
  /// \returns The successor node's minimum key.
  FOLLY_ALWAYS_INLINE T loadNextMinKey() const {
    return static_cast<T>(nextMinKey_);
  }

 protected:
  /// Registers the node's shared fields with the thread sanitizer as benign
  /// races.
  void annotateBaseRaces() {
    annotate_benign_race_sized(
        &next_,
        sizeof(next_),
        "BSkipList OLC: std::atomic next pointer; readers use acquire load",
        __FILE__,
        __LINE__);
    annotate_benign_race_sized(
        &nextMinKey_,
        sizeof(nextMinKey_),
        "BSkipList OLC: readers validate via version check",
        __FILE__,
        __LINE__);
    seq_.annotateVersionField(__FILE__, __LINE__);
    annotate_benign_race_sized(
        &numElements_,
        sizeof(numElements_),
        "BSkipList OLC: readers validate via version check",
        __FILE__,
        __LINE__);
  }

  FOLLY_BSKIP_FRIEND_LIST;
  template <typename, int>
  friend class AdaptiveReadGuard;

  /// Pointer to the next node at this level; readers pair load(acquire) with
  /// the release in publishSplitSibling.
  std::atomic<BSkipNode<Traits>*> next_;
  /// Seqlock bracketing optimistic reads of this node.
  mutable Seq seq_;
  /// Shared/exclusive mutex guarding locked-path access.
  mutable folly::RWSpinLock mutex_;
  /// Cached minimum key of the successor node.
  KeyStorage nextMinKey_{Traits::kMaxSentinel};
  /// Number of live elements in this node.
  folly::relaxed_atomic<uint8_t> numElements_;
  /// Node level; set once at allocation and immutable thereafter.
  uint8_t level_{0};
};

/// Finds the first slot in [begin, end) whose key sorts after target.
///
/// Callers under HOH locks get correct keys. Callers on the optimistic
/// (lockfree) path may read stale-but-complete keys (hw-atomic guaranteed);
/// they must validate the seqlock after this returns.
/// \tparam Traits The InternalTraits for the list.
/// \tparam LookupKey The search key type.
/// \tparam LoadKey The per-slot key loader callable type.
/// \param begin The first slot to scan.
/// \param end One past the last slot to scan.
/// \param target The search key.
/// \param loadKey Callable that loads the key at a given slot.
/// \returns The first slot whose key sorts after target, or end.
template <typename Traits, typename LookupKey, typename LoadKey>
FOLLY_ALWAYS_INLINE uint8_t findFirstGreaterLinear(
    uint8_t begin, uint8_t end, const LookupKey& target, LoadKey&& loadKey) {
  uint8_t i = begin;
  for (; i < end; ++i) {
    if (Traits::less(target, loadKey(i))) {
      break;
    }
  }
  return i;
}

/// RAII read guard that starts optimistic and escalates to the shared lock
/// when a writer is in flight.
/// \tparam Traits The InternalTraits for the list.
/// \tparam kReadSpins Spin budget before taking the shared lock.
template <typename Traits, int kReadSpins = BSkipTuning::kReadSpins>
class AdaptiveReadGuard : folly::NonCopyableNonMovable {
 public:
  /// Begins a read of the given node, taking the shared lock if a writer is
  /// active.
  /// \param node The node to read under this guard.
  explicit AdaptiveReadGuard(BSkipNode<Traits>& node) : node_(node) {
    version_ = node_.seq_.template readBegin<kReadSpins>();
    if (!(version_ & 1)) {
      return;
    }
    node_.mutex_.lock_shared();
    version_ = node_.seq_.template readBegin<0>();
    holdingSharedLock_ = true;
  }

  /// Releases the shared lock if this guard still holds it.
  ~AdaptiveReadGuard() { releaseSharedLockEarly(); }

  /// Reports whether the read was consistent.
  /// \returns true if the shared lock is held or the seqlock validated.
  bool valid() const {
    if (holdingSharedLock_) {
      return true;
    }
    return node_.seq_.validate(version_);
  }

  /// Releases the shared lock early, if held.
  ///
  /// No-op on the pure-optimistic path. Releases the shared lock earlier than
  /// the destructor would; clearing the flag prevents double-unlock.
  void releaseSharedLockEarly() {
    if (holdingSharedLock_) {
      node_.mutex_.unlock_shared();
      holdingSharedLock_ = false;
    }
  }

 private:
  BSkipNode<Traits>& node_;
  uint32_t version_ = 0;
  bool holdingSharedLock_ = false;
};

/// Leaf key/payload storage policy dispatcher.
///
/// Layout policy. Separate: parallel keys_/payloads_ arrays. Inline: AoS
/// records_. BSkipNodeLeaf delegates here so the kInlinePayload branches live
/// in one place.
/// \tparam Traits The InternalTraits for the list.
/// \tparam kInline Whether payloads are stored inline with keys.
template <typename Traits, bool kInline = Traits::kInlinePayload>
struct LeafStorage;

/// Separate (SoA) leaf storage: parallel key and payload arrays.
/// \tparam Traits The InternalTraits for the list.
template <typename Traits>
struct LeafStorage<Traits, /*kInline=*/false> {
  /// The key type.
  using T = typename Traits::key_type;
  /// The key slot storage type.
  using KeyStorage = typename Traits::KeyStorage;
  /// The payload slot storage type.
  using PayloadStorage = typename Traits::PayloadStorage;
  /// Array of key slots.
  using KeyArray = std::array<KeyStorage, Traits::kMaxKeys>;
  /// Array of payload slots (Empty when there is no separate payload).
  using PayloadArray = std::conditional_t<
      Traits::kHasSeparatePayload,
      std::array<PayloadStorage, Traits::kMaxKeys>,
      Empty>;

  /// The key slots.
  KeyArray keys_{};
  /// The payload slots.
  [[no_unique_address]] PayloadArray payloads_{};

  /// Loads the key at the given slot.
  /// \param slot The slot index.
  /// \returns The key value.
  FOLLY_ALWAYS_INLINE T loadKey(uint8_t slot) const {
    return static_cast<T>(keys_[slot]);
  }
  /// Stores a key at the given slot.
  /// \param slot The slot index.
  /// \param key The key to store.
  FOLLY_ALWAYS_INLINE void storeKey(uint8_t slot, const T& key) {
    keys_[slot] = key;
  }

  /// Loads the payload at the given slot.
  /// \tparam PayloadT The payload type to extract.
  /// \param slot The slot index.
  /// \returns The payload value.
  template <typename PayloadT>
  FOLLY_ALWAYS_INLINE PayloadT loadPayload(uint8_t slot) const {
    return static_cast<PayloadT>(payloads_[slot]);
  }
  /// Stores a payload at the given slot.
  /// \tparam PayloadT The payload type to store.
  /// \param slot The slot index.
  /// \param payload The payload to store.
  template <typename PayloadT>
  FOLLY_ALWAYS_INLINE void storePayload(uint8_t slot, const PayloadT& payload) {
    payloads_[slot] = payload;
  }

  /// Returns a const reference to the key slot storage at the given slot.
  /// \param slot The slot index.
  /// \returns A const reference to the key slot storage.
  FOLLY_ALWAYS_INLINE const KeyStorage& keyStorageAt(uint8_t slot) const {
    return keys_[slot];
  }
  /// Returns a mutable reference to the key slot storage at the given slot.
  /// \param slot The slot index.
  /// \returns A mutable reference to the key slot storage.
  FOLLY_ALWAYS_INLINE KeyStorage& keyStorageAt(uint8_t slot) {
    return keys_[slot];
  }
  /// Returns a const reference to the payload slot storage at the given slot.
  /// \tparam HasPayload Whether a separate payload array exists.
  /// \param slot The slot index.
  /// \returns A const reference to the payload slot storage.
  template <bool HasPayload = Traits::kHasSeparatePayload>
  FOLLY_ALWAYS_INLINE const PayloadStorage& payloadStorageAt(uint8_t slot) const
    requires(HasPayload)
  {
    return payloads_[slot];
  }
  /// Returns a mutable reference to the payload slot storage at the given slot.
  /// \tparam HasPayload Whether a separate payload array exists.
  /// \param slot The slot index.
  /// \returns A mutable reference to the payload slot storage.
  template <bool HasPayload = Traits::kHasSeparatePayload>
  FOLLY_ALWAYS_INLINE PayloadStorage& payloadStorageAt(uint8_t slot)
    requires(HasPayload)
  {
    return payloads_[slot];
  }

  /// Shifts elements in [slot, numElements) one position to the right.
  /// \param slot The first slot to shift.
  /// \param numElements The current live element count.
  void shiftElementsRight(uint8_t slot, uint8_t numElements) {
    for (uint8_t i = numElements; i > slot; --i) {
      keys_[i] = keys_[i - 1];
    }
    if constexpr (Traits::kHasSeparatePayload) {
      for (uint8_t i = numElements; i > slot; --i) {
        payloads_[i] = payloads_[i - 1];
      }
    }
  }

  /// Clears the key (and payload) at the given slot.
  /// \param slot The slot to clear.
  void clearElement(uint8_t slot) {
    keys_[slot] = KeyStorage{};
    if constexpr (Traits::kHasSeparatePayload) {
      payloads_[slot] = PayloadStorage{};
    }
  }

  /// Registers the key and payload arrays with the thread sanitizer.
  /// \param file Source file of the annotation site.
  /// \param line Source line of the annotation site.
  void annotateRaces(const char* file, int line) const {
    annotate_benign_race_sized(
        &keys_,
        sizeof(keys_),
        "BSkipList seqlock: readers validate via version check",
        file,
        line);
    if constexpr (Traits::kHasSeparatePayload) {
      annotate_benign_race_sized(
          &payloads_,
          sizeof(payloads_),
          "BSkipList seqlock: payload reads validate via version check",
          file,
          line);
    }
  }
};

/// Inline (AoS) leaf storage: one array of {key, payload} records.
/// \tparam Traits The InternalTraits for the list.
template <typename Traits>
struct LeafStorage<Traits, /*kInline=*/true> {
  /// The key type.
  using T = typename Traits::key_type;

  /// The interleaved {key, payload} records.
  std::array<
      InlineLeafRecord<
          typename Traits::KeyStorage,
          typename Traits::PayloadStorage>,
      Traits::kMaxKeys>
      records_{};

  /// Loads the key at the given slot.
  /// \param slot The slot index.
  /// \returns The key value.
  FOLLY_ALWAYS_INLINE T loadKey(uint8_t slot) const {
    return static_cast<T>(records_[slot].key);
  }
  /// Stores a key at the given slot.
  /// \param slot The slot index.
  /// \param key The key to store.
  FOLLY_ALWAYS_INLINE void storeKey(uint8_t slot, const T& key) {
    records_[slot].key = key;
  }

  /// Loads the payload at the given slot.
  /// \tparam PayloadT The payload type to extract.
  /// \param slot The slot index.
  /// \returns The payload value.
  template <typename PayloadT>
  FOLLY_ALWAYS_INLINE PayloadT loadPayload(uint8_t slot) const {
    return static_cast<PayloadT>(records_[slot].payload);
  }
  /// Stores a payload at the given slot.
  /// \tparam PayloadT The payload type to store.
  /// \param slot The slot index.
  /// \param payload The payload to store.
  template <typename PayloadT>
  FOLLY_ALWAYS_INLINE void storePayload(uint8_t slot, const PayloadT& payload) {
    records_[slot].payload = payload;
  }

  /// Returns a const reference to the key slot storage at the given slot.
  /// \param slot The slot index.
  /// \returns A const reference to the key slot storage.
  FOLLY_ALWAYS_INLINE const typename Traits::KeyStorage& keyStorageAt(
      uint8_t slot) const {
    return records_[slot].key;
  }
  /// Returns a const reference to the payload slot storage at the given slot.
  /// \tparam PayloadT The payload type (defaults to the list's payload type).
  /// \param slot The slot index.
  /// \returns A const reference to the payload slot storage.
  template <typename PayloadT = typename Traits::payload_type>
  FOLLY_ALWAYS_INLINE const typename Traits::PayloadStorage& payloadStorageAt(
      uint8_t slot) const
    requires(!std::is_void_v<PayloadT>)
  {
    return records_[slot].payload;
  }

  /// Shifts records in [slot, numElements) one position to the right.
  /// \param slot The first slot to shift.
  /// \param numElements The current live element count.
  void shiftElementsRight(uint8_t slot, uint8_t numElements) {
    for (uint8_t i = numElements; i > slot; --i) {
      records_[i] = records_[i - 1];
    }
  }

  /// Clears the record at the given slot.
  /// \param slot The slot to clear.
  void clearElement(uint8_t slot) { records_[slot] = {}; }

  /// Registers the record array with the thread sanitizer.
  /// \param file Source file of the annotation site.
  /// \param line Source line of the annotation site.
  void annotateRaces(const char* file, int line) const {
    annotate_benign_race_sized(
        &records_,
        sizeof(records_),
        "BSkipList seqlock: readers validate inline key/payload storage",
        file,
        line);
  }
};

/// Leaf node holding sorted keys, optional payloads, and tombstone bits.
/// \tparam Traits The InternalTraits for the list.
template <typename Traits>
class BSkipNodeLeaf : public BSkipNode<Traits> {
  using typename BSkipNode<Traits>::T;

 public:
  /// Result of a leaf key search.
  struct LeafSearchResult {
    /// exactMatch=true: slot of the match. Otherwise: predecessor slot for
    /// the insert (n-1 if the new key would go past the end).
    uint8_t slot = 0;
    /// Whether the search key was found exactly.
    bool exactMatch = false;
  };

  /// Constructs a leaf node and registers its storage with the thread
  /// sanitizer.
  BSkipNodeLeaf() {
    annotate_benign_race_sized(
        &this->tombstones_,
        sizeof(this->tombstones_),
        "BSkipList seqlock: readers validate via version check",
        __FILE__,
        __LINE__);
    storage_.annotateRaces(__FILE__, __LINE__);
  }

  /// Returns this leaf's minimum key.
  /// \returns The key at slot 0.
  [[nodiscard]] T minKey() const { return loadKey(0); }

  /// Loads the key at the given slot.
  /// \param slot The slot index.
  /// \returns The key value.
  FOLLY_ALWAYS_INLINE T loadKey(uint8_t slot) const {
    return storage_.loadKey(slot);
  }
  /// Returns a const reference to the key slot storage at the given slot.
  /// \param slot The slot index.
  /// \returns A const reference to the key slot storage.
  FOLLY_ALWAYS_INLINE const typename Traits::KeyStorage& keyStorageAt(
      uint8_t slot) const {
    return storage_.keyStorageAt(slot);
  }

  /// Stores a key at the given slot.
  /// \param slot The slot index.
  /// \param key The key to store.
  FOLLY_ALWAYS_INLINE void storeKey(uint8_t slot, const T& key) {
    storage_.storeKey(slot, key);
  }

  /// Loads the payload at the given slot.
  /// \tparam RequestedPayload The payload type to extract.
  /// \param slot The slot index.
  /// \returns The payload value.
  template <typename RequestedPayload = typename Traits::payload_type>
  FOLLY_ALWAYS_INLINE RequestedPayload loadPayload(uint8_t slot) const
    requires(Traits::kHasPayload && !std::is_void_v<RequestedPayload>)
  {
    return storage_.template loadPayload<RequestedPayload>(slot);
  }
  /// Returns a const reference to the payload slot storage at the given slot.
  /// \tparam RequestedPayload The payload type.
  /// \param slot The slot index.
  /// \returns A const reference to the payload slot storage.
  template <typename RequestedPayload = typename Traits::payload_type>
  FOLLY_ALWAYS_INLINE const typename Traits::PayloadStorage& payloadStorageAt(
      uint8_t slot) const
    requires(Traits::kHasPayload && !std::is_void_v<RequestedPayload>)
  {
    return storage_.payloadStorageAt(slot);
  }

  /// Stores a payload at the given slot.
  /// \tparam RequestedPayload The payload type to store.
  /// \param slot The slot index.
  /// \param payload The payload to store.
  template <typename RequestedPayload = typename Traits::payload_type>
  FOLLY_ALWAYS_INLINE void storePayload(
      uint8_t slot, const RequestedPayload& payload)
    requires(Traits::kHasPayload && !std::is_void_v<RequestedPayload>)
  {
    storage_.template storePayload<RequestedPayload>(slot, payload);
  }

  /// Searches this leaf for a key.
  /// \tparam LookupKey The search key type.
  /// \param k The search key.
  /// \returns The match slot (or predecessor slot) and whether it was exact.
  template <typename LookupKey>
  LeafSearchResult findLeafSlot(const LookupKey& k) const {
    const uint8_t n = this->numElements_.load();
    DCHECK_GT(n, 0u) << "sentinel guarantees numElements_ >= 1";
    // HOH invariant: the crab-lock traversal guarantees k >= keys[0] before
    // entering this node. If this DCHECK fires, the descent chose the wrong
    // node — a bug in the traversal, not in this function.
    DCHECK(!Traits::less(k, loadKey(0)))
        << "findLeafSlot called with k < minKey — HOH "
           "invariant violated";
    // Differs from findChild's findFirstGreaterLinear call: we
    // need the loaded key value (not just its slot) for the equality check,
    // so inlining avoids a second loadKey per match.
    for (uint8_t i = 0; i < n; ++i) {
      T key = loadKey(i);
      if (!Traits::less(key, k)) {
        if (Traits::equal(key, k)) {
          return {.slot = i, .exactMatch = true};
        }
        return {.slot = (i > 0) ? static_cast<uint8_t>(i - 1) : uint8_t{0}};
      }
    }
    return {.slot = static_cast<uint8_t>(n - 1)};
  }

  /// Moves the tail of this leaf into a destination leaf during a split.
  /// \param dest The destination leaf receiving the moved elements.
  /// \param splitIndex The first slot to move out of this leaf.
  /// \param destSlot The first destination slot to write.
  /// \returns The number of elements moved.
  uint8_t splitKeys(
      BSkipNodeLeaf<Traits>* dest, uint8_t splitIndex, uint8_t destSlot) {
    DCHECK(this->seq_.isInWriteEpoch());
    DCHECK(dest->seq_.isInWriteEpoch());
    // destSlot=0: overflow split, dest is empty.
    // destSlot=1: promoted split, dest already has slot 0 populated.
    DCHECK_EQ(dest->numElements_.load(), destSlot);
    DCHECK_EQ(dest->tombstones_.load(), 0u);
    uint8_t oldNumElements = this->numElements_.load();
    uint8_t numElementsToMove = oldNumElements - splitIndex;
    DCHECK_LE(destSlot + numElementsToMove, Traits::kMaxKeys);

    for (uint8_t i = 0; i < numElementsToMove; i++) {
      copyElementTo(dest, splitIndex + i, destSlot + i);
    }

    // Shift guards below (e.g. splitIndex < kTWBits) prevent UB on
    // over-wide shifts; they are always true since kMaxKeys <= kTWBits
    // by static_assert above.
    using TW = typename Traits::TombstoneWord;
    constexpr int kTWBits = sizeof(TW) * 8;
    TW srcTombstones = tombstones_.load();
    TW movedTombstoneBits =
        splitIndex < kTWBits ? srcTombstones >> splitIndex : 0;
    if (numElementsToMove < kTWBits) {
      movedTombstoneBits &= (TW{1} << numElementsToMove) - 1;
    }
    TW destTombstoneBits =
        destSlot < kTWBits ? movedTombstoneBits << destSlot : 0;
    dest->tombstones_.store(destTombstoneBits);
    TW srcKeepMask = splitIndex < kTWBits ? (TW{1} << splitIndex) - 1 : ~TW{0};
    tombstones_.store(srcTombstones & srcKeepMask);

    this->numElements_.store(splitIndex);
    // Both nodes exclusively owned; plain store is sufficient.
    dest->numElements_.store(dest->numElements_.load() + numElementsToMove);

    for (uint8_t i = splitIndex; i < oldNumElements; ++i) {
      storage_.clearElement(i);
    }

    return numElementsToMove;
  }

  /// Reports whether the slot is tombstoned.
  ///
  /// Run under leaf mutex + seqlock write epoch; relaxed load.
  /// \param slot The slot to test.
  /// \returns true if the slot's tombstone bit is set.
  bool tombstoned(uint8_t slot) const {
    return (tombstones_.load() >> slot) & 1;
  }
  /// Sets the tombstone bit for the given slot.
  ///
  /// Run under leaf mutex + seqlock write epoch; relaxed store.
  /// \param slot The slot to tombstone.
  void setTombstone(uint8_t slot) {
    DCHECK(this->seq_.isInWriteEpoch());
    using TW = typename Traits::TombstoneWord;
    TW current = tombstones_.load();
    DCHECK_EQ((current >> slot) & 1, 0u)
        << "setTombstone on already-tombstoned";
    tombstones_.store(current | (TW{1} << slot));
  }
  /// Clears the tombstone bit for the given slot.
  /// \param slot The slot to un-tombstone.
  void clearTombstone(uint8_t slot) {
    DCHECK(this->seq_.isInWriteEpoch());
    using TW = typename Traits::TombstoneWord;
    TW current = tombstones_.load();
    DCHECK_EQ((current >> slot) & 1, 1u)
        << "clearTombstone on already-cleared slot";
    tombstones_.store(current & ~(TW{1} << slot));
  }

  /// Returns an adaptive read guard bound to this leaf.
  /// \returns An AdaptiveReadGuard for this node.
  AdaptiveReadGuard<Traits> adaptiveRead() {
    return AdaptiveReadGuard<Traits>{*this};
  }

  /// Pointer to a stored payload, or std::nullptr_t when there is none.
  using PayloadPtr = typename Traits::PayloadPtr;

  /// Inserts a key and payload at a slot and bumps the element count.
  ///
  /// Caller must already be in a seqlock write epoch (either via writeGuard
  /// or beginSplitSiblingWrite).
  /// \param slot The slot to insert at.
  /// \param key The key to insert.
  /// \param payload Pointer to the payload to insert, or nullptr.
  void insertKeyAtSlotRaw(
      uint8_t slot, const T& key, PayloadPtr payload = nullptr) {
    DCHECK(this->seq_.isInWriteEpoch());
    insertKeyAtSlot(slot, key);
    setPayload(slot, payload);
    this->numElements_.fetch_add(1);
  }

  /// Inserts a key and payload at a slot under a freshly opened write epoch.
  /// \param slot The slot to insert at.
  /// \param key The key to insert.
  /// \param payload Pointer to the payload to insert, or nullptr.
  void insertKeyAtSlotGuarded(
      uint8_t slot, const T& key, PayloadPtr payload = nullptr) {
    auto guard = this->seq_.writeGuard();
    insertKeyAtSlotRaw(slot, key, payload);
  }

 private:
  FOLLY_BSKIP_FRIEND_LIST;

  /// Shifts elements right and writes the key at the given slot.
  /// \param slot The slot to insert at.
  /// \param key The key to insert.
  void insertKeyAtSlot(uint8_t slot, const T& key) {
    using TW = typename Traits::TombstoneWord;
    // Caller must hold the seqlock write epoch (the keys_/payloads_ TSAN
    // benign-race annotation would otherwise hide a write-without-epoch bug).
    DCHECK(this->seq_.isInWriteEpoch());
    DCHECK_LT(slot, sizeof(TW) * 8) << "slot would shift past TW";
    const uint8_t numElements = this->numElements_.load();
    DCHECK_LE(numElements + 1, Traits::kMaxKeys);
    // Bit-shift below assumes tombstones at slots >= numElements are zero.
    DCHECK(
        numElements >= sizeof(TW) * 8 ||
        (tombstones_.load() >> numElements) == TW{0});
    storage_.shiftElementsRight(slot, numElements);
    TW upperMask = ~((TW{1} << slot) - 1u);
    TW tb = tombstones_.load();
    TW lower = tb & ~upperMask;
    TW upper = tb & upperMask;
    tombstones_.store(lower | (upper << 1));
    storeKey(slot, key);
  }

  /// Copies one element (key and payload) into a destination leaf.
  /// \param dest The destination leaf.
  /// \param srcSlot The source slot in this leaf.
  /// \param destSlot The destination slot in dest.
  void copyElementTo(
      BSkipNodeLeaf<Traits>* dest, uint8_t srcSlot, uint8_t destSlot) const {
    dest->storeKey(destSlot, loadKey(srcSlot));
    if constexpr (Traits::kHasPayload) {
      dest->storePayload(destSlot, loadPayload(srcSlot));
    }
  }

  /// Writes a payload at the given slot.
  ///
  /// Always writes (default-constructed if null) so insertKeyAtSlot doesn't
  /// need to pre-clear — one store per epoch instead of two.
  /// \param slot The slot to write.
  /// \param payload Pointer to the payload to store, or nullptr.
  void setPayload(uint8_t slot, PayloadPtr payload) {
    if constexpr (Traits::kHasPayload) {
      storePayload(slot, payload ? *payload : typename Traits::payload_type{});
    }
  }

  /// Per-slot tombstone bits.
  folly::relaxed_atomic<typename Traits::TombstoneWord> tombstones_;
  /// Key/payload storage for this leaf.
  LeafStorage<Traits> storage_{};
};

/// Internal (routing) node holding separator keys and child pointers.
/// \tparam Traits The InternalTraits for the list.
template <typename Traits>
class BSkipNodeInternal : public BSkipNode<Traits> {
  using typename BSkipNode<Traits>::T;
  using typename BSkipNode<Traits>::KeyStorage;

 public:
  /// Array of separator key slots.
  using KeyArray = std::array<KeyStorage, Traits::kMaxKeys>;

  /// Result of an internal node child search.
  struct InternalSearchResult {
    /// Child index to descend into. For inserts, the new key goes at slot + 1.
    uint8_t slot = 0;
    /// Item at keys_[slot] equals the search key.
    bool found = false;
  };

  /// Constructs an internal node and registers its arrays with the thread
  /// sanitizer.
  BSkipNodeInternal() {
    annotate_benign_race_sized(
        &keys_,
        sizeof(keys_),
        "BSkipList seqlock: readers validate via version check",
        __FILE__,
        __LINE__);
    annotate_benign_race_sized(
        &this->children_,
        sizeof(this->children_),
        "BSkipList OLC: readers validate via version check",
        __FILE__,
        __LINE__);
  }

  /// Returns this node's minimum routing key.
  /// \returns The key at slot 0.
  [[nodiscard]] T minKey() const { return static_cast<T>(keys_[0]); }

  /// Finds the child to descend into for a search key.
  ///
  /// Returns {child index to descend into, whether k was found as a routing
  /// key}.
  /// \tparam LookupKey The search key type.
  /// \param k The search key.
  /// \returns The child index and whether k matched a routing key.
  template <typename LookupKey>
  InternalSearchResult findChild(const LookupKey& k) const {
    const uint8_t n = this->numElements_.load();
    // OLC readers may observe a preallocated node before it's populated.
    if (n == 0) {
      return {};
    }
    uint8_t i = bskip_detail::findFirstGreaterLinear<Traits>(
        1, n, k, [&](uint8_t slot) { return static_cast<T>(keys_[slot]); });
    InternalSearchResult result;
    result.slot = static_cast<uint8_t>(i - 1);
    result.found = Traits::equal(static_cast<T>(keys_[result.slot]), k);
    return result;
  }

  /// Moves the tail of this node's keys and children into a destination node.
  /// \param dest The destination node receiving the moved keys and children.
  /// \param splitIndex The first slot to move out of this node.
  /// \param destSlot The first destination slot to write.
  void splitKeysAndChildren(
      BSkipNodeInternal<Traits>* dest, uint8_t splitIndex, uint8_t destSlot) {
    DCHECK(this->seq_.isInWriteEpoch());
    DCHECK(dest->seq_.isInWriteEpoch());
    DCHECK_EQ(dest->numElements_.load(), destSlot);
    uint8_t numElementsToMove = this->numElements_.load() - splitIndex;
    DCHECK_LE(destSlot + numElementsToMove, Traits::kMaxKeys);

    for (uint8_t i = 0; i < numElementsToMove; ++i) {
      dest->keys_[destSlot + i] = keys_[splitIndex + i];
      dest->children_[destSlot + i] = children_[splitIndex + i];
    }

    this->numElements_.store(splitIndex);
    dest->numElements_.store(dest->numElements_.load() + numElementsToMove);
  }

  /// Shifts separator keys right and writes a key at the given slot.
  /// \param slot The slot to insert at.
  /// \param key The routing key to insert.
  void insertKeyAtSlot(uint8_t slot, const T& key) {
    DCHECK(this->seq_.isInWriteEpoch());
    DCHECK_LE(this->numElements_.load() + 1, Traits::kMaxKeys);

    for (uint8_t i = this->numElements_.load(); i > slot; --i) {
      keys_[i] = keys_[i - 1];
    }
    keys_[slot] = key;
  }

  /// Shifts children right and writes a child pointer at the given slot.
  ///
  /// Caller increments numElements_ after this returns; postInsertCount is the
  /// shift range (current count + 1).
  /// \param insertionSlot The slot to insert the child at.
  /// \param child The child pointer to insert.
  void insertChildAtSlot(uint8_t insertionSlot, BSkipNode<Traits>* child) {
    DCHECK(this->seq_.isInWriteEpoch());
    const uint8_t postInsertCount = this->numElements_.load() + 1;
    DCHECK_LE(postInsertCount, Traits::kMaxKeys);
    for (uint8_t i = postInsertCount - 1; i > insertionSlot; --i) {
      children_[i] = children_[i - 1];
    }
    children_[insertionSlot] = child;
  }

 private:
  FOLLY_BSKIP_FRIEND_LIST;
  // B-tree internal node layout: keys_[i] is the separator between
  // children_[i-1] and children_[i]. keys_[0] is this node's minimum
  // routing key, set at creation. Search starts at keys_[1] because
  // target >= keys_[0] is guaranteed by the HOH descent from the parent.
  KeyArray keys_{};
  std::array<AtomicSlot<BSkipNode<Traits>*>, Traits::kMaxKeys> children_{};
};

#undef FOLLY_BSKIP_FRIEND_LIST

} // namespace folly::bskip_detail
