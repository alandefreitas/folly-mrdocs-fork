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

#include <glog/logging.h>

#include <folly/executors/ScheduledExecutor.h>
#include <folly/futures/Future.h>

namespace folly {

/// Exception thrown when no timekeeper is available for scheduling.
struct FOLLY_EXPORT TimekeeperScheduledExecutorNoTimekeeper
    : public std::logic_error {
  /// Constructs the exception with a fixed message.
  TimekeeperScheduledExecutorNoTimekeeper()
      : std::logic_error("No Timekeeper available") {}
};

/// This class turns a Executor into a ScheduledExecutor.
class TimekeeperScheduledExecutor : public ScheduledExecutor {
 public:
  /// Copy constructor (deleted).
  ///
  /// \param other The executor to copy from.
  TimekeeperScheduledExecutor(TimekeeperScheduledExecutor const& other) = delete;

  /// Copy assignment operator (deleted).
  ///
  /// \param other The executor to assign from.
  TimekeeperScheduledExecutor& operator=(
      TimekeeperScheduledExecutor const& other) = delete;

  /// Move constructor (deleted).
  ///
  /// \param other The executor to move from.
  TimekeeperScheduledExecutor(TimekeeperScheduledExecutor&& other) = delete;

  /// Move assignment operator (deleted).
  ///
  /// \param other The executor to move from.
  TimekeeperScheduledExecutor& operator=(
      TimekeeperScheduledExecutor&& other) = delete;

  /// Creates a TimekeeperScheduledExecutor wrapping the given parent executor.
  ///
  /// \param parent The executor that runs the scheduled work.
  /// \param getTimekeeper Factory returning the timekeeper used for scheduling.
  /// \returns A keep-alive handle to the new executor.
  static Executor::KeepAlive<TimekeeperScheduledExecutor> create(
      Executor::KeepAlive<> parent,
      Function<std::shared_ptr<Timekeeper>()> getTimekeeper =
          detail::getTimekeeperSingleton);

  /// Schedules a function to be executed as soon as possible.
  ///
  /// \param func The function to execute.
  virtual void add(Func func) override;

  /// Schedules a function to be executed at the given time point.
  ///
  /// \param func The function to execute.
  /// \param t The time point at which to execute the function.
  virtual void scheduleAt(
      Func&& func, ScheduledExecutor::TimePoint const& t) override;

 protected:
  /// Acquires a keep-alive reference to this executor.
  ///
  /// \returns True if the reference was acquired.
  bool keepAliveAcquire() noexcept override;

  /// Releases a keep-alive reference to this executor.
  void keepAliveRelease() noexcept override;

 private:
  TimekeeperScheduledExecutor(
      KeepAlive<Executor>&& parent,
      Function<std::shared_ptr<Timekeeper>()> getTimekeeper)
      : parent_(std::move(parent)), getTimekeeper_(std::move(getTimekeeper)) {}

  ~TimekeeperScheduledExecutor() override { DCHECK(!keepAliveCounter_); }

  void run(Func);

  KeepAlive<Executor> parent_;
  Function<std::shared_ptr<Timekeeper>()> getTimekeeper_;
  std::atomic<ssize_t> keepAliveCounter_{1};
};

} // namespace folly
