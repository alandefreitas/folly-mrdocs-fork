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

#include <folly/observer/Observer.h>

/// The Folly library.
namespace folly {
/// Folly's observer library.
namespace observer {

/**
 * The returned Observer will proxy updates from the input observer but will
 * delay the propagation of each update by some duration between 0 ms and
 * lag + jitter. In addition, if an update arrives while no preceding jittered
 * updates are still in flight, then the delay applied to the latest update will
 * be a uniformly random duration between lag - jitter and lag + jitter.
 *
 * \param observer The input observer whose updates are proxied.
 * \param lag The base delay applied to each propagated update.
 * \param jitter The random spread added to or subtracted from the lag.
 * \returns An `Observer` that proxies updates with jittered delays.
 */
template <typename T>
Observer<T> withJitter(
    Observer<T> observer,
    std::chrono::milliseconds lag,
    std::chrono::milliseconds jitter);

} // namespace observer
} // namespace folly

#include <folly/observer/WithJitter-inl.h>
