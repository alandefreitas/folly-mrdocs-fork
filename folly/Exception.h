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

#include <errno.h>

#include <cstdio>
#include <stdexcept>
#include <system_error>

#include <folly/Conv.h>
#include <folly/FBString.h>
#include <folly/Likely.h>
#include <folly/Portability.h>
#include <folly/portability/SysTypes.h>

namespace folly {

// Various helpers to throw appropriate std::system_error exceptions from C
// library errors (returned in errno, as positive return values (many POSIX
// functions), or as negative return values (Linux syscalls))
//
// The *Explicit functions take an explicit value for errno.

// On linux and similar platforms the value of `errno` is a mixture of
// POSIX-`errno`-domain error codes and per-OS extended error codes. So the
// most appropriate category to use is `system_category`.
//
// On Windows `system_category` means codes that can be returned by Win32 API
// `GetLastError` and codes from the `errno`-domain must be reported as
// `generic_category`.
/// Returns the most appropriate error category for `errno`-domain codes on the current platform.
///
/// \returns The error category for `errno`-domain codes on this platform.
inline const std::error_category& errorCategoryForErrnoDomain() noexcept {
  if (kIsWindows) {
    return std::generic_category();
  }
  return std::system_category();
}

/// Builds a `std::system_error` from an explicit error number and message.
///
/// @param err The error number to report.
/// @param msg The description of the failure.
/// @return A `std::system_error` for the given error number.
inline std::system_error makeSystemErrorExplicit(int err, const char* msg) {
  return std::system_error(err, errorCategoryForErrnoDomain(), msg);
}

/// Builds a `std::system_error` from an explicit error number and message components.
///
/// @param err The error number to report.
/// @param args Components concatenated into the failure message.
/// @return A `std::system_error` for the given error number.
template <class... Args>
std::system_error makeSystemErrorExplicit(int err, Args&&... args) {
  return makeSystemErrorExplicit(
      err, to<fbstring>(std::forward<Args>(args)...).c_str());
}

/// Builds a `std::system_error` from the current `errno` and a message.
///
/// @param msg The description of the failure.
/// @return A `std::system_error` for the current `errno`.
inline std::system_error makeSystemError(const char* msg) {
  return makeSystemErrorExplicit(errno, msg);
}

/// Builds a `std::system_error` from the current `errno` and message components.
///
/// @param args Components concatenated into the failure message.
/// @return A `std::system_error` for the current `errno`.
template <class... Args>
std::system_error makeSystemError(Args&&... args) {
  return makeSystemErrorExplicit(errno, std::forward<Args>(args)...);
}

// Helper to throw std::system_error
/// Throws a `std::system_error` built from an explicit error number and message.
///
/// @param err The error number to report.
/// @param msg The description of the failure.
[[noreturn]] inline void throwSystemErrorExplicit(int err, const char* msg) {
  throw_exception(makeSystemErrorExplicit(err, msg));
}

/// Throws a `std::system_error` built from an explicit error number and message components.
///
/// @param err The error number to report.
/// @param args Components concatenated into the failure message.
template <class... Args>
[[noreturn]] void throwSystemErrorExplicit(int err, Args&&... args) {
  throw_exception(makeSystemErrorExplicit(err, std::forward<Args>(args)...));
}

/// Throws a `std::system_error` from the current `errno` and components of a string.
///
/// @param args Components concatenated into the failure message.
template <class... Args>
[[noreturn]] void throwSystemError(Args&&... args) {
  throwSystemErrorExplicit(errno, std::forward<Args>(args)...);
}

/// Checks a POSIX return code (0 on success, error number on error) and throws on error.
///
/// @param err The POSIX return code to check.
/// @param args Components concatenated into the failure message.
template <class... Args>
void checkPosixError(int err, Args&&... args) {
  if (FOLLY_UNLIKELY(err != 0)) {
    throwSystemErrorExplicit(err, std::forward<Args>(args)...);
  }
}

/// Checks a Linux kernel-style return code (>= 0 on success, negative error number on error) and throws on error.
///
/// @param ret The kernel-style return code to check.
/// @param args Components concatenated into the failure message.
template <class... Args>
void checkKernelError(ssize_t ret, Args&&... args) {
  if (FOLLY_UNLIKELY(ret < 0)) {
    throwSystemErrorExplicit(int(-ret), std::forward<Args>(args)...);
  }
}

/// Checks a traditional Unix return code (-1 and sets `errno` on error) and throws on error.
///
/// @param ret The Unix return code to check.
/// @param args Components concatenated into the failure message.
template <class... Args>
void checkUnixError(ssize_t ret, Args&&... args) {
  if (FOLLY_UNLIKELY(ret == -1)) {
    throwSystemError(std::forward<Args>(args)...);
  }
}

/// Checks a traditional Unix return code (-1 on error) and throws using an explicit saved `errno`.
///
/// @param ret The Unix return code to check.
/// @param savedErrno The previously saved error number to report.
/// @param args Components concatenated into the failure message.
template <class... Args>
void checkUnixErrorExplicit(ssize_t ret, int savedErrno, Args&&... args) {
  if (FOLLY_UNLIKELY(ret == -1)) {
    throwSystemErrorExplicit(savedErrno, std::forward<Args>(args)...);
  }
}

/// Checks the return value from a fopen-style function (non-null `FILE*` on success, null on error) and throws on error.
///
/// Works with `fopen`, `fdopen`, `freopen`, `tmpfile`, and similar functions that set `errno`.
///
/// @param fp The `FILE*` returned by the fopen-style call.
/// @param args Components concatenated into the failure message.
template <class... Args>
void checkFopenError(FILE* fp, Args&&... args) {
  if (FOLLY_UNLIKELY(!fp)) {
    throwSystemError(std::forward<Args>(args)...);
  }
}

/// Checks the return value from a fopen-style function and throws using an explicit saved `errno`.
///
/// @param fp The `FILE*` returned by the fopen-style call.
/// @param savedErrno The previously saved error number to report.
/// @param args Components concatenated into the failure message.
template <class... Args>
void checkFopenErrorExplicit(FILE* fp, int savedErrno, Args&&... args) {
  if (FOLLY_UNLIKELY(!fp)) {
    throwSystemErrorExplicit(savedErrno, std::forward<Args>(args)...);
  }
}

/**
 * If cond is not true, raise an exception of type E.  E must have a ctor that
 * works with const char* (a description of the failure).
 *
 * @param cond The condition that must hold; an exception is thrown when it is false.
 * @param E The exception type to throw, constructible from a `const char*` description.
 */
#define CHECK_THROW(cond, E)                       \
  do {                                             \
    if (!(cond)) {                                 \
      folly::throw_exception<E>(                   \
          "Check failed: " #cond ", in " __FILE__  \
          ":" FOLLY_PP_STRINGIZE_MACRO(__LINE__)); \
    }                                              \
  } while (0)

} // namespace folly
