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

/// Root namespace for the Folly library.
namespace folly {
/// Portability shims for platform-specific system interfaces.
namespace portability {
/// Returns the directory portion of a path, without modifying the input.
///
/// \param path The path to take the directory component of.
/// \returns A pointer to the directory portion of `path`.
char* internal_dirname(char* path);
} // namespace portability
} // namespace folly

#ifndef _WIN32
#include <libgen.h>
#else
extern "C" char* dirname(char* path);
#endif
