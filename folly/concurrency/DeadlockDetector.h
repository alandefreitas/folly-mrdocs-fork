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

#include <folly/Executor.h>
#include <folly/executors/QueueObserver.h>

namespace folly {
/// Interface for an object that watches an executor for deadlocks.
class DeadlockDetector {
 public:
  /// Destroys the deadlock detector and stops watching.
  virtual ~DeadlockDetector() {}
};

/// Factory that creates DeadlockDetector instances for executors.
class DeadlockDetectorFactory {
 public:
  /// Destroys the factory.
  virtual ~DeadlockDetectorFactory() {}

  /// Creates a deadlock detector for the given executor.
  ///
  /// \param executor The executor to watch for deadlocks.
  /// \param name A human-readable name used to identify the executor.
  /// \returns An owning pointer to the new deadlock detector.
  virtual std::unique_ptr<DeadlockDetector> create(
      Executor* executor, const std::string& name) = 0;

  /// Returns the process-wide factory instance.
  ///
  /// \returns A pointer to the singleton factory, or null if none is registered.
  static DeadlockDetectorFactory* instance();
};

/// Function type that returns the deadlock detector factory instance.
using GetDeadlockDetectorFactoryInstance = DeadlockDetectorFactory*();
#if FOLLY_HAVE_WEAK_SYMBOLS
/// Weak hook resolving to the deadlock detector factory instance getter.
///
/// \returns The registered factory, or null when no implementation is linked.
FOLLY_ATTR_WEAK GetDeadlockDetectorFactoryInstance
    get_deadlock_detector_factory_instance;
#else
/// Weak hook resolving to the deadlock detector factory instance getter.
constexpr GetDeadlockDetectorFactoryInstance*
    get_deadlock_detector_factory_instance = nullptr;
#endif
} // namespace folly
