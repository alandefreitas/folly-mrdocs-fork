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

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

#include <folly/SharedMutex.h>
#include <folly/ThreadLocal.h>
#include <folly/Utility.h>
#include <folly/lang/Access.h>
#include <folly/synchronization/Lock.h>
#include <folly/synchronization/RelaxedAtomic.h>

namespace folly {

/// A synchronized value with a per-thread cache for accelerated reads.
///
/// Roughly equivalent to Synchronized, but with a per-thread cache for
/// acceleration.
///
/// Use in hot code when Synchronized alone, with its shared lock and unlock,
/// would be too costly.
///
/// Avoid when acceleration is marginal since per-thread caches are expensive.
///
/// Example:
///
///    struct writer_and_readers {
///      folly::thread_cached_synchronized<std::shared_ptr<data>> obj_;
///      std::jthread background_writer_{std::bind(loop_update, this)};
///
///      std::shared_ptr<data> get_recent_data_fast() { return obj; }
///
///      data fetch_recent_data();
///      bool needs_data_and_not_signaled_done();
///      void loop_update() {
///        while (needs_data_and_not_signaled_done()) {
///          obj.exchange(folly::copy_to_shared_ptr(fetch_recent_data()));
///        }
///      }
///    };
///
/// Note:
///    A singleton variation of this with SingletonThreadLocal would remove one
///    of the branches when looking up the per-thread cache but would introduce
///    a new branch when looking up the global version.
template <typename T, typename Mutex = SharedMutex>
class thread_cached_synchronized {
  static_assert(std::is_same<std::decay_t<T>, T>::value, "not decayed");
  static_assert(std::is_copy_constructible<T>::value, "not copy-constructible");

 public:
  /// The stored value type.
  using value_type = T;

 private:
  using version_type = std::uint64_t; // 64 bits will not overflow

  struct truth_state {
    relaxed_atomic<version_type> version{0}; // tiny optimization if first field
    Mutex mutex{}; // protects value and sometimes version
    value_type value;

    template <typename... A>
    truth_state(A&&... a) noexcept(
        std::is_nothrow_constructible<Mutex>{} &&
        std::is_nothrow_constructible<value_type, A...>{})
        : value{static_cast<A&&>(a)...} {}
  };

  struct cache_state {
    version_type version{0};
    value_type value;

    template <typename... A>
    cache_state(A&&... a) //
        noexcept(std::is_nothrow_constructible<value_type, A...>{})
        : value{static_cast<A&&>(a)...} {}
  };
  using tlp_cache_state = ThreadLocalPtr<cache_state>;

  template <typename... A>
  static constexpr bool nx =
      noexcept(truth_state{std::in_place, FOLLY_DECLVAL(A)...});

  template <bool C>
  using if_ = std::enable_if_t<C, int>;

  using swap_fn = access::swap_fn;

 public:
  /// Default-constructs the stored value.
  ///
  /// \tparam A Value type, deduced; must be default-constructible.
  template <typename A = value_type, if_<std::is_constructible<A>{}> = 0>
  thread_cached_synchronized() noexcept(nx<>) : truth_{std::in_place} {}
  /// Constructs the stored value by copying the given value.
  ///
  /// \param a Value to copy.
  explicit thread_cached_synchronized(value_type const& a) //
      noexcept(nx<value_type const&>)
      : truth_{a} {}
  /// Constructs the stored value by moving the given value.
  ///
  /// \param a Value to move from.
  explicit thread_cached_synchronized(value_type&& a) noexcept(nx<value_type&&>)
      : truth_{static_cast<value_type&&>(a)} {}
  /// Constructs the stored value from a single convertible argument.
  ///
  /// \tparam A Argument type used to construct the value.
  /// \param a Argument forwarded to the value constructor.
  template <typename A, if_<std::is_constructible<value_type, A>{}> = 0>
  explicit thread_cached_synchronized(A&& a) noexcept(nx<A&&>)
      : truth_{static_cast<A&&>(a)} {}
  /// Constructs the stored value in place from the given arguments.
  ///
  /// \tparam A Argument types used to construct the value.
  /// \param tag In-place construction tag.
  /// \param a Arguments forwarded to the value constructor.
  template <typename... A, if_<std::is_constructible<value_type, A...>{}> = 0>
  explicit thread_cached_synchronized(std::in_place_t tag, A&&... a) noexcept(
      nx<A&&...>)
      : truth_{static_cast<A&&>(a)...} {}

  /// Assigns a new value to the synchronized store.
  ///
  /// \tparam A Assigned argument type.
  /// \param a Value to assign.
  /// \returns A reference to this object.
  template <typename A, if_<std::is_assignable<value_type&, A>{}> = 0>
  thread_cached_synchronized& operator=(A && a) noexcept(false) {
    store(static_cast<A&&>(a));
    return *this;
  }

