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
#include <utility>

#include <folly/Traits.h>
#include <folly/synchronization/AsymmetricThreadFence.h>
#include <folly/synchronization/Hazptr-fwd.h>
#include <folly/synchronization/HazptrDomain.h>
#include <folly/synchronization/HazptrRec.h>
#include <folly/synchronization/HazptrThrLocal.h>

namespace folly {

///
/// Classes related to hazard pointer holders.
///

/**
 *  hazptr_holder
 *
 *  Class for automatic acquisition and release of hazard pointers,
 *  and interface for hazard pointer operations.
 *
 *  Usage example:
 *    T* ptr;
 *    {
 *      hazptr_holder h = make_hazard_pointer();
 *      ptr = h.protect(src);
 *      //  ... *ptr is protected ...
 *      h.reset_protection();
 *      // ... *ptr is not protected ...
 *      ptr = src.load();
 *      while (!h.try_protect(ptr, src)) {}
 *      // ... *ptr is protected ...
 *    }
 *    // ... *ptr is not protected
 */
template <template <typename> class Atom>
class hazptr_holder {
  hazptr_rec<Atom>* hprec_;

  template <uint8_t M, template <typename> class A>
  friend class hazptr_local;
  friend hazptr_holder<Atom> make_hazard_pointer<Atom>(hazptr_domain<Atom>&);
  /// Grants the array factory access to the private holder constructor.
  ///
  /// \returns A hazptr_array owning `M` hazard pointer records.
  template <uint8_t M, template <typename> class A>
  friend hazptr_array<M, A> make_hazard_pointer_array();

  /// Private constructor used by make_hazard_pointer and
  /// make_hazard_pointer_array.
  ///
  /// \param hprec The hazard pointer record to take ownership of.
  FOLLY_ALWAYS_INLINE explicit hazptr_holder(hazptr_rec<Atom>* hprec)
      : hprec_(hprec) {}

 public:
  /// Constructs an empty holder that owns no hazard pointer.
  FOLLY_ALWAYS_INLINE hazptr_holder() noexcept : hprec_(nullptr) {}

  /** For nonempty construction use make_hazard_pointer. */

  /// Move-constructs a holder, transferring ownership from another.
  ///
  /// \param rhs The holder to move from; left empty afterwards.
  FOLLY_ALWAYS_INLINE hazptr_holder(hazptr_holder&& rhs) noexcept
      : hprec_(std::exchange(rhs.hprec_, nullptr)) {}

  /// Deleted copy constructor; holders are not copyable.
  hazptr_holder(const hazptr_holder& rhs) = delete;
  /// Deleted copy assignment; holders are not copyable.
  hazptr_holder& operator=(const hazptr_holder& rhs) = delete;

  /// Destroys the holder, releasing any owned hazard pointer.
  FOLLY_ALWAYS_INLINE ~hazptr_holder() {
    if (FOLLY_LIKELY(hprec_ != nullptr)) {
      hprec_->reset_hazptr();
      auto domain = hprec_->domain();
#if FOLLY_HAZPTR_THR_LOCAL
      if (FOLLY_LIKELY(domain->is_default_domain())) {
        if (FOLLY_LIKELY(hazptr_tc_tls<Atom>().try_put(hprec_))) {
          return;
        }
      }
#endif
      domain->release_hprec(hprec_);
    }
  }

  /// Move-assigns the holder, transferring ownership from another.
  ///
  /// \param rhs The holder to move from; left empty afterwards.
  /// \returns A reference to this holder.
  FOLLY_ALWAYS_INLINE hazptr_holder& operator=(hazptr_holder&& rhs) noexcept {
    /* Self-move is a no-op.  */
    if (FOLLY_LIKELY(this != &rhs)) {
      this->~hazptr_holder();
      new (this) hazptr_holder(rhs.hprec_);
      rhs.hprec_ = nullptr;
    }
    return *this;
  }

  /** Hazard pointer operations */

