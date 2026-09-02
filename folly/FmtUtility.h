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

#include <fmt/args.h>

#include <folly/CppAttributes.h>

namespace folly {

/// fmt_make_format_args_from_map_fn
/// fmt_make_format_args_from_map
///
/// A helper function-object type and variable for making a format-args object
/// from a map.
///
/// May be useful for transitioning from legacy folly::svformat to fmt::vformat.
struct fmt_make_format_args_from_map_fn {
  /// Builds a dynamic format-args store from the key/value pairs in `map`.
  ///
  /// \param map The map whose key/value pairs become format args.
  /// \returns A dynamic format-args store built from the map.
  template <typename Map>
  fmt::dynamic_format_arg_store<fmt::format_context> operator()(
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] Map const& map) const {
    fmt::dynamic_format_arg_store<fmt::format_context> ret;
    ret.reserve(map.size(), map.size());
    for (auto const& [key, val] : map) {
      ret.push_back(fmt::arg(key.c_str(), std::cref(val)));
    }
    return ret;
  }
};
/// Callable that makes a format-args object from a map.
inline constexpr fmt_make_format_args_from_map_fn
    fmt_make_format_args_from_map{};

/// fmt_vformat_mangle_name_fn
/// fmt_vformat_mangle_name
///
/// A helper function-object type and variable for mangling vformat named-arg
/// names which fmt::vformat might not otherwise permit.
struct fmt_vformat_mangle_name_fn {
  /// Returns a mangled copy of the named-arg name `str`.
  ///
  /// \param str The named-arg name to mangle.
  /// \returns A mangled copy of the named-arg name.
  std::string operator()(std::string_view const str) const;
  /// Appends the mangled form of the named-arg name `str` to `out`.
  ///
  /// \param out The string to append the mangled name to.
  /// \param str The named-arg name to mangle.
  void operator()(std::string& out, std::string_view const str) const;
};
/// Callable that mangles vformat named-arg names.
inline constexpr fmt_vformat_mangle_name_fn fmt_vformat_mangle_name{};

/// fmt_vformat_mangle_format_string_fn
/// fmt_vformat_mangle_format_string
///
/// A helper function-object type and variable for mangling the content of
/// vformat format-strings containing named-arg names which fmt::vformat might
/// not otherwise permit.
struct fmt_vformat_mangle_format_string_fn {
  /// Options controlling how a vformat format-string is mangled.
  struct options {
    /// Whether numeric args should be treated as named args.
    bool numeric_args_as_named = false;

    /// Sets whether numeric args are treated as named args; returns `*this`.
    ///
    /// \param value Whether numeric args should be treated as named args.
    /// \returns A reference to these options.
    options& set_numeric_args_as_named(bool value) noexcept {
      numeric_args_as_named = value;
      return *this;
    }
  };

  /// Returns a mangled copy of the vformat format-string `str`.
  ///
  /// \param str The vformat format-string to mangle.
  /// \returns A mangled copy of the format-string.
  std::string operator()(std::string_view const str) const;
  /// Returns a mangled copy of `str` using the given `opts`.
  ///
  /// \param opts The options controlling how the format-string is mangled.
  /// \param str The vformat format-string to mangle.
  /// \returns A mangled copy of the format-string.
  std::string operator()(options const& opts, std::string_view const str) const;
};
/// Callable that mangles the content of vformat format-strings.
inline constexpr fmt_vformat_mangle_format_string_fn
    fmt_vformat_mangle_format_string{};

/// Options type for fmt_vformat_mangle_format_string.
using fmt_vformat_mangle_format_string_options =
    fmt_vformat_mangle_format_string_fn::options;

} // namespace folly
