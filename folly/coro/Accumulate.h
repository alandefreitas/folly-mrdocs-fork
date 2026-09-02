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

#include <folly/coro/AsyncGenerator.h>
#include <folly/coro/Coroutine.h>
#include <folly/coro/Task.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

/// Accumulate the values from an input stream into a single value, similar to
/// `std::accumulate`.
///
/// The input is a stream of values and the output is a Task containing the
/// result of the accumulation.
///
/// Example:
/// @code
///   AsyncGenerator<int> stream();
///
///   Task<void> consumer() {
///     auto sum = co_await accumulate(stream(), 0, std::plus{});
///   }
/// @endcode
///
/// @param generator The input stream of values.
/// @param init The initial value of the accumulation.
/// @returns A Task containing the result of the accumulation.
template <typename Reference, typename Value, typename Output>
Task<Output> accumulate(
    AsyncGenerator<Reference, Value> generator, Output init);

/// Accumulate the values from an input stream using a binary operation.
///
/// @param generator The input stream of values.
/// @param init The initial value of the accumulation.
/// @param op The binary operation combining the accumulator and each value.
/// @returns A Task containing the result of the accumulation.
template <
    typename Reference,
    typename Value,
    typename Output,
    typename BinaryOp>
Task<Output> accumulate(
    AsyncGenerator<Reference, Value> generator, Output init, BinaryOp op);

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES

#include <folly/coro/Accumulate-inl.h>