  /// Tries to protect the pointer loaded from an atomic source.
  ///
  /// \param ptr Receives the protected pointer on success.
  /// \param src The atomic source to load the pointer from.
  /// \returns true if protection succeeded, false if the source changed.
  template <typename T>
  FOLLY_ALWAYS_INLINE bool try_protect(T*& ptr, const Atom<T*>& src) noexcept {
    return try_protect(
        ptr,
        [&]() noexcept { return src.load(std::memory_order_acquire); },
        [](T* t) noexcept { return t; });
  }

  /// Tries to protect a pointer obtained from a source callable, filtered by a
  /// function.
  ///
  /// \param ptr Receives the protected pointer on success.
  /// \param src Callable that returns the current pointer value.
  /// \param f Function applied to derive the object pointer to protect.
  /// \returns true if protection succeeded, false if the source changed.
  template <typename T, typename Src, typename Func>
    requires std::is_invocable_r_v<T*, Src&>
  FOLLY_ALWAYS_INLINE bool try_protect(T*& ptr, Src&& src, Func f) noexcept(
      noexcept(std::declval<Src&>()()) &&
      noexcept(std::declval<Func&>()(std::declval<T*>()))) {
    auto p = ptr;
    reset_protection(f(p));
    /*** Full fence ***/ folly::asymmetric_thread_fence_light(
        std::memory_order_seq_cst);
    ptr = src();
    if (FOLLY_UNLIKELY(p != ptr)) {
      reset_protection();
      return false;
    }
    return true;
  }

  /// Tries to protect a pointer obtained from a source callable.
  ///
  /// \param ptr Receives the protected pointer on success.
  /// \param src Callable that returns the current pointer value.
  /// \returns true if protection succeeded, false if the source changed.
  template <typename T, typename Src>
    requires std::is_invocable_r_v<T*, Src&>
  FOLLY_ALWAYS_INLINE bool try_protect(T*& ptr, Src&& src) noexcept(
      noexcept(src())) {
    return try_protect(ptr, std::forward<Src>(src), [](T* t) noexcept {
      return t;
    });
  }

  /// Tries to protect the pointer loaded from an atomic source, filtered by a
  /// function.
  ///
  /// \param ptr Receives the protected pointer on success.
  /// \param src The atomic source to load the pointer from.
  /// \param f Function applied to derive the object pointer to protect.
  /// \returns true if protection succeeded, false if the source changed.
  template <typename T, typename Func>
  FOLLY_ALWAYS_INLINE bool try_protect(
      T*& ptr, const Atom<T*>& src, Func f) noexcept {
    /* Filtering the protected pointer through function Func is useful
       for stealing bits of the pointer word */
    return try_protect(
        ptr, [&]() noexcept { return src.load(std::memory_order_acquire); }, f);
  }

  /// Protects and returns the pointer loaded from an atomic source, retrying
  /// until it succeeds.
  ///
  /// \param src The atomic source to load the pointer from.
  /// \returns The protected pointer.
  template <typename T>
  FOLLY_ALWAYS_INLINE T* protect(const Atom<T*>& src) noexcept {
    return protect(src, [](T* t) { return t; });
  }

  /// Protects and returns a pointer loaded from an atomic source, filtered by a
  /// function, retrying until it succeeds.
  ///
  /// \param src The atomic source to load the pointer from.
  /// \param f Function applied to derive the object pointer to protect.
  /// \returns The protected pointer.
  template <typename T, typename Func>
  FOLLY_ALWAYS_INLINE T* protect(const Atom<T*>& src, Func f) noexcept {
    T* ptr = src.load(std::memory_order_relaxed);
    while (!try_protect(ptr, src, f)) {
      /* Keep trying */
    }
    return ptr;
  }

  /// Sets the protected object to the given pointer.
  ///
  /// \param ptr The object to protect.
  template <typename T>
  FOLLY_ALWAYS_INLINE void reset_protection(const T* ptr) noexcept {
    auto p = static_cast<hazptr_obj<Atom>*>(const_cast<T*>(ptr));
    // UB if *this is empty
    DCHECK(hprec_) << "initialize hazptr_holder with make_hazard_pointer()";
    hprec_->reset_hazptr(p);
  }

