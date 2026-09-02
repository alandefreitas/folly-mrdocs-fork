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

#include <fmt/format.h>

#include <folly/Utility.h>
#include <folly/lang/Exception.h>
#include <folly/lang/SafeAssert.h>
#include <folly/portability/SourceLocation.h>

/// The Folly library.
namespace folly {

class rich_msg;

/// Extension-author-facing public details.
namespace ext { // For extension authors -- public details

/// ext::format_string_and_location
///
/// Rich error users probably want `rich_msg` instead, see below.  This is meant
/// to enable library authors to make concise APIs, in the style
/// `epitaph` or `...coded_rich_error`.
///
/// Captures a source location together with a literal string -- either for
/// `fmt` formatting with arguments, or one without substitutions. Usage:
/// template <typename... As> void
/// yourFn(format_string_and_location<std::type_identity_t<As>...> = "")
///
/// The `type_identity` allows implicit construction of this type from string
/// literals (its sole ctor is consteval).  Adding that nested template makes
/// the argument non-deducible.  Without that, template deduction would attempt
/// to deduce `const char [N]` -> `format_string_and_location`, which would fail
/// since template deduction does not consider implicit conversions.
///
/// BUG ALERT: Due to https://github.com/llvm/llvm-project/issues/137907 avoid
/// writing constructors like this: T(source_location sl =
/// source_location::current()) A constructor with a defaulted
/// `source_location` should take at least one mandatory argument to avoid this
/// pitfall.
template <typename... Args>
class format_string_and_location {
 private:
  friend class ::folly::rich_msg;

  source_location loc_;
  fmt::format_string<Args...> fmt_str_;
  literal_c_str lit_str_;

 public:
  /// Constructs from a string literal, capturing the call-site location.
  ///
  /// \param str The literal format string.
  /// \param loc The captured source location (defaults to the call site).
  /* implicit */ consteval format_string_and_location(
      const char* str, source_location loc = source_location::current())
      : loc_{std::move(loc)}, fmt_str_{str}, lit_str_{str} {}

  /// Returns the captured source location.
  ///
  /// \returns The source location.
  constexpr source_location location() const { return loc_; }

  /// Builds the message string when there are no format arguments.
  ///
  /// \param args Format arguments (empty in this overload).
  /// \returns The literal string as an `exception_shared_string`.
  constexpr exception_shared_string as_exception_shared_string(
      Args const&... args)
    requires(sizeof...(Args) == 0)
  {
    return exception_shared_string{lit_str_};
  }

  /// Builds the message string by formatting the arguments.
  ///
  /// \param args Format arguments for the message.
  /// \returns The formatted `exception_shared_string`.
  exception_shared_string as_exception_shared_string(Args const&... args)
    requires(sizeof...(Args) > 0)
  {
    // `fmt::runtime` is safe since our ctor checked the string/args combo
    //
    // Future: If this code shows up in your benchmark, check out the test
    // `exception_shared_string_format` for a possible micro-optimization that
    // involves customizing the type given to `fmt` based on
    // `is_register_pass_v`.
    return {
        fmt::formatted_size(fmt::runtime(fmt_str_), args...),
        [&](auto buf, auto len) {
          auto res =
              fmt::format_to_n(buf, len, fmt::runtime(fmt_str_), args...);
          FOLLY_SAFE_DCHECK(len == res.size);
        }};
  }
};

} // namespace ext

/// A message that can automatically capture source locations.
///
/// Usage:
///   rich_msg msg1{"error: {} at {}", code, line};
class rich_msg {
 private:
  exception_shared_string msg_;
  source_location loc_;

 public:
  /// Constructs from a format string (auto-capturing location) and arguments.
  ///
  /// Writing `rich_error{"fmt {} str {}", 1, 2}` auto-captures the location.
  ///
  /// \param snl The format string and captured source location.
  /// \param args Format arguments for the message.
  template <typename... As>
  explicit rich_msg(
      ext::format_string_and_location<std::type_identity_t<As>...> snl,
      As const&... args)
      : msg_{snl.as_exception_shared_string(args...)},
        loc_{std::move(snl.loc_)} {}

  // This overload exists specifically for use in `immortal_rich_error`, which
  // passes all of the error's constructor arguments as template parameters.
  // In C++20, this imposes severe constraints.  We cannot pass:
  //   - `source_location` since it's not a structural type.
  //   - `rich_msg`, since it's not structural (and can likely never be).
  //   - String literals, or `literal_c_str` because `char*` is forbidden in
  //     template parameters.
  //   - `literal_string`, because that instance would not have static storage
  //     and thus we could use its `c_str()` in this `consteval` code.
  //   - Format args, since our `fmt` isn't `constexpr` (yet).
  //
  /// Constructs an immortal message from a compile-time string tag.
  ///
  /// Implicitly converting from a `"foo"_litv` is about as good as this can get.
  ///
  /// \param tag Value tag carrying the compile-time string.
  template <literal_string Str>
  /* implicit */ consteval rich_msg(vtag_t<Str> tag)
      : msg_{literal_c_str{Str.c_str()}}, loc_{} {}

  /// Constructs from a pre-built message string and source location.
  ///
  /// \param msg The message string.
  /// \param loc The source location.
  rich_msg(exception_shared_string msg, source_location loc)
      : msg_{std::move(msg)}, loc_{std::move(loc)} {}

  /// Returns the captured source location.
  ///
  /// \returns The source location.
  constexpr const source_location& location() const noexcept { return loc_; }
  /// Returns the message text.
  ///
  /// \returns A NUL-terminated message string.
  constexpr const char* message() const noexcept { return msg_.what(); }
  /// Moves out the underlying message string.
  ///
  /// \returns The moved-out message string.
  exception_shared_string&& shared_string() && noexcept {
    return std::move(msg_);
  }
};

} // namespace folly
