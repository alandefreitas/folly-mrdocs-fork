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

#include <stdexcept>

#include <folly/CPortability.h>
#include <folly/Conv.h>
#include <folly/Likely.h>
#include <folly/Portability.h>
#include <folly/Range.h>
#include <folly/lang/Exception.h>

namespace folly {

struct FormatArg;

/// Exception thrown when a format argument string is invalid.
class FOLLY_EXPORT BadFormatArg : public std::invalid_argument {
 private:
  friend struct FormatArg;
  struct ErrorStrTag {};

  template <typename... A>
  static std::string str(StringPiece descr, A const&... a) {
    return to<std::string>(
        "invalid format argument {"_sp, descr, "}: "_sp, a...);
  }

 public:
  /// Inherit the constructors of std::invalid_argument.
  using invalid_argument::invalid_argument;
  /// Constructs the exception from a description and extra context arguments.
  /// \tparam A The extra context argument types.
  /// \param unused Tag selecting the error-string overload.
  /// \param descr A description of the invalid argument.
  /// \param a Extra context values appended to the message.
  template <typename... A>
  explicit BadFormatArg(ErrorStrTag unused, StringPiece descr, A const&... a)
      : invalid_argument(str(descr, a...)) {}
};

/**
 * Parsed format argument.
 */
struct FormatArg {
  /**
   * Parse a format argument from a string.  Keeps a reference to the
   * passed-in string -- does not copy the given characters.
   * \param sp The format argument string to parse.
   */
  explicit FormatArg(StringPiece sp)
      : fullArgString(sp),
        fill(kDefaultFill),
        align(Align::DEFAULT),
        sign(Sign::DEFAULT),
        basePrefix(false),
        thousandsSeparator(false),
        trailingDot(false),
        width(kDefaultWidth),
        widthIndex(kNoIndex),
        precision(kDefaultPrecision),
        presentation(kDefaultPresentation),
        nextKeyMode_(NextKeyMode::NONE) {
    if (!sp.empty()) {
      initSlow();
    }
  }

  /// Argument category used when validating a format argument.
  enum class Type {
    INTEGER, ///< An integer argument.
    FLOAT, ///< A floating-point argument.
    OTHER, ///< Any other argument type.
  };
  /**
   * Validate the argument for the given type; throws on error.
   * \param type The argument category to validate against.
   */
  void validate(Type type) const;

  /**
   * Throw an exception if the first argument is false.  The exception
   * message will contain the argument string as well as any passed-in
   * arguments to enforce, formatted using folly::to<std::string>.
   * \tparam Check The condition type (must be castable to bool).
   * \tparam Args The extra context argument types.
   * \param v The condition to check.
   * \param args Extra context values included in the error message.
   */
  template <typename Check, typename... Args>
  void enforce(Check const& v, Args&&... args) const {
    static_assert(std::is_constructible<bool, Check>::value, "not castable");
    if (FOLLY_UNLIKELY(!v)) {
      error(static_cast<Args&&>(args)...);
    }
  }

  /// Throws a BadFormatArg carrying the argument string and extra context.
  /// \tparam Args The extra context argument types.
  /// \param args Extra context values included in the error message.
  template <typename... Args>
  [[noreturn]] void error(Args&&... args) const;

  /**
   * Full argument string, as passed in to the constructor.
   */
  StringPiece fullArgString;

  /// Default fill character.
  static constexpr char kDefaultFill = '\0';
  /// Fill character used for padding.
  char fill;

  /// Field alignment mode.
  enum class Align : uint8_t {
    DEFAULT, ///< Default alignment for the argument type.
    LEFT, ///< Left-align within the field.
    RIGHT, ///< Right-align within the field.
    PAD_AFTER_SIGN, ///< Pad after the sign character.
    CENTER, ///< Center within the field.
    INVALID, ///< Invalid alignment.
  };
  /// Field alignment.
  Align align;

  /// Sign display mode.
  enum class Sign : uint8_t {
    DEFAULT, ///< Default sign handling for the argument type.
    PLUS_OR_MINUS, ///< Always show a plus or minus sign.
    MINUS, ///< Show a minus sign only for negatives.
    SPACE_OR_MINUS, ///< Show a leading space for positives, minus for negatives.
    INVALID, ///< Invalid sign mode.
  };
  /// Sign handling.
  Sign sign;

  /// Output base prefix (0 for octal, 0x for hex).
  bool basePrefix;

