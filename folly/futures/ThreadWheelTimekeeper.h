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

#include <thread>

#include <folly/futures/Future.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/HHWheelTimer.h>

namespace folly {

/// A Timekeeper that schedules timeouts on an externally supplied EventBase.
class EventBaseThreadTimekeeper : public Timekeeper {
 public:
  /// Deleted default constructor; an EventBase must be supplied.
  EventBaseThreadTimekeeper() = delete;
  /// Constructs a timekeeper that schedules timeouts on the given EventBase.
  ///
  /// @param eventBase The EventBase used to schedule timeouts.
  explicit EventBaseThreadTimekeeper(folly::EventBase& eventBase)
      : eventBaseRef_(eventBase) {}
  /// Destroys the timekeeper.
  ~EventBaseThreadTimekeeper() override = default;

  /// Implement the Timekeeper interface
  ///
  /// @param duration The delay after which the returned future completes.
  /// @returns A SemiFuture that completes once the duration elapses.
  SemiFuture<Unit> after(HighResDuration duration) override;

 protected:
  folly::EventBase& eventBaseRef_; ///< The EventBase used to schedule timeouts.
};

/// The default Timekeeper implementation which uses a HHWheelTimer on an
/// EventBase in a dedicated thread. Users needn't deal with this directly, it
/// is used by default by Future methods that work with timeouts.
class ThreadWheelTimekeeper : public EventBaseThreadTimekeeper {
 public:
  /// But it doesn't *have* to be a singleton.
  ThreadWheelTimekeeper();
  /// Destroys the timekeeper and stops its EventBase thread.
  ~ThreadWheelTimekeeper() override;

 protected:
  /// The EventBase driving the wheel timer.
  folly::EventBase eventBase_{folly::EventBase::Options().setTimerTickInterval(
      std::chrono::milliseconds(1))};
  std::thread thread_; ///< The dedicated thread running the EventBase loop.
};

} // namespace folly
