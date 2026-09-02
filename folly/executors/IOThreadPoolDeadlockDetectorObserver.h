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

#include <folly/Singleton.h>
#include <folly/concurrency/DeadlockDetector.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/executors/ThreadPoolExecutor.h>

namespace folly {

/// Observer that attaches a deadlock detector to each event base of an
/// IO thread pool.
class IOThreadPoolDeadlockDetectorObserver
    : public folly::IOThreadPoolExecutorBase::IOObserver {
 public:
  /// Constructs the observer.
  ///
  /// \param deadlockDetectorFactory Factory used to create a deadlock
  /// detector for each registered event base.
  /// \param name Name used to identify the observed thread pool.
  IOThreadPoolDeadlockDetectorObserver(
      folly::DeadlockDetectorFactory* deadlockDetectorFactory,
      const std::string& name);

  /// Creates and starts a deadlock detector for the given event base.
  ///
  /// \param evb The event base to start monitoring.
  void registerEventBase(EventBase& evb) noexcept override;

  /// Stops and removes the deadlock detector for the given event base.
  ///
  /// \param evb The event base to stop monitoring.
  void unregisterEventBase(EventBase& evb) noexcept override;

  /// Creates an observer using the default deadlock detector factory.
  ///
  /// \param name Name used to identify the observed thread pool.
  /// \returns A new observer instance.
  static std::unique_ptr<IOThreadPoolDeadlockDetectorObserver> create(
      const std::string& name);

 private:
  const std::string name_;
  folly::DeadlockDetectorFactory* deadlockDetectorFactory_;
  folly::Synchronized<std::unordered_map<
      folly::EventBase*,
      std::unique_ptr<folly::DeadlockDetector>>>
      detectors_;
};

} // namespace folly