  /// Clears any protection held by this holder.
  ///
  /// \param null Placeholder null pointer argument.
  FOLLY_ALWAYS_INLINE void reset_protection(
      std::nullptr_t null = nullptr) noexcept {
    // UB if *this is empty
    DCHECK(hprec_) << "initialize hazptr_holder with make_hazard_pointer()";
    hprec_->reset_hazptr();
  }

  /// Swaps ownership of hazard pointers between two holders.
  ///
  /// The owned hazard pointers remain unmodified during the swap and continue
  /// to protect the objects they were protecting before, if any.
  ///
  /// \param rhs The holder to swap with.
  FOLLY_ALWAYS_INLINE void swap(hazptr_holder<Atom>& rhs) noexcept {
    std::swap(this->hprec_, rhs.hprec_);
  }

  /// Returns a pointer to the owned hazptr_rec.
  ///
  /// \returns The owned hazard pointer record, or nullptr if empty.
  FOLLY_ALWAYS_INLINE hazptr_rec<Atom>* hprec() const noexcept {
    return hprec_;
  }

  /// Sets the pointer to the owned hazptr_rec.
  ///
  /// \param hprec The hazard pointer record to own.
  FOLLY_ALWAYS_INLINE void set_hprec(hazptr_rec<Atom>* hprec) noexcept {
    hprec_ = hprec;
  }
}; // hazptr_holder

/// Constructs a nonempty hazard pointer holder in the given domain.
///
/// \param domain The hazard pointer domain to acquire a record from.
/// \returns A holder owning a fresh hazard pointer record.
template <template <typename> class Atom>
FOLLY_ALWAYS_INLINE hazptr_holder<Atom> make_hazard_pointer(
    hazptr_domain<Atom>& domain) {
#if FOLLY_HAZPTR_THR_LOCAL
  if (FOLLY_LIKELY(domain.is_default_domain())) {
    auto hprec = hazptr_tc_tls<Atom>().try_get();
    if (FOLLY_LIKELY(hprec != nullptr)) {
      return hazptr_holder<Atom>(hprec);
    }
  }
#endif
  auto hprec = domain.acquire_hprecs(1);
  DCHECK(hprec);
  DCHECK(hprec->next_avail() == nullptr);
  return hazptr_holder<Atom>(hprec);
}

/// Swaps two hazard pointer holders.
///
/// \param lhs The first holder.
/// \param rhs The second holder.
template <template <typename> class Atom>
FOLLY_ALWAYS_INLINE void swap(
    hazptr_holder<Atom>& lhs, hazptr_holder<Atom>& rhs) noexcept {
  lhs.swap(rhs);
}

/**
 *  Type used by hazptr_array and hazptr_local.
 */
template <template <typename> class Atom>
using aligned_hazptr_holder = aligned_storage_for_t<hazptr_holder<Atom>>;

/**
 *  hazptr_array
 *
 *  Optimized template for bulk construction and destruction of hazard
 *  pointers.
 *
 *  WARNING: Do not move from or to individual hazptr_holder-s.
 *  Only move the whole hazptr_array.
 *
 *  NOTE: It is allowed to swap an individual hazptr_holder that
 *  belongs to hazptr_array with (a) a hazptr_holder object, or (b) a
 *  hazptr_holder that is part of hazptr_array, under the conditions:
 *  (i) both hazptr_holder-s are either both empty or both nonempty
 *  and (ii) both belong to the same domain.
 */
template <uint8_t M, template <typename> class Atom>
class hazptr_array {
  static_assert(M > 0, "M must be a positive integer.");

  aligned_hazptr_holder<Atom> raw_[M];
  bool empty_{true};

  friend hazptr_array<M, Atom> make_hazard_pointer_array<M, Atom>();

  /** Private constructor used by make_hazard_pointer_array */
  FOLLY_ALWAYS_INLINE explicit hazptr_array(std::nullptr_t) noexcept {}

