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

#include <folly/DefaultKeepAliveExecutor.h>

namespace folly {

/**
 * VirtualExecutor implements a light-weight view onto existing Executor.
 *
 * Multiple VirtualExecutors can be backed by a single Executor.
 *
 * VirtualExecutor's destructor blocks until all tasks scheduled through it are
 * complete. Executor's destructor also blocks until all VirtualExecutors
 * backed by it are released.
 */
class VirtualExecutor : public DefaultKeepAliveExecutor {
  auto wrapFunc(Func f) {
    class FuncAndKeepAlive {
     public:
      FuncAndKeepAlive(Func&& f, VirtualExecutor* executor)
          : keepAlive_(getKeepAliveToken(executor)), f_(std::move(f)) {}

      void operator()() { f_(); }

     private:
      Executor::KeepAlive<VirtualExecutor> keepAlive_;
      Func f_;
    };

    return FuncAndKeepAlive(std::move(f), this);
  }

 public:
  /// Constructs a view onto the executor referenced by a keep-alive token.
  ///
  /// \param executor Keep-alive token for the backing executor.
  explicit VirtualExecutor(KeepAlive<> executor)
      : executor_(std::move(executor)) {
    assert(!isKeepAliveDummy(executor_));
  }

  /// Constructs a view onto the given executor.
  ///
  /// \param executor The backing executor.
  explicit VirtualExecutor(Executor* executor)
      : VirtualExecutor(getKeepAliveToken(executor)) {}

  /// Constructs a view onto the given executor.
  ///
  /// \param executor The backing executor.
  explicit VirtualExecutor(Executor& executor)
      : VirtualExecutor(getKeepAliveToken(executor)) {}

  /// Copy constructor (deleted).
  ///
  /// \param other The executor to copy from.
  VirtualExecutor(const VirtualExecutor& other) = delete;

  /// Copy assignment operator (deleted).
  ///
  /// \param other The executor to assign from.
  /// \returns A reference to this executor.
  VirtualExecutor& operator=(const VirtualExecutor& other) = delete;

  /// Returns the number of priority levels supported by the backing executor.
  ///
  /// \returns The number of priority levels.
  uint8_t getNumPriorities() const override {
    return executor_->getNumPriorities();
  }

  /// Schedules a function to run on the backing executor.
  ///
  /// \param f The function to execute.
  void add(Func f) override { executor_->add(wrapFunc(std::move(f))); }

  /// Schedules a function to run on the backing executor at the given priority.
  ///
  /// \param f The function to execute.
  /// \param priority The priority level for the task.
  void addWithPriority(Func f, int8_t priority) override {
    executor_->addWithPriority(wrapFunc(std::move(f)), priority);
  }

  /// Destroys the executor, blocking until all scheduled tasks complete.
  ~VirtualExecutor() override { joinKeepAlive(); }

 private:
  const KeepAlive<> executor_;
};

} // namespace folly
