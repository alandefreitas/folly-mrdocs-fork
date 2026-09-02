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

#include <map>
#include <string>

#include <folly/Range.h>
#include <folly/ScopeGuard.h>
#include <folly/io/FsUtil.h>

/// Folly, Facebook's open-source C++ library.
namespace folly {
/// Testing utilities.
namespace test {

/**
 * Temporary file.
 *
 * By default, the file is created in a system-specific location (the value
 * of the TMPDIR environment variable, or /tmp), but you can override that
 * with a different (non-empty) directory passed to the constructor.
 *
 * By default, the file is closed and deleted when the TemporaryFile object
 * is destroyed, but both these behaviors can be overridden with arguments
 * to the constructor.
 */
class TemporaryFile {
 public:
  /// Controls when the temporary file is unlinked.
  enum class Scope {
    PERMANENT, ///< Keep the file after the object is destroyed.
    UNLINK_IMMEDIATELY, ///< Unlink the file immediately after creating it.
    UNLINK_ON_DESTRUCTION, ///< Unlink the file when the object is destroyed.
  };
  /// Creates a temporary file.
  ///
  /// \param namePrefix Prefix for the generated file name.
  /// \param dir Directory in which to create the temporary file.
  /// \param scope When the file is unlinked.
  /// \param closeOnDestruction Whether to close the file when the object is destroyed.
  explicit TemporaryFile(
      StringPiece namePrefix = StringPiece(),
      fs::path dir = fs::path(),
      Scope scope = Scope::UNLINK_ON_DESTRUCTION,
      bool closeOnDestruction = true);
  /// Destroys the object, closing and unlinking the file per the chosen scope.
  ~TemporaryFile();

  /// Move-constructs from another instance.
  ///
  /// \param other The instance to move from.
  TemporaryFile(TemporaryFile&& other) noexcept { assign(other); }

  /// Move-assigns from another instance.
  ///
  /// \param other The instance to move from.
  /// \returns A reference to this instance.
  TemporaryFile& operator=(TemporaryFile&& other) {
    if (this != &other) {
      reset();
      assign(other);
    }
    return *this;
  }

  /// Closes the underlying file descriptor.
  void close();
  /// Returns the underlying file descriptor.
  ///
  /// \returns The underlying file descriptor.
  int fd() const { return fd_; }
  /// Returns the path of the temporary file.
  ///
  /// \returns The path of the temporary file.
  const fs::path& path() const;
  /// Closes and unlinks the file, releasing its resources.
  void reset();

 private:
  Scope scope_;
  bool closeOnDestruction_;
  int fd_;
  fs::path path_;

  void assign(TemporaryFile& other) {
    scope_ = other.scope_;
    closeOnDestruction_ = other.closeOnDestruction_;
    fd_ = std::exchange(other.fd_, -1);
    path_ = std::exchange(other.path_, fs::path());
  }
};

/**
 * Temporary directory.
 *
 * By default, the temporary directory is created in a system-specific
 * location (the value of the TMPDIR environment variable, or /tmp), but you
 * can override that with a non-empty directory passed to the constructor.
 *
 * By default, the directory is recursively deleted when the TemporaryDirectory
 * object is destroyed, but that can be overridden with an argument
 * to the constructor.
 */

class TemporaryDirectory {
 public:
  /// Controls whether the directory is deleted when the object is destroyed.
  enum class Scope {
    PERMANENT, ///< Keep the directory after the object is destroyed.
    DELETE_ON_DESTRUCTION, ///< Delete the directory when the object is destroyed.
  };
  /// Creates a temporary directory.
  ///
  /// \param namePrefix Prefix for the generated directory name.
  /// \param dir Parent directory in which to create the temporary directory.
  /// \param scope Whether the directory is deleted on destruction.
  explicit TemporaryDirectory(
      StringPiece namePrefix = StringPiece(),
      fs::path dir = fs::path(),
      Scope scope = Scope::DELETE_ON_DESTRUCTION);
  /// Destroys the object, deleting the directory unless the scope is PERMANENT.
  ~TemporaryDirectory();

  /// Move-constructs from another instance.
  ///
  /// \param other The instance to move from.
  TemporaryDirectory(TemporaryDirectory&& other) = default;
  /// Move-assigns from another instance.
  ///
  /// \param other The instance to move from.
  /// \returns A reference to this instance.
  TemporaryDirectory& operator=(TemporaryDirectory&& other) = default;

  /// Returns the path of the temporary directory.
  ///
  /// \returns The path of the temporary directory.
  const fs::path& path() const { return *path_; }

 private:
  Scope scope_;
  std::unique_ptr<fs::path> path_;
};

/**
 * Changes into a temporary directory, and deletes it with all its contents
 * upon destruction, also changing back to the original working directory.
 */
class ChangeToTempDir {
 public:
  /// Creates a temporary directory and changes into it.
  ChangeToTempDir();
  /// Changes back to the original directory and deletes the temporary one.
  ~ChangeToTempDir();

  /// Move-constructs from another instance.
  ///
  /// \param other The instance to move from.
  ChangeToTempDir(ChangeToTempDir&& other) = default;
  /// Move-assigns from another instance.
  ///
  /// \param other The instance to move from.
  /// \returns A reference to this instance.
  ChangeToTempDir& operator=(ChangeToTempDir&& other) = default;

  /// Returns the path of the temporary directory.
  ///
  /// \returns The path of the temporary directory.
  const fs::path& path() const { return dir_.path(); }

