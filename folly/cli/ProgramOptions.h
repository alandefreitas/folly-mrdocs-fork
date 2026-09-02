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

#include <boost/program_options.hpp>

#include <folly/Optional.h>
#include <folly/portability/GFlags.h>

namespace folly {

#if FOLLY_HAVE_LIBGFLAGS && __has_include(<gflags/gflags.h>)
enum class ProgramOptionsStyle {
  GFLAGS,
  GNU,
};

// Add all GFlags to the given options_description.
// Use this *instead of* gflags::ParseCommandLineFlags().
//
// in GFLAGS style, the flags are named as per gflags conventions:
//   names_with_underscores
//   boolean flags have a "no" prefix
//
// in GNU style, the flags are named as per GNU conventions:
//   names-with-dashes
//   boolean flags have a "no-" prefix
//
// Consider (for example) a boolean flag:
//   DEFINE_bool(flying_pigs, false, "...");
//
// In GFLAGS style, the corresponding flags are named
//   flying_pigs
//   noflying_pigs
//
// In GNU style, the corresponding flags are named
//   flying-pigs
//   no-flying-pigs
//
// You may not pass arguments to boolean flags, so you must use the
// "no" / "no-" prefix to set them to false; "--flying_pigs false"
// and "--flying_pigs=false" are not allowed, to prevent ambiguity.
boost::program_options::options_description getGFlags(
    ProgramOptionsStyle style = ProgramOptionsStyle::GNU);
#endif

/// Result of parsing a nested command line.
///
/// Nested command lines have the form:
///
/// program [--common_options...] command [--command_options...] args
///
/// The result has "command" set to the first positional argument, if any,
/// and "rest" set to the remaining options and arguments. Note that any
/// unrecognized flags must appear after the command name.
///
/// You may pass "rest" to parseNestedCommandLine again, etc.
struct NestedCommandLineParseResult {
  /// Constructs an empty parse result.
  NestedCommandLineParseResult() {}

  /// The options recognized before the command name.
  boost::program_options::parsed_options options{nullptr};

  /// The command name, i.e. the first positional argument, if any.
  Optional<std::string> command;
  /// The remaining options and arguments after the command name.
  std::vector<std::string> rest;
};

/// Parses a nested command line from a C-style argument array.
/// \param argc The number of arguments.
/// \param argv The argument array.
/// \param desc The common options recognized before the command name.
/// \param style The option parsing style to use.
/// \returns The parsed command name and remaining arguments.
NestedCommandLineParseResult parseNestedCommandLine(
    int argc,
    const char* const argv[],
    const boost::program_options::options_description& desc,
    boost::program_options::command_line_style::style_t style =
        boost::program_options::command_line_style::default_style);

/// Parses a nested command line from a list of arguments.
/// \param cmdline The command-line arguments to parse.
/// \param desc The common options recognized before the command name.
/// \param style The option parsing style to use.
/// \returns The parsed command name and remaining arguments.
NestedCommandLineParseResult parseNestedCommandLine(
    const std::vector<std::string>& cmdline,
    const boost::program_options::options_description& desc,
    boost::program_options::command_line_style::style_t style =
        boost::program_options::command_line_style::default_style);

} // namespace folly
