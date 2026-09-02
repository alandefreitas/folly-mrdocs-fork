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
#include <memory>

#include <folly/CancellationToken.h>
#include <folly/fibers/ExecutorBasedLoopController.h>
#include <folly/fibers/FiberManagerInternal.h>
#include <folly/io/async/VirtualEventBase.h>

namespace folly {
namespace fibers {

/// A LoopController that drives a FiberManager loop from a folly EventBase.
class EventBaseLoopController : public ExecutorBasedLoopController {
 public:
  /// Constructs a loop controller with no EventBase attached yet.
  explicit EventBaseLoopController();
  /// Destroys the loop controller.
  ~EventBaseLoopController() override;

  /**
   * Attach EventBase after LoopController was created.
   *
   * @param eventBase The EventBase to drive this loop controller.
   */
  void attachEventBase(EventBase& eventBase);
  /// Attach a VirtualEventBase after LoopController was created.
  ///
  /// @param eventBase The VirtualEventBase to drive this loop controller.
  void attachEventBase(VirtualEventBase& eventBase);

  /// Returns the EventBase currently attached to this loop controller.
  ///
  /// @returns The attached EventBase, or null if none is attached.
  VirtualEventBase* getEventBase() { return eventBase_; }

  /// Sets the runner used to execute the fiber loop function.
  ///
  /// @param loopRunner The runner to execute the loop function.
  void setLoopRunner(InlineFunctionRunner* loopRunner) {
    loopRunner_ = loopRunner;
  }

  /// Returns the executor backing this loop controller.
  ///
  /// @returns The EventBase used as the executor.
  folly::Executor* executor() const override { return eventBase_; }

  /// Reports whether the caller is running on the EventBase thread.
  ///
  /// @returns True if the current thread is the EventBase loop thread.
  bool isInLoopThread() override {
    return eventBase_->getEventBase().inRunningEventBaseThread();
  }

 private:
  class ControllerCallback : public folly::EventBase::LoopCallback {
   public:
    explicit ControllerCallback(EventBaseLoopController& controller)
        : controller_(controller) {}

    void runLoopCallback() noexcept override { controller_.runLoop(); }

   private:
    EventBaseLoopController& controller_;
  };

  folly::CancellationToken eventBaseShutdownToken_;

  VirtualEventBase* eventBase_{nullptr};
  Executor::KeepAlive<VirtualEventBase> eventBaseKeepAlive_;
  ControllerCallback callback_;
  FiberManager* fm_{nullptr};
  InlineFunctionRunner* loopRunner_{nullptr};
  std::atomic<bool> eventBaseAttached_{false};
  bool awaitingScheduling_{false};

  /* LoopController interface */

  void setFiberManager(FiberManager* fm) override;
  void schedule() override;
  void runLoop() override;
  void runEagerFiber(Fiber*) override;
  void scheduleThreadSafe() override;
  HHWheelTimer* timer() override;

  friend class FiberManager;
};
} // namespace fibers
} // namespace folly

#include <folly/fibers/EventBaseLoopController-inl.h>
