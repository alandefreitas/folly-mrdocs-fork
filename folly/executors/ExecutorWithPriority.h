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

#include <folly/Executor.h>

namespace folly {
/// Wraps an executor so that submitted tasks run at a chosen priority.
class ExecutorWithPriority {
 public:
  /// Wraps an executor with a priority computed per task by a callback.
  ///
  /// \tparam Callback Type of the callback that computes each task's priority.
  /// \param executor Keep-alive token for the backing executor.
  /// \param callback Callable that returns the priority for each task.
  /// \returns A keep-alive token for the priority-applying executor.
  template <typename Callback>
  static Executor::KeepAlive<> createDynamic(
      Executor::KeepAlive<Executor> executor, Callback&& callback);

  /// Wraps an executor so that all tasks run at a fixed priority.
  ///
  /// \param executor Keep-alive token for the backing executor.
  /// \param priority The priority applied to every submitted task.
  /// \returns A keep-alive token for the priority-applying executor.
  static Executor::KeepAlive<> create(
      Executor::KeepAlive<Executor> executor, int8_t priority);
};
} // namespace folly

#include <folly/executors/ExecutorWithPriority-inl.h>
