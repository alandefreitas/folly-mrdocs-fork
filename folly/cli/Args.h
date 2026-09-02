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
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <folly/container/span.h>
#include <folly/lang/cstring_view.h>

/// Folly, Facebook's open-source C++ library.
namespace folly {

/// Converts a list of strings into a null-pointer-terminated array of C strings
/// borrowing the storage of the input strings.
/// \param args The strings to convert.
/// \returns A vector of C string pointers borrowing the input storage.
std::vector<char const*> cli_args_strings_to_c_strings(
    std::span<std::string const> args);

/// Callback interface receiving events while expanding args-files.
class cli_apply_args_files_receiver {
 public:
  /// Result telling the expander how to continue after a plain term.
  enum class control {
    stop, ///< stop processing at this level, and recurse up
    pass, ///< continue processing at this level
  };

  /// Result telling the expander what to do with an args-file argument.
  enum class found {
    stop, ///< stop processing at this level, and recurse up
    skip, ///< skip this arg
    dive, ///< expand this arg like an args-file
  };

  /// Result telling the expander how to handle a detected file cycle.
  enum class cycle {
    stop, ///< stop processing at this level, and recurse up
    skip, ///< skip this arg and continue processing
  };

  /// Reason an argument term failed to parse.
  enum class term_error {
    success, ///< the term parsed successfully
    unclosed_single_quote, ///< a single quote was never closed
    unclosed_double_quote, ///< a double quote was never closed
    unrecognized_escape_sequence, ///< an escape sequence was not recognized
  };

  /// A human-readable line and column position within an args-file.
  struct human_location {
    /// 1-base line number of this entry in the containing args-file (or zero)
    size_t line = 0;

    /// 1-base column number of this entry in the containing args-file (or zero)
    size_t col = 0;

    /// Compares two positions for equality.
    /// \param lhs The left-hand position.
    /// \param rhs The right-hand position.
    /// \returns True if both positions are equal.
    friend bool operator==(
        human_location const& lhs, human_location const& rhs) noexcept {
      return lhs.line == rhs.line && lhs.col == rhs.col;
    }
  };

  /// The full location of an entry within the arg-list or an args-file.
  struct location {
    /// 0-base logical index of this entry in the arg-list or args-file
    size_t idx = 0;

    /// 0-base byte offset of this entry in the containing args-file (or zero)
    /// if a beginning loc, offset of the beginning char
    /// if an end loc, offset of the char after the end
    size_t off = 0;

    /// length of this entry in the containing args-file (or zero)
    size_t len = 0;

    /// line and col of the beginning char
    human_location b;

    /// line and col of the ending char (not past the end!)
    human_location e;

    /// Compares two locations for equality.
    /// \param lhs The left-hand location.
    /// \param rhs The right-hand location.
    /// \returns True if both locations are equal.
    friend bool operator==(location const& lhs, location const& rhs) noexcept {
      return lhs.idx == rhs.idx && lhs.off == rhs.off && lhs.len == rhs.len &&
          lhs.b == rhs.b && lhs.e == rhs.e;
    }
  };

  /// Destroys the receiver.
  virtual ~cli_apply_args_files_receiver() = default;

  /// Called when an argument is just a string.
  /// Is passed ownership of the string.
  /// \param arg The argument string.
  /// \param loc The location of the argument.
  /// \returns Whether to keep processing at this level.
  virtual control on_term(std::string arg, location loc) = 0;

  /// Called when an argument fails to parse (e.g., unclosed quotes).
  /// term_loc is the location of the term containing the error.
  /// error_loc is the location where the error begins within that term.
  /// Parsing of the current file stops, but parent files continue.
  /// \param error The reason the term failed to parse.
  /// \param term_loc The location of the term containing the error.
  /// \param error_loc The location where the error begins within the term.
  virtual void on_term_error(
      term_error error, location term_loc, location error_loc) = 0;

  /// Called when an argument is an args-file. Returns whether to expand it.
  /// \param file The args-file name.
  /// \param loc The location of the argument.
  /// \returns Whether to expand, skip, or stop at this args-file.
  virtual found on_file_found(cstring_view file, location loc) = 0;

