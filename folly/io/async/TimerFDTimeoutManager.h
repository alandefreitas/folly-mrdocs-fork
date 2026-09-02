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

#include <map>

#include <folly/io/async/DelayedDestruction.h>
#include <folly/io/async/TimerFD.h>

/// Facebook Folly library namespace.
namespace folly {
// generic TimerFD based timeout manager
/// Manages many timeouts on a single TimerFD.
class TimerFDTimeoutManager : public TimerFD {
 public:
  /// Owning pointer that respects DelayedDestruction semantics.
  using UniquePtr = DelayedDestructionUniquePtr<TimerFDTimeoutManager>;
  /// Shared owning pointer type.
  using SharedPtr = std::shared_ptr<TimerFDTimeoutManager>;

 public:
  /// A single timeout registered with a TimerFDTimeoutManager.
  class Callback
      : public boost::intrusive::list_base_hook<
            boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {
   public:
    /// Constructs a callback not yet bound to a manager.
    Callback() = default;
    /// Constructs a callback bound to the given manager.
    ///
    /// \param mgr The manager that owns this callback.
    explicit Callback(TimerFDTimeoutManager* mgr) : mgr_(mgr) {}
    /// Destroys the callback.
    virtual ~Callback() = default;

    /// Called when the timeout fires.
    virtual void timeoutExpired() noexcept = 0;
    /// Called when the timeout is canceled.
    virtual void callbackCanceled() noexcept { timeoutExpired(); }

    /// Returns the absolute time at which this callback expires.
    ///
    /// \returns The expiration time.
    const std::chrono::microseconds& getExpirationTime() const {
      return expirationTime_;
    }

    /// Binds the callback to a manager and sets its expiration time.
    ///
    /// \param mgr The manager that owns this callback.
    /// \param expirationTime The absolute time at which the callback expires.
    void setExpirationTime(
        TimerFDTimeoutManager* mgr,
        const std::chrono::microseconds& expirationTime) {
      mgr_ = mgr;
      expirationTime_ = expirationTime;
    }

    /// Returns the time left until this callback expires.
    ///
    /// \returns The remaining time, or zero if already expired.
    std::chrono::microseconds getTimeRemaining() const {
      return getTimeRemaining(std::chrono::steady_clock::now());
    }

    /// Returns the time left until this callback expires, relative to a time.
    ///
    /// \param now The reference time to measure from.
    /// \returns The remaining time, or zero if already expired.
    std::chrono::microseconds getTimeRemaining(
        std::chrono::steady_clock::time_point now) const {
      auto nowMs = std::chrono::duration_cast<std::chrono::microseconds>(
          now.time_since_epoch());
      if (expirationTime_ > nowMs) {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            expirationTime_ - nowMs);
      }

      return std::chrono::microseconds(0);
    }

    /// Schedules this callback to fire after the given delay.
    ///
    /// \param timeout The delay before the callback fires.
    void scheduleTimeout(std::chrono::microseconds timeout) {
      if (mgr_) {
        mgr_->scheduleTimeout(this, timeout);
      }
    }

    /// Cancels this callback's pending timeout.
    ///
    /// \returns `true` if a pending timeout was canceled.
    bool cancelTimeout() { return mgr_->cancelTimeout(this); }

   private:
    TimerFDTimeoutManager* mgr_{nullptr};
    std::chrono::microseconds expirationTime_{0};
  };

  /// Constructs a timeout manager driven by the given EventBase.
  ///
  /// \param eventBase The EventBase that runs the underlying timer.
  explicit TimerFDTimeoutManager(folly::EventBase* eventBase);
  /// Destroys the timeout manager.
  ~TimerFDTimeoutManager() override;

  // from TimerFD
  /// Called when the underlying timer fires; dispatches expired callbacks.
  void onTimeout() noexcept final;

  /// Cancels every pending timeout.
  ///
  /// \returns The number of timeouts that were canceled.
  size_t cancelAll();
  /// Schedules a callback to fire after the given delay.
  ///
  /// \param callback The callback to schedule.
  /// \param timeout The delay before the callback fires.
  void scheduleTimeout(Callback* callback, std::chrono::microseconds timeout);
  /// Cancels a pending callback.
  ///
  /// \param callback The callback to cancel.
  /// \returns `true` if a pending timeout was canceled.
  bool cancelTimeout(Callback* callback);

  /// Schedules a function to run after the given delay.
  ///
  /// \tparam F The callable type.
  /// \param fn The function to run when the timeout fires.
  /// \param timeout The delay before the function runs.
  template <class F>
  void scheduleTimeoutFn(F fn, std::chrono::microseconds timeout) {
    struct Wrapper : Callback {
      explicit Wrapper(F f) : fn_(std::move(f)) {}
      void timeoutExpired() noexcept override {
        try {
          fn_();
        } catch (std::exception const& e) {
          LOG(ERROR) << "HHWheelTimerBase timeout callback threw an exception: "
                     << e.what();
        } catch (...) {
          LOG(ERROR)
              << "HHWheelTimerBase timeout callback threw a non-exception.";
        }
        delete this;
      }
      F fn_;
    };
    Wrapper* w = new Wrapper(std::move(fn));
    scheduleTimeout(w, timeout);
  }

  /// Returns the number of pending timeouts.
  ///
  /// \returns The count of scheduled callbacks.
  size_t count() const;

 private:
  void processExpiredTimers();
  void scheduleNextTimer();

  std::chrono::steady_clock::time_point getCurTime() {
    return std::chrono::steady_clock::now();
  }

  // we can attempt to schedule new entries while in processExpiredTimers
  // we want to reschedule the timers once we're done with the processing
  bool processingExpired_{false};

  using CallbackList = boost::intrusive::
      list<Callback, boost::intrusive::constant_time_size<false>>;
  std::map<std::chrono::microseconds, CallbackList> callbacks_;
  CallbackList inProgressList_;
};
} // namespace folly