 public:
  /** Default empty constructor */
  FOLLY_ALWAYS_INLINE hazptr_array() noexcept {
    auto h = reinterpret_cast<hazptr_holder<Atom>*>(&raw_);
    for (uint8_t i = 0; i < M; ++i) {
      new (&h[i]) hazptr_holder<Atom>();
    }
  }

  /** For nonempty construction use make_hazard_pointer_array. */

  /// Move-constructs an array, transferring all holders from another.
  ///
  /// \param other The array to move from; left empty afterwards.
  FOLLY_ALWAYS_INLINE hazptr_array(hazptr_array&& other) noexcept {
    auto h = reinterpret_cast<hazptr_holder<Atom>*>(&raw_);
    auto hother = reinterpret_cast<hazptr_holder<Atom>*>(&other.raw_);
    for (uint8_t i = 0; i < M; ++i) {
      new (&h[i]) hazptr_holder<Atom>(std::move(hother[i]));
    }
    empty_ = other.empty_;
    other.empty_ = true;
  }

  /// Deleted copy constructor; arrays are not copyable.
  hazptr_array(const hazptr_array& other) = delete;
  /// Deleted copy assignment; arrays are not copyable.
  hazptr_array& operator=(const hazptr_array& other) = delete;

  /// Destroys the array, releasing all owned hazard pointers.
  FOLLY_ALWAYS_INLINE ~hazptr_array() {
    if (empty_) {
      return;
    }
    auto h = reinterpret_cast<hazptr_holder<Atom>*>(&raw_);
#if FOLLY_HAZPTR_THR_LOCAL
    auto& tc = hazptr_tc_tls<Atom>();
    auto count = tc.count();
    auto cap = hazptr_tc<Atom>::min_capacity();
    if (FOLLY_UNLIKELY((M + count) > cap)) {
      tc.evict((M + count) - cap);
      count = cap - M;
    }
    for (uint8_t i = 0; i < M; ++i) {
      h[i].reset_protection();
      tc[count + i].fill(h[i].hprec());
      h[i].set_hprec(nullptr);
    }
    tc.set_count(count + M);
#else
    for (uint8_t i = 0; i < M; ++i) {
      h[i].~hazptr_holder();
    }
#endif
  }

  /// Move-assigns the array, transferring all holders from another.
  ///
  /// \param other The array to move from; left empty afterwards.
  /// \returns A reference to this array.
  FOLLY_ALWAYS_INLINE hazptr_array& operator=(hazptr_array&& other) noexcept {
    auto h = reinterpret_cast<hazptr_holder<Atom>*>(&raw_);
    for (uint8_t i = 0; i < M; ++i) {
      h[i] = std::move(other[i]);
    }
    empty_ = other.empty_;
    other.empty_ = true;
    return *this;
  }

  /// Returns the holder at the given index.
  ///
  /// \param i The index of the holder to access.
  /// \returns A reference to the holder at index `i`.
  FOLLY_ALWAYS_INLINE hazptr_holder<Atom>& operator[](uint8_t i) noexcept {
    auto h = reinterpret_cast<hazptr_holder<Atom>*>(&raw_);
    DCHECK(i < M);
    return h[i];
  }
}; // hazptr_array

/// Constructs a nonempty array of `M` hazard pointers.
///
/// \returns A hazptr_array owning `M` hazard pointer records.
template <uint8_t M, template <typename> class Atom>
FOLLY_ALWAYS_INLINE hazptr_array<M, Atom> make_hazard_pointer_array() {
  hazptr_array<M, Atom> a(nullptr);
  auto h = reinterpret_cast<hazptr_holder<Atom>*>(&a.raw_);
#if FOLLY_HAZPTR_THR_LOCAL
  static_assert(
      M <= hazptr_tc<Atom>::min_capacity(),
      "M must be within the thread cache capacity.");
  auto& tc = hazptr_tc_tls<Atom>();
  auto count = tc.count();
  if (FOLLY_UNLIKELY(M > count)) {
    tc.fill(M - count);
    count = M;
  }
  size_t offset = count - M;
  for (uint8_t i = 0; i < M; ++i) {
    auto hprec = tc[offset + i].get();
    DCHECK(hprec != nullptr);
    new (&h[i]) hazptr_holder<Atom>(hprec);
  }
  tc.set_count(offset);
#else
  auto hprec = hazard_pointer_default_domain<Atom>().acquire_hprecs(M);
  for (uint8_t i = 0; i < M; ++i) {
    DCHECK(hprec);
    auto next = hprec->next_avail();
    hprec->set_next_avail(nullptr);
    new (&h[i]) hazptr_holder<Atom>(hprec);
    hprec = next;
  }
  DCHECK(hprec == nullptr);
#endif
  a.empty_ = false;
  return a;
}

/**
 *  hazptr_local
 *
 *  Optimized for construction and destruction of one or more
 *  nonempty hazptr_holder-s with local scope.
 *
 *  WARNING 1: Do not move from or to individual hazptr_holder-s.
 *
 *  WARNING 2: There can only be one hazptr_local active for the same
 *  thread at any time. This is not tracked and checked by the
 *  implementation (except in debug mode) because it would negate the
 *  performance gains of this class.
 */
template <uint8_t M, template <typename> class Atom>
class hazptr_local {
  static_assert(M > 0, "M must be a positive integer.");

