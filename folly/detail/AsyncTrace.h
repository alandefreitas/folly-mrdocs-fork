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

#include <chrono>

#include <folly/Optional.h>

/// The Folly library.
namespace folly {

/// Schedules and runs asynchronous work.
class Executor;
/// An executor backed by an I/O event loop.
class IOExecutor;

/// Implementation details.
namespace async_tracing {
/// Whether a discarded SemiFuture carried a deferred executor.
enum class DiscardHasDeferred {
  NO_EXECUTOR, ///< No deferred executor was present.
  DEFERRED_EXECUTOR, ///< A deferred executor was present.
};
/// Trace hook invoked when the global CPU executor is set.
///
/// \param executor The executor being installed.
void logSetGlobalCPUExecutor(Executor* executor) noexcept;
/// Trace hook invoked when the global CPU executor is set to the immutable one.
void logSetGlobalCPUExecutorToImmutable() noexcept;
/// Trace hook invoked when the global CPU executor is retrieved.
///
/// \param executor The executor being returned.
void logGetGlobalCPUExecutor(Executor* executor) noexcept;
/// Trace hook invoked when the immutable CPU executor is retrieved.
///
/// \param executor The executor being returned.
void logGetImmutableCPUExecutor(Executor* executor) noexcept;
/// Trace hook invoked when the global IO executor is set.
///
/// \param executor The executor being installed.
void logSetGlobalIOExecutor(IOExecutor* executor) noexcept;
/// Trace hook invoked when the global IO executor is retrieved.
///
/// \param executor The executor being returned.
void logGetGlobalIOExecutor(IOExecutor* executor) noexcept;
/// Trace hook invoked when the immutable IO executor is retrieved.
///
/// \param executor The executor being returned.
void logGetImmutableIOExecutor(IOExecutor* executor) noexcept;
/// Trace hook invoked when a SemiFuture is rescheduled onto an executor.
///
/// \param from The executor the work is moving away from.
/// \param to The executor the work is moving to.
void logSemiFutureVia(Executor* from, Executor* to) noexcept;
/// Trace hook invoked when a Future is rescheduled onto an executor.
///
/// \param from The executor the work is moving away from.
/// \param to The executor the work is moving to.
void logFutureVia(Executor* from, Executor* to) noexcept;
/// Trace hook invoked for a blocking operation.
///
/// \param duration The observed duration of the blocking operation.
void logBlockingOperation(std::chrono::milliseconds duration) noexcept;
/// Trace hook invoked when a SemiFuture is discarded.
///
/// \param hasDeferredExecutor Whether the discarded SemiFuture had a deferred
/// executor.
void logSemiFutureDiscard(
    DiscardHasDeferred hasDeferredExecutor) noexcept;
} // namespace async_tracing
} // namespace folly
