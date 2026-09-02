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

#include <folly/fibers/FiberManagerInternal.h>
#include <folly/fibers/FiberManagerMap.h>
#include <folly/fibers/async/Async.h>
#include <folly/fibers/async/FiberManager.h>

#pragma once

namespace folly {
namespace fibers {
namespace async {

namespace detail {
template <typename F>
lift_unit_t<async_invocable_inner_type_t<F>> executeOnFiberAndWait(
    F&& func, folly::EventBase& evb, FiberManager& fm) {
  DCHECK(!detail::onFiber());
  return addFiberFuture(std::forward<F>(func), fm).getVia(&evb);
}
} // namespace detail

/**
 * Run an async-annotated functor `func`, to completion on a fiber manager `fm`
 * associated with Event Base `evb`, blocking the current thread.
 *
 * Should not be called from fiber context.
 *
 * Several overloads are provided to configure the executor / fiber manager
 * options
 *
 * @param func The async-annotated functor to run.
 * @param opts Options used to configure the fiber manager.
 * @returns The result produced by `func`.
 */
template <typename F>
lift_unit_t<async_invocable_inner_type_t<F>> executeOnFiberAndWait(
    F&& func, const FiberManager::Options& opts = FiberManager::Options()) {
  folly::EventBase evb;
  return detail::executeOnFiberAndWait(
      std::forward<F>(func), evb, getFiberManager(evb, opts));
}

/// Runs `func` to completion on a fiber manager created for a local event
/// base, blocking the current thread.
///
/// @param func The async-annotated functor to run.
/// @param opts Frozen options used to configure the fiber manager.
/// @returns The result produced by `func`.
template <typename F>
lift_unit_t<async_invocable_inner_type_t<F>> executeOnFiberAndWait(
    F&& func, const FiberManager::FrozenOptions& opts) {
  folly::EventBase evb;
  return detail::executeOnFiberAndWait(
      std::forward<F>(func), evb, getFiberManager(evb, opts));
}

/// Runs `func` to completion on the fiber manager for `evb`, blocking the
/// current thread.
///
/// @param func The async-annotated functor to run.
/// @param evb The event base whose fiber manager runs `func`.
/// @param opts Options used to configure the fiber manager.
/// @returns The result produced by `func`.
template <typename F>
lift_unit_t<async_invocable_inner_type_t<F>> executeOnFiberAndWait(
    F&& func,
    folly::EventBase& evb,
    const FiberManager::Options& opts = FiberManager::Options()) {
  return detail::executeOnFiberAndWait(
      std::forward<F>(func), evb, getFiberManager(evb, opts));
}

/// Runs `func` to completion on the fiber manager for `evb`, blocking the
/// current thread.
///
/// @param func The async-annotated functor to run.
/// @param evb The event base whose fiber manager runs `func`.
/// @param opts Frozen options used to configure the fiber manager.
/// @returns The result produced by `func`.
template <typename F>
lift_unit_t<async_invocable_inner_type_t<F>> executeOnFiberAndWait(
    F&& func, folly::EventBase& evb, const FiberManager::FrozenOptions& opts) {
  return detail::executeOnFiberAndWait(
      std::forward<F>(func), evb, getFiberManager(evb, opts));
}

/**
 * Run an async-annotated functor `func`, to completion on a remote
 * fiber manager, blocking the current thread.
 *
 * Should not be called from fiber context.
 *
 * This is similar to the above functions (sugar to initialize
 * Async annotated fiber context) but handle the case where the
 * library uses a dedicated thread pool to run fibers.
 *
 * @param func The async-annotated functor to run.
 * @param fm The remote fiber manager on which to run `func`.
 * @returns The result produced by `func`.
 */
template <typename F>
lift_unit_t<async_invocable_inner_type_t<F>> executeOnRemoteFiberAndWait(
    F&& func, FiberManager& fm) {
  DCHECK(!detail::onFiber());
  return addFiberRemoteFuture(std::forward<F>(func), fm).get();
}

} // namespace async
} // namespace fibers
} // namespace folly
