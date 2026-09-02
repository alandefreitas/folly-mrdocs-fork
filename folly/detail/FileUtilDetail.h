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

#include <cerrno>
#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>

#include <folly/portability/SysTypes.h>

//  This header is intended to be extremely lightweight. In particular, the
//  parallel private functions for wrapping vector file-io are in a separate
//  header.

/// The Folly library.
namespace folly {
/// Implementation details.
namespace fileutil_detail {

//  The following wrapX() funcions are private functions for wrapping file-io
//  against interrupt and partial op completions.

/// Wrap call to f(args) in loop to retry on EINTR.
///
/// \param f The function to invoke.
/// \param args The arguments forwarded to the function.
/// \returns The result of the last invocation of the function.
template <class F, class... Args, class R = std::invoke_result_t<F&, Args&...>>
R wrapNoInt(F f, Args... args) {
  R r;
  do {
    r = f(args...);
  } while (r == -1 && errno == EINTR);
  return r;
}

/// No-op overload used when no offset is tracked.
///
/// \param n The number of bytes transferred (ignored).
inline void incr(ssize_t n) {}
/// Advance the offset by the number of bytes transferred.
///
/// \param n The number of bytes transferred.
/// \param offset The offset to advance.
template <typename Offset>
inline void incr(ssize_t n, Offset& offset) {
  offset += static_cast<Offset>(n);
}

/// Wrap call to read/pread/write/pwrite(fd, buf, count, offset?) to retry on
/// incomplete reads / writes.  The variadic argument magic is there to support
/// an additional argument (offset) for pread / pwrite; see the incr() functions
/// above which do nothing if the offset is not present and increment it if it
/// is.
///
/// \param f The read/write function to invoke.
/// \param fd The file descriptor to operate on.
/// \param buf The buffer to read into or write from.
/// \param count The number of bytes to transfer.
/// \param offset Optional file offset for positional variants.
/// \returns The total number of bytes transferred, or -1 on error.
template <class F, class... Offset>
ssize_t wrapFull(F f, int fd, void* buf, size_t count, Offset... offset) {
  char* b = static_cast<char*>(buf);
  ssize_t totalBytes = 0;
  ssize_t r;
  do {
    r = f(fd, b, count, offset...);
    if (r == -1) {
      if (errno == EINTR) {
        continue;
      }
      return r;
    }

    totalBytes += r;
    b += r;
    count -= r;
    incr(r, offset...);
  } while (r != 0 && count); // 0 means EOF

  return totalBytes;
}

/// Returns a string compatible for mkstemp().
///
/// \param filePath The target file path.
/// \param temporaryDirectory The directory for the temporary file.
/// \returns A path template string suitable for mkstemp().
std::string getTemporaryFilePathString(
    const std::string& filePath, const std::string& temporaryDirectory);

} // namespace fileutil_detail
} // namespace folly
