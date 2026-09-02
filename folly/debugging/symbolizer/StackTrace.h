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

#include <sys/types.h>
#include <cstdint>
#include <cstdlib>

#include <folly/CPortability.h>
#include <folly/portability/SysTypes.h>

/// The folly library.
namespace folly {
/// Stack trace capture and symbolization utilities.
namespace symbolizer {

/**
 * Get the current stack trace into addresses, which has room for at least
 * maxAddresses frames.
 *
 * NOT async-signal-safe, but fast.
 *
 * \param addresses Output buffer for the captured frame addresses.
 * \param maxAddresses Capacity of the addresses buffer, in frames.
 * \returns The number of frames written, or -1 on failure.
 */
ssize_t getStackTrace(uintptr_t* addresses, size_t maxAddresses);

/**
 * Get the current stack trace into addresses, which has room for at least
 * maxAddresses frames.
 *
 * Async-signal-safe, but likely slower.
 *
 * \param addresses Output buffer for the captured frame addresses.
 * \param maxAddresses Capacity of the addresses buffer, in frames.
 * \returns The number of frames written, or -1 on failure.
 */
ssize_t getStackTraceSafe(uintptr_t* addresses, size_t maxAddresses);

/**
 * Get the current stack trace into addresses, which has room for at least
 * maxAddresses frames.
 *
 * Heap allocates its context. Likely slower than getStackTrace but
 * avoids large stack allocations.
 *
 * \param addresses Output buffer for the captured frame addresses.
 * \param maxAddresses Capacity of the addresses buffer, in frames.
 * \returns The number of frames written, or -1 on failure.
 */
ssize_t getStackTraceHeap(uintptr_t* addresses, size_t maxAddresses);

/**
 * Get the current async stack trace into addresses, which has room for at least
 * maxAddresses frames. If no async operation is progress, then this will
 * write 0 frames.
 *
 * This will include both async and non-async frames. For example, the stack
 * trace could look something like this:
 *
 * funcD     <--  non-async, current top of stack
 * funcC     <--  non-async
 * co_funcB  <--  async
 * co_funcA  <--  async
 * main      <--  non-async, root of async stack
 *
 * Async-signal-safe, but likely slower.
 *
 * \param addresses Output buffer for the captured frame addresses.
 * \param maxAddresses Capacity of the addresses buffer, in frames.
 * \returns The number of frames written, or -1 on failure.
 */
FOLLY_NOINLINE ssize_t
getAsyncStackTraceSafe(uintptr_t* addresses, size_t maxAddresses);

} // namespace symbolizer
} // namespace folly