  /// Output thousands separator (comma).
  bool thousandsSeparator;

  /// Force a trailing decimal on doubles which could be rendered as ints.
  bool trailingDot;

  /// Default field width sentinel.
  static constexpr int kDefaultWidth = -1;
  /// Dynamic field width sentinel (width taken from an argument).
  static constexpr int kDynamicWidth = -2;
  /// Sentinel for an absent argument index.
  static constexpr int kNoIndex = -1;
  /// Field width.
  int width;
  /// Optional argument index supplying the width.
  int widthIndex;

  /// Default precision sentinel.
  static constexpr int kDefaultPrecision = -1;
  /// Output precision.
  int precision;

  /// Default presentation character.
  static constexpr char kDefaultPresentation = '\0';
  /// Presentation character.
  char presentation;

  /**
   * Split a key component from "key", which must be non-empty (an exception
   * is thrown otherwise).
   * \tparam emptyOk Whether an empty key component is allowed.
   * \returns The next key component.
   */
  template <bool emptyOk = false>
  StringPiece splitKey();

  /**
   * Is the entire key empty?
   * \returns true if there is no remaining key.
   */
  bool keyEmpty() const {
    return nextKeyMode_ == NextKeyMode::NONE && key_.empty();
  }

  /**
   * Split an key component from "key", which must be non-empty and a valid
   * integer (an exception is thrown otherwise).
   * \returns The next key component as an integer.
   */
  int splitIntKey();

  /// Sets the next key to a pre-parsed integer value.
  /// \param val The integer key to use next.
  void setNextIntKey(int val) {
    assert(nextKeyMode_ == NextKeyMode::NONE);
    nextKeyMode_ = NextKeyMode::INT;
    nextIntKey_ = val;
  }

  /// Sets the next key to a pre-parsed string value.
  /// \param val The string key to use next.
  void setNextKey(StringPiece val) {
    assert(nextKeyMode_ == NextKeyMode::NONE);
    nextKeyMode_ = NextKeyMode::STRING;
    nextKey_ = val;
  }

 private:
  void initSlow();
  template <bool emptyOk>
  StringPiece doSplitKey();

  StringPiece key_;
  int nextIntKey_;
  StringPiece nextKey_;
  enum class NextKeyMode {
    NONE,
    INT,
    STRING,
  };
  NextKeyMode nextKeyMode_;
};

template <typename... Args>
[[noreturn]] inline void FormatArg::error(Args&&... args) const {
  // take advantage of throw_exception decaying char const (&)[N} to char const*
  // as a special case of the facility
  throw_exception<BadFormatArg>(
      BadFormatArg::ErrorStrTag{}, fullArgString, static_cast<Args&&>(args)...);
}

template <bool emptyOk>
inline StringPiece FormatArg::splitKey() {
  enforce(nextKeyMode_ != NextKeyMode::INT, "integer key expected");
  return doSplitKey<emptyOk>();
}

template <bool emptyOk>
inline StringPiece FormatArg::doSplitKey() {
  if (nextKeyMode_ == NextKeyMode::STRING) {
    nextKeyMode_ = NextKeyMode::NONE;
    if (!emptyOk) { // static
      enforce(!nextKey_.empty(), "non-empty key required");
    }
    return nextKey_;
  }

  if (key_.empty()) {
    if (!emptyOk) { // static
      error("non-empty key required");
    }
    return StringPiece();
  }

  const char* b = key_.begin();
  const char* e = key_.end();
  const char* p;
  if (e[-1] == ']') {
    --e;
    p = static_cast<const char*>(memchr(b, '[', size_t(e - b)));
    enforce(p != nullptr, "unmatched ']'");
  } else {
    p = static_cast<const char*>(memchr(b, '.', size_t(e - b)));
  }
  if (p) {
    key_.assign(p + 1, e);
  } else {
    p = e;
    key_.clear();
  }
  if (!emptyOk) { // static
    enforce(b != p, "non-empty key required");
  }
  return StringPiece(b, p);
}

inline int FormatArg::splitIntKey() {
  if (nextKeyMode_ == NextKeyMode::INT) {
    nextKeyMode_ = NextKeyMode::NONE;
    return nextIntKey_;
  }
  auto result = tryTo<int>(doSplitKey<true>());
  enforce(result, "integer key required");
  return *result;
}

} // namespace folly
