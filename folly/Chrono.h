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

#include <chrono>
#include <ctime>
#include <stdexcept>
#include <type_traits>

#include <folly/Portability.h>
#include <folly/lang/Exception.h>
#include <folly/portability/Time.h>

namespace folly {
/// Clock utilities and coarse-resolution clocks.
namespace chrono {

/// Re-export of `std::chrono::abs`.
/* using override */ using std::chrono::abs;
/// Re-export of `std::chrono::ceil`.
/* using override */ using std::chrono::ceil;
/// Re-export of `std::chrono::floor`.
/* using override */ using std::chrono::floor;
/// Re-export of `std::chrono::round`.
/* using override */ using std::chrono::round;

/// Spec tag shared by all steady clocks with the same epoch and tick rate.
struct steady_clock_spec {};

/// Spec tag shared by all system clocks with the same epoch and tick rate.
struct system_clock_spec {};

/// Detects and re-exports per-clock traits.
///
/// Specializeable for clocks for which trait detection fails.
template <typename Clock>
struct clock_traits {
 private:
  template <typename C>
  using detect_spec_ = typename C::folly_spec;

 public:
  /// The spec tag associated with the clock.
  using spec = detected_or_t<void, detect_spec_, Clock>;
};

template <>
struct clock_traits<std::chrono::steady_clock> {
  using spec = steady_clock_spec;
};
template <>
struct clock_traits<std::chrono::system_clock> {
  using spec = system_clock_spec;
};

/// A coarse-resolution steady clock backed by CLOCK_MONOTONIC_COARSE.
struct coarse_steady_clock {
  /// The spec tag for this clock.
  using folly_spec = steady_clock_spec;

  /// The duration type of the clock.
  using duration = std::chrono::steady_clock::duration;
  /// The representation type of the clock's duration.
  using rep = duration::rep;
  /// The tick period of the clock.
  using period = duration::period;
  /// The time point type of the clock.
  using time_point = std::chrono::time_point<coarse_steady_clock>;
  /// Whether the clock is steady (never adjusted backwards).
  constexpr static bool is_steady = true;

  /// Return the current time point of the clock.
  ///
  /// \returns The current coarse steady-clock time.
  static time_point now() noexcept {
#ifndef CLOCK_MONOTONIC_COARSE
    auto time = std::chrono::steady_clock::now().time_since_epoch();
#else
    timespec ts;
    int ret = clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
    if (kIsDebug && (ret != 0)) {
      throw_exception<std::runtime_error>(
          "Error using CLOCK_MONOTONIC_COARSE.");
    }
    auto time =
        std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);
#endif
    return time_point(std::chrono::duration_cast<duration>(time));
  }
};

/// A coarse-resolution system clock backed by CLOCK_REALTIME_COARSE.
struct coarse_system_clock {
  /// The spec tag for this clock.
  using folly_spec = system_clock_spec;

  /// The duration type of the clock.
  using duration = std::chrono::system_clock::duration;
  /// The representation type of the clock's duration.
  using rep = duration::rep;
  /// The tick period of the clock.
  using period = duration::period;
  /// The time point type of the clock.
  using time_point = std::chrono::time_point<coarse_system_clock>;
  /// Whether the clock is steady (never adjusted backwards).
  constexpr static bool is_steady = false;

  /// Return the current time point of the clock.
  ///
  /// \returns The current coarse system-clock time.
  static time_point now() noexcept {
#ifndef CLOCK_REALTIME_COARSE
    auto time = std::chrono::system_clock::now().time_since_epoch();
#else
    timespec ts;
    int ret = clock_gettime(CLOCK_REALTIME_COARSE, &ts);
    if (kIsDebug && (ret != 0)) {
      throw_exception<std::runtime_error>("Error using CLOCK_REALTIME_COARSE.");
    }
    auto time =
        std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);
#endif
    return time_point(std::chrono::duration_cast<duration>(time));
  }

  /// Convert a clock time point to a `std::time_t`.
  ///
  /// \param t The time point to convert.
  /// \returns The equivalent `std::time_t` value in seconds.
  static std::time_t to_time_t(const time_point& t) noexcept {
    auto d = t.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(d).count();
  }

  /// Convert a `std::time_t` to a clock time point.
  ///
  /// \param t The `std::time_t` value to convert.
  /// \returns The equivalent clock time point.
  static time_point from_time_t(std::time_t t) noexcept {
    return time_point(
        std::chrono::duration_cast<duration>(std::chrono::seconds(t)));
  }
};

} // namespace chrono
} // namespace folly