 private:
  TemporaryDirectory dir_;
  fs::path orig_;
};

namespace detail {
struct SavedState {
  void* previousThreadLocalHandler;
  int previousCrtReportMode;
};
SavedState disableInvalidParameters();
void enableInvalidParameters(SavedState state);
} // namespace detail

/// Invokes a function while suppressing the Windows CRT abort on invalid
/// parameters.
///
/// Ok, so fun fact: The CRT on windows will actually abort
/// on certain failed parameter validation checks in debug
/// mode rather than simply returning -1 as it does in release
/// mode. We can however, ensure consistent behavior by
/// registering our own thread-local invalid parameter handler
/// for the duration of the call, and just have that handler
/// immediately return. We also have to disable CRT asertion
/// alerts for the duration of the call, otherwise we get
/// the abort-retry-ignore window.
///
/// \param func The function to invoke with invalid-parameter aborts suppressed.
/// \returns The value returned by invoking func.
template <typename Func>
auto msvcSuppressAbortOnInvalidParams(Func func) -> decltype(func()) {
  auto savedState = detail::disableInvalidParameters();
  SCOPE_EXIT {
    detail::enableInvalidParameters(savedState);
  };
  return func();
}

/**
 * Easy PCRE regex matching. Note that pattern must match the ENTIRE target,
 * so use .* at the start and end of the pattern, as appropriate.  See
 * http://regex101.com/ for a PCRE simulator.
 *
 * \param pattern_stringpiece The PCRE pattern to match against.
 * \param target_stringpiece The target string that must match the pattern.
 */
#define EXPECT_PCRE_MATCH(pattern_stringpiece, target_stringpiece) \
  EXPECT_PRED2(                                                    \
      ::folly::test::detail::hasPCREPatternMatch,                  \
      pattern_stringpiece,                                         \
      target_stringpiece)
#define EXPECT_NO_PCRE_MATCH(pattern_stringpiece, target_stringpiece) \
  EXPECT_PRED2(                                                       \
      ::folly::test::detail::hasNoPCREPatternMatch,                   \
      pattern_stringpiece,                                            \
      target_stringpiece)

namespace detail {
bool hasPCREPatternMatch(StringPiece pattern, StringPiece target);
bool hasNoPCREPatternMatch(StringPiece pattern, StringPiece target);
} // namespace detail

/**
 * Use these patterns together with CaptureFD and EXPECT_PCRE_MATCH() to
 * test for the presence (or absence) of log lines at a particular level:
 *
 *   CaptureFD stderr(2);
 *   LOG(INFO) << "All is well";
 *   EXPECT_NO_PCRE_MATCH(glogErrOrWarnPattern(), stderr.readIncremental());
 *   LOG(ERROR) << "Uh-oh";
 *   EXPECT_PCRE_MATCH(glogErrorPattern(), stderr.readIncremental());
 *
 * \returns A PCRE pattern matching an error-level glog line.
 */
inline std::string glogErrorPattern() {
  return ".*(^|\n)E[0-9].*";
}
/// PCRE pattern matching a glog warning-level log line.
///
/// \returns A PCRE pattern matching a warning-level glog line.
inline std::string glogWarningPattern() {
  return ".*(^|\n)W[0-9].*";
}
/// PCRE pattern matching a glog error-level or warning-level log line.
///
/// \returns A PCRE pattern matching an error-level or warning-level glog line.
inline std::string glogErrOrWarnPattern() {
  return ".*(^|\n)[EW][0-9].*";
}

/**
 * Temporarily capture a file descriptor by redirecting it into a file.
 * You can consume its entire output thus far via read(), incrementally
 * via readIncremental(), or via callback using chunk_cob.
 * Great for testing logging (see also glog*Pattern()).
 */
class CaptureFD {
 private:
  struct NoOpChunkCob {
    void operator()(StringPiece /*unused*/) {}
  };

 public:
  /// Callback type invoked with each captured chunk of output.
  using ChunkCob = std::function<void(folly::StringPiece)>;

  /**
   * Begins capturing the given file descriptor by redirecting it into a file.
   *
   * chunk_cob is is guaranteed to consume all the captured output. It is
   * invoked on each readIncremental(), and also on FD release to capture
   * as-yet unread lines.  Chunks can be empty.
   *
   * \param fd The file descriptor to capture.
   * \param chunk_cob Callback invoked with each captured chunk of output.
   */
  explicit CaptureFD(int fd, ChunkCob chunk_cob = NoOpChunkCob());
  /// Stops capturing and restores the file descriptor.
  ~CaptureFD();

  /**
   * Restore the captured FD to its original state. It can be useful to do
   * this before the destructor so that you can read() the captured data and
   * log about it to the formerly captured stderr or stdout.
   */
  void release();

  /**
   * Reads the whole file into a string, but does not remove the redirect.
   *
   * \returns The whole captured file contents.
   */
  std::string read() const;

  /**
   * Read any bytes that were appended to the file since the last
   * readIncremental.  Great for testing line-by-line output.
   *
   * \returns The bytes appended to the file since the last readIncremental.
   */
  std::string readIncremental();

 private:
  ChunkCob chunkCob_;
  TemporaryFile file_;

  int fd_;
  int oldFDCopy_; // equal to fd_ after restore()

  off_t readOffset_; // for incremental reading
};

/// Finds the file path of a resource which was built alongside a test binary.
///
/// Care must be taken to set up the test and resource build rules in accordance
/// with this function.
///
/// \param resource The name of the resource to locate.
/// \returns The file path of the resource.
fs::path find_resource(std::string_view resource);

} // namespace test
} // namespace folly
