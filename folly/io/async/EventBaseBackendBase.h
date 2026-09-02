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

#include <memory>

#include <folly/io/IOBuf.h>
#include <folly/io/async/EventUtil.h>
#include <folly/net/NetOps.h>
#include <folly/portability/Event.h>
#include <folly/portability/IOVec.h>

/// Facebook Folly library namespace.
namespace folly {

/// Event loop and I/O multiplexer bound to a single thread.
class EventBase;
/// Abstract base for the backend that drives an EventBase loop.
class EventBaseBackendBase;

/// Wrapper around a libevent event owned by an EventBase backend.
class EventBaseEvent {
 public:
  /// Constructs an unregistered event.
  EventBaseEvent() = default;
  /// Frees any user data still owned by the event.
  ~EventBaseEvent() {
    if (userData_ && freeFn_) {
      freeFn_(userData_);
    }
  }

  /// Deleted copy constructor.
  EventBaseEvent(const EventBaseEvent& other) = delete;
  /// Deleted copy assignment.
  EventBaseEvent& operator=(const EventBaseEvent& other) = delete;

  /// Callback type used to free the event's user data.
  using FreeFunction = void (*)(void* userData);

  /// Returns the underlying libevent event.
  ///
  /// \returns A pointer to the underlying libevent event.
  const struct event* getEvent() const { return &event_; }

  /// Returns the underlying libevent event.
  ///
  /// \returns A pointer to the underlying libevent event.
  struct event* getEvent() { return &event_; }

  /// Returns whether the event is currently registered.
  ///
  /// \returns True if the event is registered.
  bool isEventRegistered() const {
    return EventUtil::isEventRegistered(&event_);
  }

  /// Returns the file descriptor of the event.
  ///
  /// \returns The file descriptor associated with the event.
  libevent_fd_t eb_ev_fd() const { return event_.ev_fd; }

  /// Returns the event flags of the event.
  ///
  /// \returns The event flags associated with the event.
  short eb_ev_events() const { return event_.ev_events; }

  /// Returns the result flags of the event.
  ///
  /// \returns The result flags set on the last activation.
  int eb_ev_res() const { return event_.ev_res; }

  /// Returns the user data pointer attached to the event.
  ///
  /// \returns The user data pointer attached to the event.
  void* getUserData() { return userData_; }
  /// Returns the free function for the event's user data.
  ///
  /// \returns The free function for the event's user data.
  FreeFunction getFreeFunction() const { return freeFn_; }

  /// Sets the user data pointer attached to the event.
  ///
  /// \param userData The user data pointer to attach.
  void setUserData(void* userData) { userData_ = userData; }

  /// Sets the user data pointer and its free function.
  ///
  /// \param userData The user data pointer to attach.
  /// \param freeFn The function used to free the user data.
  void setUserData(void* userData, FreeFunction freeFn) {
    userData_ = userData;
    freeFn_ = freeFn;
  }

  /// Configures the event for a file descriptor and callback.
  ///
  /// \param fd The file descriptor to watch.
  /// \param events The event flags to watch for.
  /// \param callback The callback invoked when the event fires.
  /// \param arg The argument passed to the callback.
  void eb_event_set(
      libevent_fd_t fd,
      short events,
      void (*callback)(libevent_fd_t, short, void*),
      void* arg) {
    event_set(&event_, fd, events, callback, arg);
  }

  /// Configures the event as a persistent signal handler.
  ///
  /// \param signum The signal number to watch.
  /// \param callback The callback invoked when the signal fires.
  /// \param arg The argument passed to the callback.
  void eb_signal_set(
      int signum, void (*callback)(libevent_fd_t, short, void*), void* arg) {
    event_set(&event_, signum, EV_SIGNAL | EV_PERSIST, callback, arg);
  }

  /// Configures the event as a timer.
  ///
  /// \param callback The callback invoked when the timer fires.
  /// \param arg The argument passed to the callback.
  void eb_timer_set(void (*callback)(libevent_fd_t, short, void*), void* arg) {
    event_set(&event_, -1, 0, callback, arg);
  }

  /// Sets the EventBase that owns this event.
  ///
  /// \param evb The owning EventBase.
  void eb_ev_base(EventBase* evb);
  /// Returns the EventBase that owns this event.
  ///
  /// \returns The owning EventBase.
  EventBase* eb_ev_base() const { return evb_; }

  /// Associates the event with the given EventBase.
  ///
  /// \param evb The EventBase to associate the event with.
  /// \returns Zero on success, or a non-zero error code.
  int eb_event_base_set(EventBase* evb);

  /// Adds the event to its EventBase, optionally with a timeout.
  ///
  /// \param timeout The optional timeout, or null for none.
  /// \returns Zero on success, or a non-zero error code.
  int eb_event_add(const struct timeval* timeout);

  /// Removes the event from its EventBase.
  ///
  /// \returns Zero on success, or a non-zero error code.
  int eb_event_del();

  /// Marks the event as active with the given result flags.
  ///
  /// \param res The result flags to set.
  /// \returns True if the event was activated.
  bool eb_event_active(int res);

