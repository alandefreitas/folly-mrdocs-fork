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

#include <string>
#include <thread>

#include <folly/Executor.h>

namespace folly {

/// Interface for creating threads used by executors.
class ThreadFactory {
 public:
  /// Constructs the factory.
  ThreadFactory() = default;

  /// Destroys the factory.
  virtual ~ThreadFactory() = default;

  /// Creates a new thread that runs the given function.
  ///
  /// \param func The function to run on the new thread.
  /// \returns The newly created thread.
  virtual std::thread newThread(Func&& func) = 0;

  /// Returns the name prefix applied to threads created by this factory.
  ///
  /// \returns The thread name prefix.
  virtual const std::string& getNamePrefix() const = 0;

 private:
  ThreadFactory(const ThreadFactory&) = delete;
  ThreadFactory& operator=(const ThreadFactory&) = delete;
  ThreadFactory(ThreadFactory&&) = delete;
  ThreadFactory& operator=(ThreadFactory&&) = delete;
};

} // namespace folly