  aligned_hazptr_holder<Atom> raw_[M];

 public:
  /** Constructor */
  FOLLY_ALWAYS_INLINE hazptr_local() {
    auto h = reinterpret_cast<hazptr_holder<Atom>*>(&raw_);
#if FOLLY_HAZPTR_THR_LOCAL
    static_assert(
        M <= hazptr_tc<Atom>::min_capacity(),
        "M must be <= hazptr_tc::min_capacity().");
    auto& tc = hazptr_tc_tls<Atom>();
    auto count = tc.count();
    if (FOLLY_UNLIKELY(M > count)) {
      tc.fill(M - count);
    }
    if (kIsDebug) {
      DCHECK(!tc.local());
      tc.set_local(true);
    }
    for (uint8_t i = 0; i < M; ++i) {
      auto hprec = tc[i].get();
      DCHECK(hprec != nullptr);
      new (&h[i]) hazptr_holder<Atom>(hprec);
    }
#else
    for (uint8_t i = 0; i < M; ++i) {
      new (&h[i]) hazptr_holder<Atom>(make_hazard_pointer<Atom>());
    }
#endif
  }

  /// Deleted copy constructor; local arrays are not copyable.
  hazptr_local(const hazptr_local& other) = delete;
  /// Deleted copy assignment; local arrays are not copyable.
  hazptr_local& operator=(const hazptr_local& other) = delete;
  /// Deleted move constructor; local arrays are not movable.
  hazptr_local(hazptr_local&& other) = delete;
  /// Deleted move assignment; local arrays are not movable.
  hazptr_local& operator=(hazptr_local&& other) = delete;

  /// Destroys the local array, releasing all owned hazard pointers.
  FOLLY_ALWAYS_INLINE ~hazptr_local() {
    auto h = reinterpret_cast<hazptr_holder<Atom>*>(&raw_);
#if FOLLY_HAZPTR_THR_LOCAL
    if (kIsDebug) {
      auto& tc = hazptr_tc_tls<Atom>();
      DCHECK(tc.local());
      tc.set_local(false);
    }
    for (uint8_t i = 0; i < M; ++i) {
      h[i].reset_protection();
    }
#else
    for (uint8_t i = 0; i < M; ++i) {
      h[i].~hazptr_holder();
    }
#endif
  }

  /// Returns the holder at the given index.
  ///
  /// \param i The index of the holder to access.
  /// \returns A reference to the holder at index `i`.
  FOLLY_ALWAYS_INLINE hazptr_holder<Atom>& operator[](uint8_t i) noexcept {
    auto h = reinterpret_cast<hazptr_holder<Atom>*>(&raw_);
    DCHECK(i < M);
    return h[i];
  }
}; // hazptr_local

} // namespace folly