  /// Enables edge-triggered mode for the event.
  ///
  /// \returns True if edge-triggered mode was enabled.
  bool setEdgeTriggered();

 protected:
  /// The underlying libevent event.
  struct event event_;

  /// Returns the backend that owns this event.
  ///
  /// \returns The backend that owns this event.
  EventBaseBackendBase* getBackend() const;

  /// The EventBase that owns this event.
  EventBase* evb_{nullptr};
  /// The user data pointer attached to the event.
  void* userData_{nullptr};
  /// The function used to free the user data.
  FreeFunction freeFn_{nullptr};
};

/// Abstract base for the backend that drives an EventBase loop.
class EventBaseBackendBase {
 public:
  /// The event wrapper type used by this backend.
  using Event = EventBaseEvent;
  /// Factory callable that creates a backend instance.
  using FactoryFunc =
      std::function<std::unique_ptr<folly::EventBaseBackendBase>()>;
  /// Callback invoked with the number of bytes received via zero-copy.
  using RecvZcCallback = folly::Function<void(ssize_t)>;

  // Per-EventBase hooks invoked around the poll syscall (epoll_wait /
  // io_uring CQE reaping).  Only EpollBackend and IoUringBackend invoke
  // these hooks; other backends (e.g. LibeventBackend) do not.
  //
  // prePollLoopHook is called immediately before the poll syscall.
  // postPollLoopHook is called immediately after, receiving the number of
  // events returned by the syscall.
  // Both hooks share a single opaque context pointer.

  /// Hook invoked immediately before the poll syscall.
  using PrePollLoopHook = void (*)(void* ctx);
  /// Hook invoked immediately after the poll syscall.
  using PostPollLoopHook = void (*)(void* ctx, int numEvents);

  /// Pair of poll-loop hooks sharing a single opaque context pointer.
  struct PollLoopHook {
    /// The hook called immediately before the poll syscall.
    PrePollLoopHook preLoopHook = nullptr;
    /// The hook called immediately after the poll syscall.
    PostPollLoopHook postLoopHook = nullptr;
    /// The opaque context pointer passed to both hooks.
    void* hookCtx = nullptr;
  };

  /// Constructs a backend base.
  EventBaseBackendBase() = default;
  /// Destroys the backend base.
  virtual ~EventBaseBackendBase() = default;

  /// Deleted copy constructor.
  EventBaseBackendBase(const EventBaseBackendBase& other) = delete;
  /// Deleted copy assignment.
  EventBaseBackendBase& operator=(const EventBaseBackendBase& other) = delete;

  /// Returns a pollable file descriptor for the backend, if any.
  ///
  /// \returns The pollable file descriptor, or -1 if none.
  virtual int getPollableFd() const { return -1; }

  /// Returns the NAPI id of the backend, if any.
  ///
  /// \returns The NAPI id, or -1 if none.
  virtual int getNapiId() const { return -1; }
  /// Queues a zero-copy receive request on the backend.
  ///
  /// \param fd The file descriptor to receive from.
  /// \param buf The buffer that receives the data.
  /// \param nbytes The maximum number of bytes to receive.
  /// \param callback The callback invoked with the byte count.
  virtual void queueRecvZc(
      int fd,
      void* buf,
      unsigned long nbytes,
      RecvZcCallback&& callback) {}

  /// Sets the poll-loop hooks invoked around the poll syscall.
  ///
  /// \param pollLoopHook The hooks to install.
  void setPollLoopHook(PollLoopHook pollLoopHook) {
    pollLoopHook_ = pollLoopHook;
  }

  /// Returns the underlying libevent event_base.
  ///
  /// \returns The underlying libevent event_base.
  virtual event_base* getEventBase() = 0;
  /// Runs the backend event loop.
  ///
  /// \param flags The loop flags passed to the backend.
  /// \returns Zero on success, or a non-zero error code.
  virtual int eb_event_base_loop(int flags) = 0;
  /// Breaks out of the backend event loop.
  ///
  /// \returns Zero on success, or a non-zero error code.
  virtual int eb_event_base_loopbreak() = 0;

  /// Adds an event to the backend, optionally with a timeout.
  ///
  /// \param event The event to add.
  /// \param timeout The optional timeout, or null for none.
  /// \returns Zero on success, or a non-zero error code.
  virtual int eb_event_add(Event& event, const struct timeval* timeout) = 0;
  /// Removes an event from the backend.
  ///
  /// \param event The event to remove.
  /// \returns Zero on success, or a non-zero error code.
  virtual int eb_event_del(Event& event) = 0;

  /// Marks an event as active with the given result flags.
  ///
  /// \param event The event to activate.
  /// \param res The result flags to set.
  /// \returns True if the event was activated.
  virtual bool eb_event_active(Event& event, int res) = 0;

  /// Enables edge-triggered mode for an event.
  ///
  /// \param event The event to configure.
  /// \returns True if edge-triggered mode was enabled.
  virtual bool setEdgeTriggered(Event& event) { return false; }

 protected:
  /// The poll-loop hooks invoked around the poll syscall.
  PollLoopHook pollLoopHook_;
};

} // namespace folly
