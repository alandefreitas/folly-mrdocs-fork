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

#include <future>

#include <folly/Executor.h>
#include <folly/Function.h>
#include <folly/Synchronized.h>
#include <folly/executors/SequencedExecutor.h>
#include <folly/io/async/EventBase.h>
#include <folly/synchronization/Baton.h>

/// Facebook Folly library namespace.
namespace folly {

/**
 * VirtualEventBase implements a light-weight view onto existing EventBase.
 *
 * Multiple VirtualEventBases can be backed by a single EventBase. Similarly
 * to EventBase, VirtualEventBase implements KeepAlive functionality,
 * which allows callbacks holding KeepAlive token to keep EventBase looping
 * until they are complete.
 *
 * VirtualEventBase destructor blocks until all its KeepAliveTokens are released
 * and all tasks scheduled through it are complete. EventBase destructor also
 * blocks until all VirtualEventBases backed by it are released.
 */
class VirtualEventBase
    : public folly::TimeoutManager,
      public folly::SequencedExecutor {
 public:
  /// Constructs a view backed by the given EventBase.
  ///
  /// \param evb The EventBase that backs this VirtualEventBase.
  explicit VirtualEventBase(EventBase& evb);

  /// Deleted copy constructor.
  VirtualEventBase(const VirtualEventBase& other) = delete;
  /// Deleted copy assignment.
  VirtualEventBase& operator=(const VirtualEventBase& other) = delete;

  /// Blocks until all keep-alive tokens are released and tasks complete.
  ~VirtualEventBase() override;

  /// Returns the backing EventBase.
  ///
  /// \returns The EventBase that backs this VirtualEventBase.
  EventBase& getEventBase() { return *evb_; }

  /**
   * Adds the given callback to a queue of things run before destruction
   * of current VirtualEventBase.
   *
   * This allows users of VirtualEventBase that run in it, but don't control it,
   * to be notified before VirtualEventBase gets destructed.
   *
   * Note: this will be called from the loop of the EventBase, backing this
   * VirtualEventBase
   *
   * \param callback The callback to run before destruction.
   */
  void runOnDestruction(EventBase::OnDestructionCallback& callback);
  /// Adds a callback to run before destruction of this VirtualEventBase.
  ///
  /// \param f The function to run before destruction.
  void runOnDestruction(Func f);

  /**
   * VirtualEventBase destructor blocks until all tasks scheduled through its
   * runInEventBaseThread are complete.
   *
   * @see EventBase::runInEventBaseThread
   *
   * \param f The function to run in the EventBase thread.
   */
  template <typename F>
  void runInEventBaseThread(F&& f) noexcept {
    // KeepAlive token has to be released in the EventBase thread. If
    // runInEventBaseThread() fails, we can't extract the KeepAlive token
    // from the callback to properly release it.
    evb_->runInEventBaseThread(
        [keepAliveToken = getKeepAliveToken(this),
         f = std::forward<F>(f)]() mutable { f(); });
  }

  /// Returns the timer of the backing EventBase.
  ///
  /// \returns The HHWheelTimer of the backing EventBase.
  HHWheelTimer& timer() { return evb_->timer(); }

  /// Attaches a timeout to the backing EventBase.
  ///
  /// \param obj The timeout to attach.
  /// \param internal Whether the timeout is internal to the manager.
  void attachTimeoutManager(
      AsyncTimeout* obj, TimeoutManager::InternalEnum internal) override {
    evb_->attachTimeoutManager(obj, internal);
  }

  /// Detaches a timeout from the backing EventBase.
  ///
  /// \param obj The timeout to detach.
  void detachTimeoutManager(AsyncTimeout* obj) override {
    evb_->detachTimeoutManager(obj);
  }

  /// Schedules a timeout on the backing EventBase.
  ///
  /// \param obj The timeout to schedule.
  /// \param timeout The delay before the timeout fires.
  /// \returns True if the timeout was scheduled successfully.
  bool scheduleTimeout(
      AsyncTimeout* obj, TimeoutManager::timeout_type timeout) override {
    return evb_->scheduleTimeout(obj, timeout);
  }

  /// Cancels a scheduled timeout on the backing EventBase.
  ///
  /// \param obj The timeout to cancel.
  void cancelTimeout(AsyncTimeout* obj) override { evb_->cancelTimeout(obj); }

  /// Bumps the handling time of the backing EventBase.
  void bumpHandlingTime() override { evb_->bumpHandlingTime(); }

  /// Returns whether the caller runs in the backing EventBase thread.
  ///
  /// \returns True if the caller runs in the backing EventBase thread.
  bool isInTimeoutManagerThread() override {
    return evb_->isInTimeoutManagerThread();
  }

  /// Runs the given function in the EventBase thread.
  ///
  /// @see runInEventBaseThread
  ///
  /// \param f The function to run in the EventBase thread.
  void add(folly::Func f) override { runInEventBaseThread(std::move(f)); }

  /// Returns whether the caller runs in the backing EventBase thread.
  ///
  /// \returns True if the caller runs in the running EventBase thread.
  bool inRunningEventBaseThread() const {
    return evb_->inRunningEventBaseThread();
  }

 protected:
  /// Acquires a keep-alive token on this VirtualEventBase.
  ///
  /// \returns True once the token is acquired.
  bool keepAliveAcquire() noexcept override {
    auto oldCount = keepAliveCount_.fetch_add(1, std::memory_order_relaxed);
    DCHECK_NE(oldCount, 0);
    return true;
  }

  /// Releases a keep-alive token on this VirtualEventBase.
  void keepAliveRelease() noexcept override {
    auto oldCount = keepAliveCount_.fetch_sub(1, std::memory_order_acq_rel);
    if (oldCount != 1) {
      DCHECK_NE(oldCount, 0);
      return;
    }
    if (!evb_->inRunningEventBaseThread()) {
      evb_->runInEventBaseThreadAlwaysEnqueue([this] { destroyImpl(); });
    } else {
      destroyImpl();
    }
  }

 private:
  friend class EventBase;

  size_t keepAliveCount() {
    return keepAliveCount_.load(std::memory_order_acquire);
  }

  std::future<void> destroy();
  void destroyImpl() noexcept;

  using LoopCallbackList = EventBase::LoopCallback::List;

  KeepAlive<EventBase> evb_;

  std::atomic<size_t> keepAliveCount_{1};
  std::promise<void> destroyPromise_;
  std::future<void> destroyFuture_{destroyPromise_.get_future()};
  KeepAlive<VirtualEventBase> loopKeepAlive_{
      makeKeepAlive<VirtualEventBase>(this)};

  Synchronized<EventBase::OnDestructionCallback::List> onDestructionCallbacks_;
};
} // namespace folly
