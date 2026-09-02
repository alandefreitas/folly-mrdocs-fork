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

#include <cstddef>

#include <glog/logging.h>

#include <folly/io/async/EventBaseBackendBase.h>
#include <folly/io/async/EventUtil.h>
#include <folly/net/NetworkSocket.h>
#include <folly/portability/Event.h>

namespace folly {

class EventBase;

/**
 * The EventHandler class is used to asynchronously wait for events on a file
 * descriptor.
 *
 * Users that wish to wait on I/O events should derive from EventHandler and
 * implement the handlerReady() method.
 */
class EventHandler {
 public:
  /// Bitset flags describing the I/O events a handler can wait for.
  enum EventFlags {
    NONE = 0, ///< No events.
    READ = EV_READ, ///< The file descriptor is ready for reading.
    WRITE = EV_WRITE, ///< The file descriptor is ready for writing.
    READ_WRITE = (READ | WRITE), ///< The file descriptor is ready for reading or writing.
    PERSIST = EV_PERSIST, ///< Keep the handler registered after it fires.
// Temporary flag until EPOLLPRI is upstream on libevent.
#ifdef EV_PRI
    PRI = EV_PRI, ///< The file descriptor has priority (urgent) data to read.
#endif
  };

  /**
   * Create a new EventHandler object.
   *
   * @param eventBase  The EventBase to use to drive this event handler.
   *                   This may be nullptr, in which case the EventBase must be
   *                   set separately using initHandler() or attachEventBase()
   *                   before the handler can be registered.
   * @param fd         The file descriptor that this EventHandler will
   *                   monitor.  This may be -1, in which case the file
   *                   descriptor must be set separately using initHandler() or
   *                   changeHandlerFD() before the handler can be registered.
   */
  explicit EventHandler(
      EventBase* eventBase = nullptr, NetworkSocket fd = NetworkSocket());

  /// Deleted copy constructor; EventHandler objects are not copyable.
  ///
  /// \param other The handler that would be copied.
  EventHandler(const EventHandler& other) = delete;
  /// Deleted copy assignment; EventHandler objects are not copyable.
  ///
  /// \param other The handler that would be assigned from.
  /// \returns A reference to this handler.
  EventHandler& operator=(const EventHandler& other) = delete;

  /**
   * EventHandler destructor.
   *
   * The event will be automatically unregistered if it is still registered.
   */
  virtual ~EventHandler();

  /**
   * handlerReady() is invoked when the handler is ready.
   *
   * @param events  A bitset indicating the events that are ready.
   */
  virtual void handlerReady(uint16_t events) noexcept = 0;

  /**
   * Register the handler.
   *
   * If the handler is already registered, the registration will be updated
   * to wait on the new set of events.
   *
   * @param events   A bitset specifying the events to monitor.
   *                 If the PERSIST bit is set, the handler will remain
   *                 registered even after handlerReady() is called.
   *
   * @return Returns true if the handler was successfully registered,
   *         or false if an error occurred.  After an error, the handler is
   *         always unregistered, even if it was already registered prior to
   *         this call to registerHandler().
   */
  bool registerHandler(uint16_t events) { return registerImpl(events, false); }

  /**
   * Unregister the handler, if it is registered.
   */
  void unregisterHandler();

  /**
   * Returns true if the handler is currently registered.
   *
   * \returns true if the handler is currently registered, false otherwise.
   */
  bool isHandlerRegistered() const { return event_.isEventRegistered(); }

  /**
   * Attach the handler to a EventBase.
   *
   * This may only be called if the handler is not currently attached to a
   * EventBase (either by using the default constructor, or by calling
   * detachEventBase()).
   *
   * This method must be invoked in the EventBase's thread.
   *
   * \param eventBase The EventBase to attach this handler to.
   */
  void attachEventBase(EventBase* eventBase);

  /**
   * Detach the handler from its EventBase.
   *
   * This may only be called when the handler is not currently registered.
   * Once detached, the handler may not be registered again until it is
   * re-attached to a EventBase by calling attachEventBase().
   *
   * This method must be called from the current EventBase's thread.
   */
  void detachEventBase();

  /**
   * Change the file descriptor that this handler is associated with.
   *
   * This may only be called when the handler is not currently registered.
   *
   * \param fd The new file descriptor to associate with this handler.
   */
  void changeHandlerFD(NetworkSocket fd);

  /**
   * Attach the handler to a EventBase, and change the file descriptor.
   *
   * This method may only be called if the handler is not currently attached to
   * a EventBase.  This is primarily intended to be used to initialize
   * EventHandler objects created using the default constructor.
   *
   * \param eventBase The EventBase to attach this handler to.
   * \param fd The file descriptor to associate with this handler.
   */
  void initHandler(EventBase* eventBase, NetworkSocket fd);

  /**
   * Return the set of events that we're currently registered for.
   *
   * \returns A bitset of the events currently registered, or 0 if the
   *          handler is not registered.
   */
  uint16_t getRegisteredEvents() const {
    return (isHandlerRegistered()) ? (uint16_t)(event_.eb_ev_events()) : 0u;
  }

  /**
   * Register the handler as an internal event.
   *
   * This event will not count as an active event for determining if the
   * EventBase loop has more events to process.  The EventBase loop runs
   * only as long as there are active EventHandlers, however "internal" event
   * handlers are not counted.  Therefore this event handler will not prevent
   * EventBase loop from exiting with no more work to do if there are no other
   * non-internal event handlers registered.
   *
   * This is intended to be used only in very rare cases by the internal
   * EventBase code.  This API is not guaranteed to remain stable or portable
   * in the future.
   *
   * \param events A bitset specifying the events to monitor.
   * \returns true if the handler was successfully registered, false otherwise.
   */
  bool registerInternalHandler(uint16_t events) {
    return registerImpl(events, true);
  }

  /// Returns true if the handler is registered but has not yet fired.
  ///
  /// \returns true if the handler is pending, false otherwise.
  bool isPending() const;

  /**
   * If supported by the backend updates the event to be edge-triggered.
   * Returns true iff the update was successful.
   *
   * This should only be used for already registered events (e.g. after
   * registerHandler/registerInternalHandler calls). Calling any other method
   * on the EventHandler may reset this flag.
   * This can be useful to avoid read calls with eventfds.
   *
   * \returns true if the event was updated to be edge-triggered, false
   *          otherwise.
   */
  bool setEdgeTriggered() { return event_.setEdgeTriggered(); }

  /**
   * Make an event active.
   *
   * \param res The set of events to mark as active on this handler.
   * \returns true if the event was successfully activated, false otherwise.
   */
  bool activateEvent(int res) { return event_.eb_event_active(res); }

 private:
  bool registerImpl(uint16_t events, bool internal);
  void ensureNotRegistered(const char* fn);

  void setEventBase(EventBase* eventBase);

  static void libeventCallback(libevent_fd_t fd, short events, void* arg);

  EventBaseBackendBase::Event event_;
  EventBase* eventBase_;
};

} // namespace folly
