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

/// Linux openat2 syscall options structure.
struct open_how;

/// Facebook Folly library namespace.
namespace folly {

/// linux_syscall_openat2
///
/// Invokes the openat2 syscall. Returns the file descriptor on success, or
/// returns -1 and sets errno on failure.
///
/// \param dirfd Directory file descriptor relative to which pathname is
/// resolved.
/// \param pathname Path of the file to open.
/// \param how Pointer to the open_how structure describing how to open the file.
/// \returns The file descriptor on success, or -1 with errno set on failure.
long linux_syscall_openat2(
    int dirfd, char const* pathname, struct open_how const* how);

} // namespace folly
