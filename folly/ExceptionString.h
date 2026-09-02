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

#include <exception>

#include <folly/FBString.h>

namespace folly {

/**
 * Debug string for an exception: include type and what(), if
 * defined.
 *
 * \param e The exception to describe.
 * \returns A debug string containing the exception's type and message.
 */
fbstring exceptionStr(std::exception const& e);

/// Debug string for an exception referenced by a std::exception_ptr.
///
/// \param ep The exception pointer to describe.
/// \returns A debug string describing the referenced exception.
fbstring exceptionStr(std::exception_ptr const& ep);

} // namespace folly
