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

#include <filesystem>

/// Portable filesystem imports selecting the available `<filesystem>` backend.
namespace folly::fs {

/// Identifies which standard filesystem implementation is in use.
enum class which_enum {
  std = 1, ///< The C++17 `std::filesystem` implementation.
  std_experimental = 2, ///< The `std::experimental::filesystem` implementation.
};

/// Alias for the selected standard filesystem namespace.
namespace std_fs = std::filesystem;
/// The filesystem implementation selected at compile time.
inline constexpr which_enum which = which_enum::std;

//  imports

/// Imported `std::filesystem::absolute`.
using std_fs::absolute;
/// Imported `std::filesystem::canonical`.
using std_fs::canonical;
/// Imported `std::filesystem::copy`.
using std_fs::copy;
/// Imported `std::filesystem::copy_file`.
using std_fs::copy_file;
/// Imported `std::filesystem::copy_options`.
using std_fs::copy_options;
/// Imported `std::filesystem::copy_symlink`.
using std_fs::copy_symlink;
/// Imported `std::filesystem::create_directories`.
using std_fs::create_directories;
/// Imported `std::filesystem::create_directory`.
using std_fs::create_directory;
/// Imported `std::filesystem::create_directory_symlink`.
using std_fs::create_directory_symlink;
/// Imported `std::filesystem::create_hard_link`.
using std_fs::create_hard_link;
/// Imported `std::filesystem::create_symlink`.
using std_fs::create_symlink;
/// Imported `std::filesystem::current_path`.
using std_fs::current_path;
/// Imported `std::filesystem::directory_entry`.
using std_fs::directory_entry;
/// Imported `std::filesystem::directory_iterator`.
using std_fs::directory_iterator;
/// Imported `std::filesystem::directory_options`.
using std_fs::directory_options;
/// Imported `std::filesystem::equivalent`.
using std_fs::equivalent;
/// Imported `std::filesystem::exists`.
using std_fs::exists;
/// Imported `std::filesystem::file_size`.
using std_fs::file_size;
/// Imported `std::filesystem::file_status`.
using std_fs::file_status;
/// Imported `std::filesystem::file_time_type`.
using std_fs::file_time_type;
/// Imported `std::filesystem::file_type`.
using std_fs::file_type;
/// Imported `std::filesystem::filesystem_error`.
using std_fs::filesystem_error;
/// Imported `std::filesystem::hard_link_count`.
using std_fs::hard_link_count;
/// Imported `std::filesystem::is_block_file`.
using std_fs::is_block_file;
/// Imported `std::filesystem::is_character_file`.
using std_fs::is_character_file;
/// Imported `std::filesystem::is_directory`.
using std_fs::is_directory;
/// Imported `std::filesystem::is_empty`.
using std_fs::is_empty;
/// Imported `std::filesystem::is_fifo`.
using std_fs::is_fifo;
/// Imported `std::filesystem::is_other`.
using std_fs::is_other;
/// Imported `std::filesystem::is_regular_file`.
using std_fs::is_regular_file;
/// Imported `std::filesystem::is_socket`.
using std_fs::is_socket;
/// Imported `std::filesystem::is_symlink`.
using std_fs::is_symlink;
/// Imported `std::filesystem::last_write_time`.
using std_fs::last_write_time;
/// Imported `std::filesystem::path`.
using std_fs::path;
/// Imported `std::filesystem::permissions`.
using std_fs::permissions;
/// Imported `std::filesystem::perms`.
using std_fs::perms;
/// Imported `std::filesystem::read_symlink`.
using std_fs::read_symlink;
/// Imported `std::filesystem::recursive_directory_iterator`.
using std_fs::recursive_directory_iterator;
/// Imported `std::filesystem::remove`.
using std_fs::remove;
/// Imported `std::filesystem::remove_all`.
using std_fs::remove_all;
/// Imported `std::filesystem::rename`.
using std_fs::rename;
/// Imported `std::filesystem::resize_file`.
using std_fs::resize_file;
/// Imported `std::filesystem::space`.
using std_fs::space;
/// Imported `std::filesystem::space_info`.
using std_fs::space_info;
/// Imported `std::filesystem::status`.
using std_fs::status;
/// Imported `std::filesystem::status_known`.
using std_fs::status_known;
/// Imported `std::filesystem::symlink_status`.
using std_fs::symlink_status;
/// Imported `std::filesystem::temp_directory_path`.
using std_fs::temp_directory_path;
/// Imported `std::filesystem::u8path`.
using std_fs::u8path;

/// Imported `std::filesystem::perm_options`.
using std_fs::perm_options;
/// Imported `std::filesystem::proximate`.
using std_fs::proximate;
/// Imported `std::filesystem::relative`.
using std_fs::relative;
/// Imported `std::filesystem::weakly_canonical`.
using std_fs::weakly_canonical;

/// Function object that returns the lexically normal form of a path.
struct lexically_normal_fn {
  /// Returns the lexically normal form of the given path.
  ///
  /// \param p The path to normalize.
  /// \returns The lexically normal form of `p`.
  path operator()(path const& p) const;
};
/// Callable that returns the lexically normal form of a path.
inline constexpr lexically_normal_fn lexically_normal;

} // namespace folly::fs
