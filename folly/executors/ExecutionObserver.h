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

#include <cstdint>

#include <boost/intrusive/list.hpp>

/// Facebook Folly library.
namespace folly {

/**
 * Observes the execution of a task. Multiple execution observers can be chained
 * together. As a caveat, execution observers should not remove themselves from
 * the list of observers during execution
 */
class ExecutionObserver
    : public boost::intrusive::list_base_hook<
          boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {
 public:
  /// The type of callback whose execution is being observed.
  enum class CallbackType {
    Event, ///< An event callback owned by EventBase.
    Loop, ///< A loop callback owned by EventBase.
    NotificationQueue, ///< A notification queue callback owned by EventBase.
    Fiber, ///< A fiber callback owned by FiberManager.
  };
  /// An intrusive list of execution observers.
  ///
  /// Constant time size is false to support auto_unlink behavior; the options
  /// are mutually exclusive.
  using List = boost::intrusive::
      list<ExecutionObserver, boost::intrusive::constant_time_size<false>>;

  /// Destroys the execution observer.
  virtual ~ExecutionObserver() = default;

  /**
   * Called when a task is about to start executing.
   *
   * @param id Unique id for the task which is starting.
   * @param callbackType The type of callback which is starting.
   */
  virtual void starting(uintptr_t id, CallbackType callbackType) noexcept = 0;

  /**
   * Called just after a task stops executing.
   *
   * @param id Unique id for the task which stopped.
   * @param callbackType The type of callback which stopped.
   */
  virtual void stopped(uintptr_t id, CallbackType callbackType) noexcept = 0;
};

/// Notifies a list of execution observers around a scoped task execution.
class ExecutionObserverScopeGuard {
 public:
  /// Notifies the observers that the task is starting to execute.
  ///
  /// \param observerList The list of observers to notify.
  /// \param id Unique id for the task being executed.
  /// \param callbackType The type of callback being executed.
  ExecutionObserverScopeGuard(
      folly::ExecutionObserver::List* observerList,
      void* id,
      folly::ExecutionObserver::CallbackType callbackType);

  /// Notifies the observers that the task stopped executing.
  ~ExecutionObserverScopeGuard();

 private:
  folly::ExecutionObserver::List* observerList_;
  uintptr_t id_;
  folly::ExecutionObserver::CallbackType callbackType_;
};

} // namespace folly