  /// Called when an argument is an args-file, but the file could not be read.
  /// Is passed ownership of the filename.
  /// \param file The args-file name.
  /// \param loc The location of the argument.
  /// \param err The error encountered while reading the file.
  /// \returns Whether to keep processing at this level.
  virtual control on_file_error(
      std::string file, location loc, std::error_code err) = 0;

  /// Called when an argument is an args-file, the file was read successfully,
  /// and control recurses into the args-file.
  /// Is passed ownership of the filename.
  /// \param file The args-file name.
  /// \param loc The location of the argument.
  virtual void on_file_enter(std::string file, location loc) = 0;

  /// Called when control recurses out of an args-file.
  /// Calls to on_file_enter and on_file_leave are balanced in pairs.
  virtual void on_file_leave() = 0;

  /// Called when an argument is an args-file that would create a cycle
  /// (directly or indirectly). The file parameter is the original
  /// filename from the argument, and canonical_path is the resolved path that
  /// was detected as part of a cycle.
  /// \param file The original args-file name from the argument.
  /// \param loc The location of the argument.
  /// \param canonical_path The resolved path detected as part of a cycle.
  /// \returns Whether to skip the file or stop processing.
  virtual cycle on_file_cycle(
      std::string file, location loc, std::filesystem::path canonical_path) = 0;
};

/// Exception thrown by the simple cli_apply_args_files overload.
class cli_apply_args_files_error : public std::runtime_error {
 public:
  /// Inherits the std::runtime_error constructors.
  using std::runtime_error::runtime_error;
};

/// Options controlling args-file expansion.
struct cli_apply_args_files_options {
  /// The maximum args-file recursion depth before expansion fails.
  size_t max_depth = 64;
};

/// Error codes reported by args-file expansion.
enum class cli_apply_args_files_errc : int {
  max_depth_exceeded = 1, ///< the maximum args-file recursion depth was exceeded
};

/// Builds a std::error_code from an args-file expansion error code.
/// \param errc The error code to wrap.
/// \returns A std::error_code carrying the given error.
std::error_code make_error_code(cli_apply_args_files_errc errc);

} // namespace folly

/// The C++ standard library.
namespace std {
/// Marks folly::cli_apply_args_files_errc as an error-code enum.
template <>
struct is_error_code_enum<folly::cli_apply_args_files_errc> : true_type {};
} // namespace std

namespace folly {

/// Applies args-file expansion to a list of arguments.
/// Uses the receiver interface for full control over processing.
/// \param receiver The receiver notified of each expansion event.
/// \param current_dir The directory args-file paths are resolved against.
/// \param args The arguments to expand.
/// \param options Options controlling the expansion.
void cli_apply_args_files(
    cli_apply_args_files_receiver& receiver,
    std::filesystem::path const& current_dir,
    std::span<std::string const> args,
    cli_apply_args_files_options const& options = {});

/// Applies args-file expansion to a list of arguments.
/// Returns the expanded list of arguments.
/// Throws cli_apply_args_files_error on any error (file not found, parse
/// error).
/// \param current_dir The directory args-file paths are resolved against.
/// \param args The arguments to expand.
/// \returns The expanded list of arguments.
std::vector<std::string> cli_apply_args_files(
    std::filesystem::path const& current_dir,
    std::span<std::string const> args);

/// Applies args-file expansion to a C-style argument array.
/// \param current_dir The directory args-file paths are resolved against.
/// \param argc The number of arguments.
/// \param argv The argument array.
/// \returns The expanded list of arguments.
std::vector<std::string> cli_apply_args_files(
    std::filesystem::path const& current_dir,
    int argc,
    char const* const* argv);

/// Applies args-file expansion to a mutable C-style argument array.
/// \param current_dir The directory args-file paths are resolved against.
/// \param argc The number of arguments.
/// \param argv The argument array.
/// \returns The expanded list of arguments.
std::vector<std::string> cli_apply_args_files(
    std::filesystem::path const& current_dir, int argc, char* const* argv);

/// Applies args-file expansion using the current working directory.
/// \param argc The number of arguments.
/// \param argv The argument array.
/// \returns The expanded list of arguments.
std::vector<std::string> cli_apply_args_files(
    int argc, char const* const* argv);

/// Applies args-file expansion using the current working directory.
/// \param argc The number of arguments.
/// \param argv The argument array.
/// \returns The expanded list of arguments.
std::vector<std::string> cli_apply_args_files(int argc, char* const* argv);

} // namespace folly
