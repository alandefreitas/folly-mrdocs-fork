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
#include <iosfwd>
#include <string>
#include <type_traits>

#include <folly/Portability.h>
#include <folly/Range.h>

/// The Folly library.
namespace folly {

/**
 * Log level values.
 *
 * Higher levels are more important than lower ones.
 *
 * However, the numbers in the DBG* and INFO* level names are reversed, and can
 * be thought of as debug verbosity levels.  Increasing DBG* numbers mean
 * increasing level of verbosity.  DBG0 is the least verbose debug level, DBG1
 * is one level higher of verbosity, etc.
 */
enum class LogLevel : uint32_t {
  UNINITIALIZED = 0, ///< No log level has been set yet.
  NONE = 1, ///< No messages should be logged.
  MIN_LEVEL = 1, ///< The minimum valid log level value.

  // "DBG" is the lowest (aka most verbose) debug log level.
  // This level is intended to be primarily used in log category settings.
  // In your code it is usually better to use one of the finer-grained DBGn
  // levels.  In your log category settings you can then set the log category
  // level to a specific DBGn level, or to to main DBG level to enable all DBGn
  // messages.
  //
  // This is named "DBG" rather than "DEBUG" since some open source projects
  // define "DEBUG" as a preprocessor macro.
  DBG = 1000, ///< The lowest (most verbose) debug log level.

  // Fine-grained debug log levels.
  DBG0 = 1999, ///< Fine-grained debug log level 0 (least verbose).
  DBG1 = 1998, ///< Fine-grained debug log level 1.
  DBG2 = 1997, ///< Fine-grained debug log level 2.
  DBG3 = 1996, ///< Fine-grained debug log level 3.
  DBG4 = 1995, ///< Fine-grained debug log level 4.
  DBG5 = 1994, ///< Fine-grained debug log level 5.
  DBG6 = 1993, ///< Fine-grained debug log level 6.
  DBG7 = 1992, ///< Fine-grained debug log level 7.
  DBG8 = 1991, ///< Fine-grained debug log level 8.
  DBG9 = 1990, ///< Fine-grained debug log level 9 (most verbose).

  INFO = 2000, ///< The informational log level.
  // Fine-grained info log levels.
  INFO0 = 2999, ///< Fine-grained info log level 0 (least verbose).
  INFO1 = 2998, ///< Fine-grained info log level 1.
  INFO2 = 2997, ///< Fine-grained info log level 2.
  INFO3 = 2996, ///< Fine-grained info log level 3.
  INFO4 = 2995, ///< Fine-grained info log level 4.
  INFO5 = 2994, ///< Fine-grained info log level 5.
  INFO6 = 2993, ///< Fine-grained info log level 6.
  INFO7 = 2992, ///< Fine-grained info log level 7.
  INFO8 = 2991, ///< Fine-grained info log level 8.
  INFO9 = 2990, ///< Fine-grained info log level 9 (most verbose).

  WARN = 3000, ///< The warning log level.
  WARNING = 3000, ///< The warning log level (alias for WARN).

  // Unfortunately Windows headers #define ERROR, so we cannot use
  // it as an enum value name.  We only provide ERR instead.
  ERR = 4000, ///< The error log level.

  CRITICAL = 5000, ///< The critical log level.

  DFATAL = 0x7ffffffe, ///< Crashes the program on debug builds.
  // FATAL log messages always abort the program.
  // This level is equivalent to MAX_LEVEL.
  FATAL = 0x7fffffff, ///< Always aborts the program; equivalent to MAX_LEVEL.

  // The most significant bit is used by LogCategory to store a flag value,
  // so the maximum value has that bit cleared.
  //
  // (We call this MAX_LEVEL instead of MAX just since MAX() is commonly
  // defined as a preprocessor macro by some C headers.)
  MAX_LEVEL = 0x7fffffff, ///< The maximum valid log level value.
};

/// The default log level used when none is specified.
constexpr LogLevel kDefaultLogLevel = LogLevel::INFO;
/// The lowest log level considered fatal for the current build.
constexpr LogLevel kMinFatalLogLevel =
    folly::kIsDebug ? LogLevel::DFATAL : LogLevel::FATAL;

/*
 * Support adding and subtracting integers from LogLevels, to create slightly
 * adjusted log level values.
 */
/// Add an integer to a LogLevel, capping the result at LogLevel::MAX_LEVEL.
///
/// \param level The base log level.
/// \param value The amount to add.
/// \returns The adjusted log level.
inline constexpr LogLevel operator+(LogLevel level, uint32_t value) {
  // Cap the result at LogLevel::MAX_LEVEL
  return ((static_cast<uint32_t>(level) + value) >
          static_cast<uint32_t>(LogLevel::MAX_LEVEL))
      ? LogLevel::MAX_LEVEL
      : static_cast<LogLevel>(static_cast<uint32_t>(level) + value);
}
/// Add an integer to a LogLevel in place, capping at LogLevel::MAX_LEVEL.
///
/// \param level The log level to adjust.
/// \param value The amount to add.
/// \returns A reference to the adjusted log level.
inline LogLevel& operator+=(LogLevel& level, uint32_t value) {
  level = level + value;
  return level;
}
/// Subtract an integer from a LogLevel.
///
/// \param level The base log level.
/// \param value The amount to subtract.
/// \returns The adjusted log level.
inline constexpr LogLevel operator-(LogLevel level, uint32_t value) {
  return static_cast<LogLevel>(static_cast<uint32_t>(level) - value);
}
/// Subtract an integer from a LogLevel in place.
///
/// \param level The log level to adjust.
/// \param value The amount to subtract.
/// \returns A reference to the adjusted log level.
inline LogLevel& operator-=(LogLevel& level, uint32_t value) {
  level = level - value;
  return level;
}

/**
 * Construct a LogLevel from a string name.
 *
 * \param name The string name of the log level.
 * \returns The LogLevel corresponding to the given name.
 */
LogLevel stringToLogLevel(folly::StringPiece name);

/**
 * Get a human-readable string representing the LogLevel.
 *
 * \param level The log level to convert.
 * \returns A human-readable string representing the log level.
 */
std::string logLevelToString(LogLevel level);

/**
 * Print a LogLevel in a human readable format.
 *
 * \param os The output stream to write to.
 * \param level The log level to print.
 * \returns A reference to the output stream.
 */
std::ostream& operator<<(std::ostream& os, LogLevel level);

/**
 * Returns true if and only if a LogLevel is fatal.
 *
 * \param level The log level to test.
 * \returns `true` if the level is fatal, `false` otherwise.
 */
inline constexpr bool isLogLevelFatal(LogLevel level) {
  return folly::kIsDebug
      ? (level >= LogLevel::DFATAL)
      : (level >= LogLevel::FATAL);
}
} // namespace folly
