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

#ifdef _WIN32
#include <string>
#endif
#include <string_view>

/// The folly library.
namespace folly {
/// Extension points overridable by tests and integrations.
namespace ext {

/// Signature of a function that resolves a test resource path by name.
using test_find_resource_t = std::string(std::string_view);
/// Overridable hook used by tests to locate resource files.
extern test_find_resource_t* test_find_resource;

} // namespace ext
} // namespace folly
