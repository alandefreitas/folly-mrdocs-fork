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
#include <folly/io/async/TimeoutManager.h>
#include <folly/io/async/TimerFD.h>

/// Facebook Folly library namespace.
namespace folly {
// single timeout timerfd based TimeoutManager
/// A TimeoutManager backed by a TimerFD that tracks a single timeout.
class STTimerFDTimeoutManager : public TimeoutManager, TimerFD {
 public:
  /// Constructs a manager driven by the given EventBase.
  ///
  /// \param eventBase The EventBase that runs the underlying timer.
  explicit STTimerFDTimeoutManager(folly::EventBase* eventBase);
  /// Destroys the manager.
  ~STTimerFDTimeoutManager() override;
  /// Deleted copy constructor.
  ///
  /// \param other The manager to copy from.
  STTimerFDTimeoutManager(const STTimerFDTimeoutManager& other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param other The manager to copy from.
  /// \returns A reference to this manager.
  STTimerFDTimeoutManager& operator=(const STTimerFDTimeoutManager& other) =
      delete;
  /// Deleted move constructor.
  ///
  /// \param other The manager to move from.
  STTimerFDTimeoutManager(STTimerFDTimeoutManager&& other) = delete;
  /// Deleted move assignment.
  ///
  /// \param other The manager to move from.
  /// \returns A reference to this manager.
  STTimerFDTimeoutManager& operator=(STTimerFDTimeoutManager&& other) = delete;

  /**
   * Attaches TimeoutManager to AsyncTimeout
   *
   * \param obj The timeout to attach.
   * \param internal Whether the timeout is internal to the manager.
   */
  void attachTimeoutManager(AsyncTimeout* obj, InternalEnum internal) final;
  /// Detaches the TimeoutManager from an AsyncTimeout.
  ///
  /// \param obj The timeout to detach.
  void detachTimeoutManager(AsyncTimeout* obj) final;

  /**
   * Schedules AsyncTimeout to fire after `timeout` milliseconds
   *
   * \param obj The timeout to schedule.
   * \param timeout The delay in milliseconds before the timeout fires.
   * \returns `true` if the timeout was scheduled.
   */
  bool scheduleTimeout(AsyncTimeout* obj, timeout_type timeout) final;

  /**
   * Schedules AsyncTimeout to fire after `timeout` microseconds
   *
   * \param obj The timeout to schedule.
   * \param timeout The delay in microseconds before the timeout fires.
   * \returns `true` if the timeout was scheduled.
   */
  bool scheduleTimeoutHighRes(
      AsyncTimeout* obj, timeout_type_high_res timeout) final;

  /**
   * Cancels the AsyncTimeout, if scheduled
   *
   * \param obj The timeout to cancel.
   */
  void cancelTimeout(AsyncTimeout* obj) final;

  /**
   * This is used to mark the beginning of a new loop cycle by the
   * first handler fired within that cycle.
   */
  void bumpHandlingTime() final;

  /**
   * Helper method to know whether we are running in the timeout manager
   * thread
   *
   * \returns `true` if the caller runs in the timeout manager thread.
   */
  bool isInTimeoutManagerThread() final {
    return eventBase_->isInEventBaseThread();
  }

  // from TimerFD
  /// Called when the underlying timer fires.
  void onTimeout() noexcept final;

 private:
  static void setActive(AsyncTimeout* obj, bool active);

  folly::EventBase* eventBase_{nullptr};
  AsyncTimeout* obj_{nullptr};
};
} // namespace folly
