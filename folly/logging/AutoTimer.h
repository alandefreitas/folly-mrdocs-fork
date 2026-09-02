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

#include <chrono>
#include <string>
#include <type_traits>

#include <fmt/format.h>
#include <glog/logging.h>

#include <folly/Conv.h>
#include <folly/Optional.h>
#include <folly/String.h>

/// The Folly library.
namespace folly {

/// Output style for the default glog-based `AutoTimer` logger.
enum class GoogleLoggerStyle {
  SECONDS, ///< Print the duration as a plain number of seconds.
  PRETTY, ///< Print the duration in a human-readable form.
};
template <GoogleLoggerStyle>
struct GoogleLogger;

/**
 * Automatically times a block of code, printing a specified log message on
 * destruction or whenever the log() method is called. For example:
 *
 *   AutoTimer t("Foo() completed");
 *   doWork();  // Takes ~1.2 seconds
 *   t.log("Do work finished");
 *   doMoreWork();  // Takes ~1.1 seconds
 *
 * This would print something like:
 *   "Do work finished in 1.2 seconds"
 *   "Foo() completed in 1.1 seconds"
 *
 * Note that the start of the timer is reset after every call to log(). This
 * means the destruction message printed after a prior call to log() will
 * log the time since the last call to log(), not the time since the object
 * was created.
 *
 * You can customize what you use as the logger and clock. The logger needs
 * to have an operator()(StringPiece, std::chrono::duration<double>) that
 * gets a message and a duration. The clock needs to model Clock from
 * std::chrono.
 *
 * The default logger logs usings glog. It only logs if the message is
 * non-empty, so you can also just use this class for timing, e.g.:
 *
 *   AutoTimer t;
 *   doWork()
 *   const auto how_long = t.log();
 */
template <
    class Logger = GoogleLogger<GoogleLoggerStyle::PRETTY>,
    class Clock = std::chrono::high_resolution_clock>
class AutoTimer final {
 public:
  /// A duration measured in seconds using a `double` representation.
  using DoubleSeconds = std::chrono::duration<double>;

  /// Constructs a timer and starts measuring immediately.
  ///
  /// \param msg The message to log on destruction; empty disables it.
  /// \param minTimetoLog The minimum duration before a message is logged.
  /// \param logger The logger used to emit timing messages.
  explicit AutoTimer(
      std::string&& msg = "",
      const DoubleSeconds& minTimetoLog = DoubleSeconds::zero(),
      Logger&& logger = Logger())
      : destructionMessage_(std::move(msg)),
        minTimeToLog_(minTimetoLog),
        logger_(std::move(logger)) {}

  // It doesn't really make sense to copy AutoTimer
  // Movable to make sure the helper method for creating an AutoTimer works.
  /// Deleted copy constructor; timers are not copyable.
  ///
  /// \param other The timer that would be copied.
  AutoTimer(const AutoTimer& other) = delete;
  /// Move constructor.
  ///
  /// \param other The timer to move from.
  AutoTimer(AutoTimer&& other) = default;
  /// Deleted copy assignment; timers are not copyable.
  ///
  /// \param other The timer that would be assigned from.
  /// \returns A reference to this timer.
  AutoTimer& operator=(const AutoTimer& other) = delete;
  /// Move assignment.
  ///
  /// \param other The timer to move from.
  /// \returns A reference to this timer.
  AutoTimer& operator=(AutoTimer&& other) = default;

  /// Logs the destruction message, if any, and stops the timer.
  ~AutoTimer() {
    if (destructionMessage_) {
      log(destructionMessage_.value());
    }
  }

  /// Logs the elapsed time with an optional message and resets the timer.
  ///
  /// \param msg The message to log.
  /// \returns The elapsed duration since the last log or construction.
  DoubleSeconds log(StringPiece msg = "") { return logImpl(Clock::now(), msg); }

  /// Logs the elapsed time with a concatenated message and resets the timer.
  ///
  /// \param args The values to concatenate into the log message.
  /// \returns The elapsed duration since the last log or construction.
  template <typename... Args>
  DoubleSeconds log(Args&&... args) {
    auto now = Clock::now();
    return logImpl(now, to<std::string>(std::forward<Args>(args)...));
  }

  /// Logs the elapsed time with a formatted message and resets the timer.
  ///
  /// \param fmt The format string for the message.
  /// \param args The arguments substituted into the format string.
  /// \returns The elapsed duration since the last log or construction.
  template <typename... Args>
  DoubleSeconds logFormat(fmt::format_string<Args...> fmt, Args&&... args) {
    auto now = Clock::now();
    return logImpl(now, fmt::format(fmt, std::forward<Args>(args)...));
  }

 private:
  // We take in the current time so that we don't measure time to call
  // to<std::string> or format() in the duration.
  DoubleSeconds logImpl(std::chrono::time_point<Clock> now, StringPiece msg) {
    auto duration = now - start_;
    if (duration >= minTimeToLog_) {
      logger_(msg, duration);
    }
    start_ = Clock::now(); // Don't measure logging time
    return duration;
  }

  Optional<std::string> destructionMessage_;
  std::chrono::time_point<Clock> start_ = Clock::now();
  DoubleSeconds minTimeToLog_;
  Logger logger_;
};

/// Creates an `AutoTimer` with deduced logger and clock types.
///
/// \param msg The message to log on destruction; empty disables it.
/// \param minTimeToLog The minimum duration before a message is logged.
/// \param logger The logger used to emit timing messages.
/// \returns The constructed `AutoTimer`.
template <
    class Logger = GoogleLogger<GoogleLoggerStyle::PRETTY>,
    class Clock = std::chrono::high_resolution_clock>
auto makeAutoTimer(
    std::string&& msg = "",
    const std::chrono::duration<double>& minTimeToLog =
        std::chrono::duration<double>::zero(),
    Logger&& logger = Logger()) {
  return AutoTimer<Logger, Clock>(
      std::move(msg), minTimeToLog, std::move(logger));
}

/// The default `AutoTimer` logger, which writes timing messages via glog.
template <GoogleLoggerStyle Style>
struct GoogleLogger final {
  /// Logs a timing message using glog.
  ///
  /// \param msg The message to log; nothing is logged when it is empty.
  /// \param sec The measured duration.
  void operator()(
      StringPiece msg, const std::chrono::duration<double>& sec) const {
    if (msg.empty()) {
      return;
    }
    if (Style == GoogleLoggerStyle::PRETTY) {
      LOG(INFO) << msg << " in "
                << prettyPrint(sec.count(), PrettyType::PRETTY_TIME);
    } else {
      LOG(INFO) << msg << " in " << sec.count() << " seconds";
    }
  }
};
} // namespace folly
