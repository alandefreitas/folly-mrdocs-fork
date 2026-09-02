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

#include <string_view>

#include <folly/settings/Settings.h>
#include <folly/settings/SettingsAccessorProxy.h>

namespace folly::settings {

/// Outcome of parsing command line arguments.
enum ArgParsingResult {
  OK, ///< Arguments were parsed successfully.
  HELP, ///< A help flag was encountered.
  ERROR, ///< An error occurred during parsing.
};

/// Parses command line arguments into folly::settings.
class CommandLineParser {
 public:
  /// Constructs a parser over the given argument vector and settings proxy.
  ///
  /// \param argc The number of command line arguments.
  /// \param argv The command line argument vector.
  /// \param flags_info The settings accessor proxy to apply parsed flags to.
  CommandLineParser(int& argc, char**& argv, SettingsAccessorProxy& flags_info);

  /// Move constructor.
  ///
  /// \param other The parser to move from.
  CommandLineParser(CommandLineParser&& other) noexcept;
  /// Move assignment operator.
  ///
  /// \param other The parser to move from.
  /// \returns A reference to this parser.
  CommandLineParser& operator=(CommandLineParser&& other) noexcept;

  /// Deleted copy constructor.
  ///
  /// \param other The parser to copy from.
  CommandLineParser(const CommandLineParser& other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param other The parser to copy from.
  /// \returns A reference to this parser.
  CommandLineParser& operator=(const CommandLineParser& other) = delete;

  /// Destroys the parser.
  ~CommandLineParser();

  /// Parses the command line arguments into the associated settings.
  ///
  /// \returns The result of argument parsing.
  ArgParsingResult parse();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

/**
 * Main function to parse arguments folly::settings. This function parses
 * command line arguments into folly::settings. It moves all positional, --help,
 * not recognized flags and their values to the end of the argv array. It stops
 * parsing arguments after '--' is encoutered.
 *
 * @param argc number of arguments in command line
 * @param argv vector of command line arguments
 * @param snapshot folly::settings snapshot.
 * @param project default project name for CLI parsing (default: "")
 * @param aliases map of aliases for registered settings
 * @return true if help flag was encountered
 */
ArgParsingResult parseCommandLineArguments(
    int& argc,
    char**& argv,
    std::string_view project = "",
    Snapshot* snapshot = nullptr,
    const SettingsAccessorProxy::SettingAliases& aliases = {});

/**
 * Outputs help message into stderr. May terminate program if error was
 * encoutered during arguments parsing or exit is requested by caller.
 *
 * @param app inforgation about app printed before main help message
 * @param exit_on_help flag to terminate process after printing help
 */
void printHelpIfNeeded(std::string_view app, bool exit_on_help = false);

} // namespace folly::settings