  /// Replaces the stored value under the lock and invalidates caches.
  ///
  /// \tparam A Argument type; defaults to the value type.
  /// \param a New value to store.
  template <typename A = value_type>
  void store(A&& a = A{}) {
    mutate([&](auto& value) { value = static_cast<A&&>(a); });
  }

  /// Replaces the stored value and returns the previous one.
  ///
  /// \tparam A Argument type; defaults to the value type.
  /// \param a New value to store.
  /// \returns The previous value.
  template <typename A = value_type>
  value_type exchange(A&& a = A{}) {
    return mutate([&](auto& value) { //
      return std::exchange(value, static_cast<A&&>(a));
    });
  }

  /// Atomically replaces the stored value if it equals the expected value.
  ///
  /// \tparam A Type of the desired value.
  /// \param expected Value to compare against; updated with the current value
  /// on failure.
  /// \param desired Value to store on success.
  /// \returns True if the exchange succeeded.
  template <typename A>
  bool compare_exchange(value_type& expected, A&& desired) {
    return mutate_cx(expected, static_cast<A&&>(desired));
  }

  /// Swaps the stored value with the given object.
  ///
  /// \tparam A Type of the object to swap with.
  /// \param that Object to swap with the stored value.
  template <typename A>
  void swap(A& that) noexcept(false) {
    mutate([&](auto& value) { access::swap(value, that); });
  }

  /// Swaps the stored value of a synchronized object with another object.
  ///
  /// \tparam A Type of the object to swap with.
  /// \param self Synchronized object whose value is swapped.
  /// \param that Object to swap with the stored value.
  template <typename A, if_<std::is_invocable_v<swap_fn, value_type&, A&>> = 0>
  friend void swap(thread_cached_synchronized& self, A& that) noexcept(false) {
    self.swap(that);
  }

  /// Returns a const reference to the cached stored value.
  ///
  /// \returns A const reference to the value.
  value_type const& operator*() const { return ref(); }
  /// Accesses members of the cached stored value.
  ///
  /// \returns A const pointer to the value.
  value_type const* operator->() const { return std::addressof(ref()); }
  /// Returns a copy of the current value.
  ///
  /// \returns A copy of the stored value.
  value_type load() const { return std::as_const(ref()); }
  /// Implicitly converts to a copy of the current value.
  ///
  /// \returns A copy of the stored value.
  /* implicit */ operator value_type() const { return load(); }

 private:
  void invalidate_caches() {
    truth_.version = truth_.version + 1; // intentionally not +=
  }

  // TODO: past C++17, just use if-constexpr in mutate()
  template <
      typename F,
      typename R = invoke_result_t<F, value_type&>,
      if_<std::is_void<R>{}> = 0>
  R mutate_locked(F f) {
    f(truth_.value); // value first: mutation may throw
    invalidate_caches();
  }
  template <
      typename F,
      typename R = invoke_result_t<F, value_type&>,
      if_<!std::is_void<R>{}> = 0>
  R mutate_locked(F f) {
    decltype(auto) ret = f(truth_.value); // value first: mutation may throw
    invalidate_caches();
    return ret;
  }
  template <typename F, typename R = invoke_result_t<F, value_type&>>
  R mutate(F f) {
    unique_lock<Mutex> lock{truth_.mutex};
    return mutate_locked(f);
  }

  template <typename A>
  bool mutate_cx(value_type& expected, A&& desired) {
    unique_lock<Mutex> lock{truth_.mutex};
    auto const eq = std::as_const(truth_.value) == std::as_const(expected);
    if (eq) {
      truth_.value = // value first: mutation may throw
          static_cast<A&&>(desired);
      invalidate_caches();
    } else {
      expected = std::as_const(truth_.value);
    }
    return eq;
  }

  FOLLY_ERASE value_type& ref() const {
    auto const cache = cache_.get();
    auto const unexpired = cache && cache->version == truth_.version;
    return FOLLY_LIKELY(unexpired) ? cache->value : get_slow();
  }

  FOLLY_NOINLINE value_type& get_slow() const {
    hybrid_lock<Mutex> lock{truth_.mutex};
    auto cache = cache_.get();
    if (cache == nullptr) {
      cache = new cache_state{truth_.value}; // value first: copy may throw
      cache_.reset(cache);
    } else {
      cache->value = truth_.value; // value first: copy may throw
    }
    cache->version = truth_.version;
    return cache->value;
  }

  mutable truth_state truth_;
  mutable tlp_cache_state cache_;
};

} // namespace folly
