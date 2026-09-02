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

#include <atomic>

/// Folly library namespace.
namespace folly {

/// Process phases for programs that use Folly:
/// - Init: Not all globals may have been initialized.
/// - Regular: All globals have been initialized and have not
///   been destroyed.
/// - Exit: Some globals may have been destroyed.

/// Process phases
enum class ProcessPhase {
  Init = 0, ///< Not all globals may have been initialized.
  Regular = 1, ///< All globals have been initialized and not yet destroyed.
  Exit = 2, ///< Some globals may have been destroyed.
};

/// Start Regular phase and register handler to set Exit phase.
/// To be called exactly once in each program that uses Folly.
/// Ideally, it is to be called from folly::init(), which in turn
/// is to be called by every program that uses Folly.
void set_process_phases();

/// Get the current process phase.
///
/// \returns The current process phase.
ProcessPhase get_process_phase() noexcept;

} // namespace folly
