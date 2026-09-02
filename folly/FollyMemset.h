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

#include <cstddef>

/// Facebook's Open-source Library.
namespace folly {

/// Set the first \p count bytes of the block pointed to by \p dest to \p ch.
///
/// \param dest Pointer to the block of memory to fill.
/// \param ch Fill byte, passed as an int and converted to unsigned char.
/// \param count Number of bytes to set.
/// \returns \p dest.
extern "C" void* __folly_memset(void* dest, int ch, std::size_t count);

} // namespace folly
