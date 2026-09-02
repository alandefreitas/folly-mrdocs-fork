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
#include <cstdint>

#include <folly/Function.h>
#include <folly/Optional.h>

/// Root namespace for the Folly library.
namespace folly {

/// Asynchronously waits for a timeout to occur.
class AsyncTimeout;

/**
 * Base interface to be implemented by all classes expecting to manage
 * timeouts. AsyncTimeout will use implementations of this interface
 * to schedule/cancel timeouts.
 */
class TimeoutManager {
 public:
  /// Millisecond-resolution timeout duration.
  using timeout_type = std::chrono::milliseconds;
  /// Microsecond-resolution timeout duration.
  using timeout_type_high_res = std::chrono::microseconds;

  /// Callback type invoked when a timeout fires.
  using Func = folly::Function<void()>;

  /// Distinguishes internal timeouts from normal ones.
  enum class InternalEnum {
    INTERNAL, ///< The timeout does not keep the event loop running.
    NORMAL ///< The timeout keeps the event loop running until it fires.
  };

  /// Constructs the timeout manager.
  TimeoutManager();

  /// Destroys the timeout manager.
  virtual ~TimeoutManager();

  /**
   * Attaches TimeoutManager to AsyncTimeout
   *
   * \param obj The timeout to attach.
   * \param internal Whether the timeout is treated as an internal event.
   */
  virtual void attachTimeoutManager(
      AsyncTimeout* obj, InternalEnum internal) = 0;

  /// Detaches TimeoutManager from AsyncTimeout.
  ///
  /// \param obj The timeout to detach.
  virtual void detachTimeoutManager(AsyncTimeout* obj) = 0;

  /**
   * Schedules AsyncTimeout to fire after `timeout` milliseconds
   *
   * \param obj The timeout to schedule.
   * \param timeout The delay before the timeout fires.
   * \returns `true` if the timeout was scheduled successfully.
   */
  virtual bool scheduleTimeout(AsyncTimeout* obj, timeout_type timeout) = 0;

  /**
   * Schedules AsyncTimeout to fire after `timeout` microseconds
   *
   * \param obj The timeout to schedule.
   * \param timeout The high-resolution delay before the timeout fires.
   * \returns `true` if the timeout was scheduled successfully.
   */
  virtual bool scheduleTimeoutHighRes(
      AsyncTimeout* obj, timeout_type_high_res timeout);

  /**
   * Cancels the AsyncTimeout, if scheduled
   *
   * \param obj The timeout to cancel.
   */
  virtual void cancelTimeout(AsyncTimeout* obj) = 0;

  /**
   * This is used to mark the beginning of a new loop cycle by the
   * first handler fired within that cycle.
   */
  virtual void bumpHandlingTime() = 0;

  /**
   * Helper method to know whether we are running in the timeout manager
   * thread
   *
   * \returns `true` if the caller is running in the timeout manager thread.
   */
  virtual bool isInTimeoutManagerThread() = 0;

  /**
   * Runs the given Cob at some time after the specified number of
   * milliseconds.  (No guarantees exactly when.)
   *
   * Throws a `std::system_error` if an error occurs.
   *
   * \param cob The callback to run after the delay.
   * \param milliseconds The delay before the callback runs, in milliseconds.
   * \param internal Whether the timeout is treated as an internal event.
   */
  void runAfterDelay(
      Func cob,
      uint32_t milliseconds,
      InternalEnum internal = InternalEnum::NORMAL);

  /**
   * Attempts to run the given Cob after the specified delay.
   *
   * \see runAfterDelay for more details
   *
   * \param cob The callback to run after the delay.
   * \param milliseconds The delay before the callback runs, in milliseconds.
   * \param internal Whether the timeout is treated as an internal event.
   * \returns true iff the cob was successfully registered.
   */
  bool tryRunAfterDelay(
      Func cob,
      uint32_t milliseconds,
      InternalEnum internal = InternalEnum::NORMAL);

 protected:
  /// Cancels and clears all callback timeouts registered via runAfterDelay.
  void clearCobTimeouts();

 private:
  struct CobTimeouts;
  std::unique_ptr<CobTimeouts> cobTimeouts_;
};

} // namespace folly
