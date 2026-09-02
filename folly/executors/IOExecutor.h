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

namespace folly {
/// An event loop that drives asynchronous I/O and timers.
class EventBase;
} // namespace folly

namespace folly {

/// An executor that operates on at least one EventBase.
///
/// One of these EventBases should be accessible via getEventBase(). The event
/// base returned by a call to getEventBase() is implementation dependent.
///
/// Note that IOExecutors don't necessarily loop on the base themselves, for
/// instance, EventBase itself is an IOExecutor but doesn't drive itself.
///
/// Implementations of IOExecutor are eligible to become the global IO executor,
/// returned on every call to getIOExecutor(), via setIOExecutor().
/// These functions are declared in GlobalExecutor.h
///
/// If getIOExecutor is called and none has been set, a default global
/// IOThreadPoolExecutor will be created and returned.
class IOExecutor : public virtual folly::Executor {
 public:
  /// Destroys the IO executor.
  ~IOExecutor() override = default;

  /// Returns an EventBase this executor operates on.
  ///
  /// \returns An event base owned by this executor.
  virtual folly::EventBase* getEventBase() = 0;
};

} // namespace folly
