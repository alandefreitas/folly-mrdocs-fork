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

#include <folly/Range.h>
#include <folly/io/async/EventBase.h>

/// Facebook Folly library namespace.
namespace folly {

/// Event loop and I/O multiplexer bound to a single thread.
class EventBase;
/// Abstract base for the backend that drives an EventBase loop.
class EventBaseBackendBase;
/// Manager for per-thread EventBase objects.
class EventBaseManager;
/// Helper that runs an EventBase loop on a dedicated std::thread.
class ScopedEventBaseThread;

/// Owns an EventBase running on its own thread.
class EventBaseThread {
 public:
  /// Constructs and optionally starts the thread.
  EventBaseThread();
  /// Constructs the thread, optionally starting it immediately.
  ///
  /// \param autostart Whether to start the thread immediately.
  /// \param ebm The EventBaseManager to associate the EventBase with.
  /// \param threadName The name given to the new thread.
  explicit EventBaseThread(
      bool autostart,
      EventBaseManager* ebm = nullptr,
      folly::StringPiece threadName = folly::StringPiece());
  /// Constructs the thread with custom EventBase options.
  ///
  /// \param autostart Whether to start the thread immediately.
  /// \param eventBaseOptions The options applied to the owned EventBase.
  /// \param ebm The EventBaseManager to associate the EventBase with.
  /// \param threadName The name given to the new thread.
  EventBaseThread(
      bool autostart,
      EventBase::Options eventBaseOptions,
      EventBaseManager* ebm = nullptr,
      folly::StringPiece threadName = folly::StringPiece());
  /// Constructs the thread using the given EventBaseManager.
  ///
  /// \param ebm The EventBaseManager to associate the EventBase with.
  explicit EventBaseThread(EventBaseManager* ebm);
  /// Stops the thread and destroys the object.
  ~EventBaseThread();

  /// Deleted copy constructor.
  EventBaseThread(EventBaseThread const& other) = delete;
  /// Deleted copy assignment.
  EventBaseThread& operator=(EventBaseThread const& other) = delete;
  /// Move constructor.
  ///
  /// \param other The object to move from.
  EventBaseThread(EventBaseThread&& other) noexcept;
  /// Move assignment.
  ///
  /// \param other The object to move from.
  /// \returns A reference to this object.
  EventBaseThread& operator=(EventBaseThread&& other) noexcept;

  /// Returns the EventBase running on the thread.
  ///
  /// \returns The EventBase running on the thread.
  EventBase* getEventBase() const;

  /// Returns whether the thread is currently running.
  ///
  /// \returns True if the thread is currently running.
  bool running() const;
  /// Starts the thread.
  ///
  /// \param threadName The name given to the new thread.
  void start(folly::StringPiece threadName = folly::StringPiece());
  /// Stops the thread.
  void stop();

 private:
  EventBaseManager* ebm_;
  EventBase::Options ebOpts_;
  std::unique_ptr<ScopedEventBaseThread> th_;
};
} // namespace folly
