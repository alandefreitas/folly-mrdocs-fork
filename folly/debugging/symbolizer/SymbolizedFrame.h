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
#include <memory>
#include <ostream>
#include <string>

#include <folly/Range.h>

namespace folly {
namespace symbolizer {

/// A memory-mapped ELF object file.
class ElfFile;

/**
 * Represent a file path as a collection of three parts (base directory,
 * subdirectory, and file).
 */
class Path {
 public:
  /// Construct an empty path.
  Path() = default;

  /// Construct a path from its base directory, subdirectory, and file parts.
  ///
  /// \param baseDir The base directory part.
  /// \param subDir The subdirectory part.
  /// \param file The file name part.
  Path(
      folly::StringPiece baseDir,
      folly::StringPiece subDir,
      folly::StringPiece file);

  /// Get the base directory part of the path.
  ///
  /// \returns The base directory part of the path.
  folly::StringPiece baseDir() const { return baseDir_; }
  /// Get the subdirectory part of the path.
  ///
  /// \returns The subdirectory part of the path.
  folly::StringPiece subDir() const { return subDir_; }
  /// Get the file name part of the path.
  ///
  /// \returns The file name part of the path.
  folly::StringPiece file() const { return file_; }

  /// Get the total length of the path.
  ///
  /// \returns The total length of the path, in bytes.
  size_t size() const;

  /**
   * Copy the Path to a buffer of size bufSize.
   *
   * toBuffer behaves like snprintf: It will always null-terminate the
   * buffer (so it will copy at most bufSize-1 bytes), and it will return
   * the number of bytes that would have been written if there had been
   * enough room, so, if toBuffer returns a value >= bufSize, the output
   * was truncated.
   *
   * \param buf The destination buffer.
   * \param bufSize The size of buf, in bytes.
   * \returns The number of bytes that would have been written; a value >=
   * bufSize means the output was truncated.
   */
  size_t toBuffer(char* buf, size_t bufSize) const;

  /// Append the path to dest.
  ///
  /// \param dest The string to append the path to.
  void toString(std::string& dest) const;
  /// Render the path as a string.
  ///
  /// \returns The path as a newly constructed string.
  std::string toString() const {
    std::string s;
    toString(s);
    return s;
  }

 private:
  folly::StringPiece baseDir_;
  folly::StringPiece subDir_;
  folly::StringPiece file_;
};

/// Write a Path to an output stream.
///
/// \param out The stream to write to.
/// \param path The path to write.
/// \returns The output stream out.
inline std::ostream& operator<<(std::ostream& out, const Path& path) {
  return out << path.toString();
}

/// How aggressively to resolve source location information.
enum class LocationInfoMode {
  DISABLED, ///< Don't resolve location info.
  FAST, ///< Perform CU lookup using .debug_aranges (might be incomplete).
  FULL, ///< Scan all CU in .debug_info (slow!) on .debug_aranges lookup failure.
  /// Scan .debug_info (super slower, use with caution) for inline functions in
  /// addition to FULL.
  FULL_WITH_INLINE,
  NUM_MODES, ///< Not a mode. Sizes anything held per mode; keep it last.
};

/**
 * Contains location info like file name, line number, etc.
 */
struct LocationInfo {
  /// Whether file and line information is available.
  bool hasFileAndLine = false;
  /// Whether a main compilation-unit file is available.
  bool hasMainFile = false;
  /// The main compilation-unit file.
  Path mainFile;
  /// The source file the address maps to.
  Path file;
  /// The source line number.
  uint64_t line = 0;
};

/**
 * Frame information: symbol name and location.
 */
struct SymbolizedFrame {
  /// Whether the address was successfully symbolized.
  bool found = false;
  /// The instruction address this frame was symbolized from.
  uintptr_t addr = 0;
  /// Mangled symbol name. Use `folly::demangle()` to demangle it.
  const char* name = nullptr;
  /// Source location (file and line) for the frame.
  LocationInfo location;
  /// The ELF file the frame was resolved from.
  std::shared_ptr<ElfFile> file;

  /// Reset the frame to its default, unresolved state.
  void clear() { *this = SymbolizedFrame(); }
};

/// A fixed-capacity array of captured addresses and their symbolized frames.
template <size_t N>
struct FrameArray {
  /// Construct an empty frame array.
  FrameArray() = default;

  /// Number of valid frames.
  size_t frameCount = 0;
  /// The captured instruction addresses.
  uintptr_t addresses[N];
  /// The symbolized frames corresponding to addresses.
  SymbolizedFrame frames[N];
};

} // namespace symbolizer
} // namespace folly
