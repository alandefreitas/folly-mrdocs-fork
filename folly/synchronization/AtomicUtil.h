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
#include <cstdint>

#include <folly/Portability.h>
#include <folly/Traits.h>

namespace folly {

namespace detail {
struct atomic_value_type_alias_ {
  template <typename Atomic>
  using apply = typename Atomic::value_type;
};
struct atomic_value_type_load_ {
  template <typename Atomic>
  using apply = decltype(std::declval<Atomic const&>().load());
};
} // namespace detail

//  atomic_value_type_t
//  atomic_value_type
/// The effective value type of an atomic-like type.
///
/// Either the member type alias `value_type` or the return type of the member
/// function `load`.
template <typename Atomic>
using atomic_value_type_t = typename conditional_t<
    is_detected_v<detail::atomic_value_type_alias_::apply, Atomic>,
    detail::atomic_value_type_alias_,
    detail::atomic_value_type_load_>::template apply<Atomic>;
/// Trait giving the effective value type of an atomic-like type.
template <typename Atomic>
struct atomic_value_type {
  /// The effective value type of `Atomic`.
  using type = atomic_value_type_t<Atomic>;
};

/// The load part of a possibly-composite memory order.
///
/// \param order The memory order to reduce.
/// \returns The memory order to use for the load.
constexpr std::memory_order memory_order_load( //
    std::memory_order order) noexcept;

/// The store part of a possibly-composite memory order.
///
/// \param order The memory order to reduce.
/// \returns The memory order to use for the store.
constexpr std::memory_order memory_order_store(
    std::memory_order order) noexcept;

/// Compare-exchange (weak) that works around a TSAN bug in the standard
/// library version.
///
/// Workaround for https://github.com/google/sanitizers/issues/970. Mimics
/// `std::atomic_compare_exchange_weak`.
///
/// \param obj The atomic object to operate on.
/// \param expected Pointer to the expected value; updated on failure.
/// \param desired The value to store on success.
/// \param succ The memory order on success.
/// \param fail The memory order on failure.
/// \returns true if the exchange succeeded, otherwise false.
template <template <typename> class Atom = std::atomic, typename T>
bool atomic_compare_exchange_weak_explicit(
    Atom<T>* obj,
    type_t<T>* expected,
    type_t<T> desired,
    std::memory_order succ,
    std::memory_order fail);

/// Compare-exchange (strong) that works around a TSAN bug in the standard
/// library version.
///
/// Workaround for https://github.com/google/sanitizers/issues/970. Mimics
/// `std::atomic_compare_exchange_strong`.
///
/// \param obj The atomic object to operate on.
/// \param expected Pointer to the expected value; updated on failure.
/// \param desired The value to store on success.
/// \param succ The memory order on success.
/// \param fail The memory order on failure.
/// \returns true if the exchange succeeded, otherwise false.
template <template <typename> class Atom = std::atomic, typename T>
bool atomic_compare_exchange_strong_explicit(
    Atom<T>* obj,
    type_t<T>* expected,
    type_t<T> desired,
    std::memory_order succ,
    std::memory_order fail);

/// Sets the bit at the given index to 1 and returns its previous value.
///
/// Equivalent to `Atomic::fetch_or` with a mask. Uses an optimized
/// implementation when available (the `bts` instruction for `std::atomic` on
/// x86), otherwise falling back to `Atomic::fetch_or` with a mask.
/// \implementationdefined
struct atomic_fetch_set_fn {
  template <typename Atomic>
  bool operator()(
      Atomic& atomic,
      std::size_t bit,
      std::memory_order order = std::memory_order_seq_cst) const;
};
/// Customization point object that sets a bit and returns its previous value.
inline constexpr atomic_fetch_set_fn atomic_fetch_set{};

/// Resets the bit at the given index to 0 and returns its previous value.
///
/// Equivalent to `Atomic::fetch_and` with a mask. Uses an optimized
/// implementation when available (the `btr` instruction for `std::atomic` on
/// x86), otherwise falling back to `Atomic::fetch_and` with a mask.
/// \implementationdefined
struct atomic_fetch_reset_fn {
  template <typename Atomic>
  bool operator()(
      Atomic& atomic,
      std::size_t bit,
      std::memory_order order = std::memory_order_seq_cst) const;
};
/// Customization point object that resets a bit and returns its previous value.
inline constexpr atomic_fetch_reset_fn atomic_fetch_reset{};

/// Flips the bit at the given index and returns its previous value.
///
/// Equivalent to `Atomic::fetch_xor` with a mask. Uses an optimized
/// implementation when available (the `btc` instruction for `std::atomic` on
/// x86), otherwise falling back to `Atomic::fetch_xor` with a mask.
/// \implementationdefined
struct atomic_fetch_flip_fn {
  template <typename Atomic>
  bool operator()(
      Atomic& atomic,
      std::size_t bit,
      std::memory_order order = std::memory_order_seq_cst) const;
};
/// Customization point object that flips a bit and returns its previous value.
inline constexpr atomic_fetch_flip_fn atomic_fetch_flip{};

/// Atomically loads, transforms, and stores an atomic value with no race.
///
/// The atomic value is loaded, passed to the operation `op`, and the result is
/// stored, without risk of race from interleaving accesses. The implementation
/// is a compare-exchange loop, intended for use when other specialized forms of
/// atomic-fetch-modify are inapplicable. `op` may be called any number of times
/// (at least once) and is expected to be free of side effects. Does not attempt
/// to handle ABA scenarios.
/// \implementationdefined
struct atomic_fetch_modify_fn {
  template <typename Atomic, typename Op>
  atomic_value_type_t<Atomic> operator()(
      Atomic& atomic,
      Op op,
      std::memory_order mo = std::memory_order_seq_cst) const;
};
/// Customization point object that atomically transforms an atomic value.
inline constexpr atomic_fetch_modify_fn atomic_fetch_modify{};

/// Trait giving the thread-fence function for an atomic template.
template <template <typename> class Atom>
struct atomic_thread_fence_traits;

/// Specialization of `atomic_thread_fence_traits` for `std::atomic`.
template <>
struct atomic_thread_fence_traits<std::atomic> {
  /// The thread-fence function for `std::atomic`.
  static inline constexpr auto fence = std::atomic_thread_fence;
};

} // namespace folly

#include <folly/synchronization/AtomicUtil-inl.h>
