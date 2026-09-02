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
#include <string>
#include <thread>

#include <folly/Conv.h>
#include <folly/Range.h>
#include <folly/executors/thread_factory/ThreadFactory.h>
#include <folly/system/ThreadName.h>

namespace folly {

/// A ThreadFactory that names each thread it creates.
///
/// Threads are named by joining a shared prefix with a monotonically
/// increasing suffix, so each thread gets a distinct, recognizable name.
class NamedThreadFactory : public ThreadFactory {
 public:
  /// Construct a factory that names threads using the given prefix.
  ///
  /// \param prefix The name prefix shared by every thread this factory creates.
  explicit NamedThreadFactory(folly::StringPiece prefix)
      : prefix_(prefix.str()), suffix_(0) {}

  /// Create a new thread that runs the given function under a generated name.
  ///
  /// The thread name is the current prefix followed by an incrementing suffix.
  ///
  /// \param func The function to run on the new thread.
  /// \returns The newly created thread.
  std::thread newThread(Func&& func) override {
    auto name = folly::to<std::string>(prefix_, suffix_++);
    return std::thread(
        [func_2 = std::move(func), name_2 = std::move(name)]() mutable {
          folly::setThreadName(name_2);
          func_2();
        });
  }

  /// Change the prefix used to name threads created after this call.
  ///
  /// \param prefix The new name prefix.
  void setNamePrefix(folly::StringPiece prefix) { prefix_ = prefix.str(); }

  /// Return the current thread name prefix.
  ///
  /// \returns The prefix used to name threads.
  const std::string& getNamePrefix() const override { return prefix_; }

 protected:
  /// The name prefix shared by every thread this factory creates.
  std::string prefix_;
  /// The next numeric suffix appended to a thread name.
  std::atomic<uint64_t> suffix_;
};

} // namespace folly
