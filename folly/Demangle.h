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

#include <folly/FBString.h>
#include <folly/portability/Config.h>

namespace folly {

/// Maximum symbol size that demangling will attempt, or 0 for no limit.
inline constexpr size_t demangle_max_symbol_size =
#if defined(FOLLY_DEMANGLE_MAX_SYMBOL_SIZE)
    FOLLY_DEMANGLE_MAX_SYMBOL_SIZE;
#else
    0;
#endif

/// Report whether demangling was built with cxxabi support.
///
/// \returns `true` when the cxxabi demangler is available.
bool demangle_build_has_cxxabi() noexcept;

/// Report whether demangling was built with libiberty support.
///
/// \returns `true` when the libiberty demangler is available.
bool demangle_build_has_liberty() noexcept;

/**
 * Return the demangled (prettified) version of a C++ type.
 *
 * This function tries to produce a human-readable type, but the type name will
 * be returned unchanged in case of error or if demangling isn't supported on
 * your system.
 *
 * Use for debugging -- do not rely on demangle() returning anything useful.
 *
 * This function may allocate memory (and therefore throw std::bad_alloc).
 *
 * \param name The mangled type name to demangle.
 * \returns The demangled type name, or `name` unchanged on error.
 */
fbstring demangle(const char* name);

/// Return the demangled (prettified) version of a C++ type.
///
/// \param type The type to demangle, as reported by `typeid`.
/// \returns The demangled type name, or the raw name on error.
inline fbstring demangle(const std::type_info& type) {
  return demangle(type.name());
}

/**
 * Return the demangled (prettified) version of a C++ type in a user-provided
 * buffer.
 *
 * The semantics are the same as for snprintf or strlcpy: bufSize is the size
 * of the buffer, the string is always null-terminated, and the return value is
 * the number of characters (not including the null terminator) that would have
 * been written if the buffer was big enough. (So a return value >= bufSize
 * indicates that the output was truncated)
 *
 * This function does not allocate memory and is async-signal-safe.
 *
 * Note that the underlying function for the fbstring-returning demangle is
 * somewhat standard (abi::__cxa_demangle, which uses malloc), the underlying
 * function for this version is less so (cplus_demangle_v3_callback from
 * libiberty), so it is possible for the fbstring version to work, while this
 * version returns the original, mangled name.
 *
 * \param name The mangled type name to demangle.
 * \param out The buffer that receives the null-terminated result.
 * \param outSize The size of `out`, in bytes.
 * \returns The number of characters that would have been written, excluding the
 *     null terminator; a value >= `outSize` means the output was truncated.
 */
size_t demangle(const char* name, char* out, size_t outSize);

/// Return the demangled version of a C++ type into a user-provided buffer.
///
/// \param type The type to demangle, as reported by `typeid`.
/// \param buf The buffer that receives the null-terminated result.
/// \param bufSize The size of `buf`, in bytes.
/// \returns The number of characters that would have been written, excluding
///     the null terminator; a value >= `bufSize` means the output was
///     truncated.
inline size_t demangle(const std::type_info& type, char* buf, size_t bufSize) {
  return demangle(type.name(), buf, bufSize);
}

} // namespace folly
