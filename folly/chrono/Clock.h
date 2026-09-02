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

/**
 * Namespace for folly chrono types.
 *
 * Using a separate namespace for clock types to minimize type conflicts in
 * tests and other code which may be using `using namespace folly` while also
 * having aliased chrono types.
 */
namespace folly::chrono {

/**
 * Clock interface.
 *
 * Abstraction enables tests to control the current time.
 */
template <typename ClockType>
class Clock {
 public:
  /// Time point type of the underlying clock.
  using TimePoint = typename ClockType::time_point;
  /// Constructs the clock.
  Clock() = default;
  /// Destroys the clock.
  virtual ~Clock() = default;

  /**
   * Returns current time.
   *
   * \returns The current time point.
   */
  [[nodiscard]] virtual TimePoint now() const = 0;
};

/**
 * Implementation of ClockInterface for given std::chrono ClockType.
 */
template <typename ClockType>
class ClockImpl : public Clock<ClockType> {
 public:
  /// Time point type of the underlying clock.
  using TimePoint = typename ClockType::time_point;
  /// Constructs the clock implementation.
  ClockImpl() = default;
  /// Destroys the clock implementation.
  ~ClockImpl() override = default;
  /// Returns the current time from the underlying clock.
  ///
  /// \returns The current time point.
  [[nodiscard]] TimePoint now() const override { return ClockType::now(); }
};

/// Clock interface backed by std::chrono::steady_clock.
using SteadyClock = Clock<std::chrono::steady_clock>;
/// Clock implementation backed by std::chrono::steady_clock.
using SteadyClockImpl = ClockImpl<std::chrono::steady_clock>;
/// Clock interface backed by std::chrono::system_clock.
using SystemClock = Clock<std::chrono::system_clock>;
/// Clock implementation backed by std::chrono::system_clock.
using SystemClockImpl = ClockImpl<std::chrono::system_clock>;

} // namespace folly::chrono
