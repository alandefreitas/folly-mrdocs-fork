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

#include <utility>

#include <glog/logging.h>

#include <folly/fibers/Baton.h>
#include <folly/fibers/async/Async.h>

namespace folly {
namespace fibers {
namespace async {

/// Async tagged wrapper that blocks on a baton until it is posted.
/// @param baton The baton to wait on.
/// @param args Arguments forwarded to `Baton::wait`.
/// @returns An empty Async once the baton has been posted.
template <typename... Args>
Async<void> baton_wait(Baton& baton, Args&&... args) {
  // Call into blocking API
  baton.wait(std::forward<Args>(args)...);
  return {};
}

/// Async tagged wrapper that waits on a baton for a bounded duration.
/// @param baton The baton to wait on.
/// @param args Arguments forwarded to `Baton::try_wait_for` (e.g. a timeout).
/// @returns True if the baton was posted within the timeout, false otherwise.
template <typename... Args>
Async<bool> baton_try_wait_for(Baton& baton, Args&&... args) {
  // Call into blocking API
  return baton.try_wait_for(std::forward<Args>(args)...);
}

/// Async tagged wrapper that waits on a baton until a deadline.
/// @param baton The baton to wait on.
/// @param args Arguments forwarded to `Baton::try_wait_until` (e.g. a deadline).
/// @returns True if the baton was posted before the deadline, false otherwise.
template <typename... Args>
Async<bool> baton_try_wait_until(Baton& baton, Args&&... args) {
  // Call into blocking API
  return baton.try_wait_until(std::forward<Args>(args)...);
}

} // namespace async
} // namespace fibers
} // namespace folly
