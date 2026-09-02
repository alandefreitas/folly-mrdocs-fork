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

/**
 * Simple timeout queue.  Call user-specified callbacks when their timeouts
 * expire.
 *
 * This class assumes that "time" is an int64_t and doesn't care about time
 * units (seconds, milliseconds, etc).  You call runOnce() / runLoop() using
 * the same time units that you use to specify callbacks.
 */

#pragma once

#include <cstdint>
#include <functional>

#include <boost/multi_index/indexed_by.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

namespace folly {

/// A queue of scheduled timeout events keyed by expiration time.
class TimeoutQueue {
 public:
  /// The identifier type for a scheduled timeout event.
  using Id = int64_t;
  /// The callback type invoked when a timeout event fires.
  using Callback = std::function<void(Id, int64_t)>;

  /// Construct an empty timeout queue.
  TimeoutQueue() : nextId_(1) {}

  /**
   * Add a one-time timeout event that will fire "delay" time units from "now"
   * (that is, the first time that run*() is called with a time value >= now
   * + delay).
   *
   * \param now The current time.
   * \param delay The delay, in time units, before the event fires.
   * \param callback The callback to invoke when the event fires.
   * \returns The id of the newly scheduled event.
   */
  Id add(int64_t now, int64_t delay, Callback callback);

  /**
   * Add a repeating timeout event that will fire every "interval" time units
   * (it will first fire when run*() is called with a time value >=
   * now + interval).
   *
   * run*() will always invoke each repeating event at most once, even if
   * more than one "interval" period has passed.
   *
   * \param now The current time.
   * \param interval The interval, in time units, between firings.
   * \param callback The callback to invoke each time the event fires.
   * \returns The id of the newly scheduled event.
   */
  Id addRepeating(int64_t now, int64_t interval, Callback callback);

  /**
   * Erase a given timeout event, returns true if the event was actually
   * erased and false if it didn't exist in our queue.
   *
   * \param id The id of the event to erase.
   * \returns True if the event existed and was erased.
   */
  bool erase(Id id);

  /**
   * Process all events that are due at times <= "now" by calling their
   * callbacks.
   *
   * Callbacks are allowed to call back into the queue and add / erase events;
   * they might create more events that are already due.  In this case,
   * runOnce() will only go through the queue once, and return a "next
   * expiration" time in the past or present (<= now); runLoop()
   * will process the queue again, until there are no events already due.
   *
   * Note that it is then possible for runLoop to never return if
   * callbacks re-add themselves to the queue (or if you have repeating
   * callbacks with an interval of 0).
   *
   * Return the time that the next event will be due (same as
   * nextExpiration(), below)
   *
   * \param now The current time; events due at times <= now are processed.
   * \returns The time that the next event will be due.
   */
  int64_t runOnce(int64_t now) { return runInternal(now, true); }
  /// Process due events, looping until no events are already due.
  ///
  /// \param now The current time; events due at times <= now are processed.
  /// \returns The time that the next event will be due.
  int64_t runLoop(int64_t now) { return runInternal(now, false); }

  /**
   * Return the time that the next event will be due.
   *
   * \returns The time that the next event will be due.
   */
  int64_t nextExpiration() const;

 private:
  int64_t runInternal(int64_t now, bool onceOnly);
  TimeoutQueue(const TimeoutQueue&) = delete;
  TimeoutQueue& operator=(const TimeoutQueue&) = delete;

  struct Event {
    Id id;
    int64_t expiration;
    int64_t repeatInterval;
    Callback callback;
  };

  using Set = boost::multi_index_container<
      Event,
      boost::multi_index::indexed_by<
          boost::multi_index::ordered_unique<
              boost::multi_index::member<Event, Id, &Event::id>>,
          boost::multi_index::ordered_non_unique<
              boost::multi_index::member<Event, int64_t, &Event::expiration>>>>;

  enum {
    BY_ID = 0,
    BY_EXPIRATION = 1,
  };

  Set timeouts_;
  Id nextId_;
};

} // namespace folly
