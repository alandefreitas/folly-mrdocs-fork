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

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <folly/CPortability.h>
#include <folly/Memory.h>

namespace folly {
/// Namespace for experimental Folly components.
namespace experimental {

/// Class to model the process environment in idiomatic C++
///
/// Changes to the modeled environment do not change the process environment
/// unless `setAsCurrentEnvironment()` is called.
struct EnvironmentState {
  /// Map type used to store environment variable names and values.
  using EnvType = std::unordered_map<std::string, std::string>;

  /// Returns an EnvironmentState containing a copy of the current process
  /// environment. Subsequent changes to the process environment do not
  /// alter the stored model. If the process environment is altered during the
  /// execution of this method the results are not defined.
  ///
  /// Throws `MalformedEnvironment` if the process environment cannot be modeled.
  ///
  /// \returns An EnvironmentState modeling the current process environment.
  static EnvironmentState fromCurrentEnvironment();

  /// Returns an empty EnvironmentState
  ///
  /// \returns An EnvironmentState with no environment variables.
  static EnvironmentState empty() { return {}; }

  /// Constructs the state from a copy of the given environment map.
  ///
  /// \param env The environment map to copy.
  explicit EnvironmentState(EnvType const& env) : env_(env) {}

  /// Constructs the state by moving from the given environment map.
  ///
  /// \param env The environment map to move from.
  explicit EnvironmentState(EnvType&& env) : env_(std::move(env)) {}

  /// Get the model environment for querying.
  ///
  /// \returns A const reference to the modeled environment.
  EnvType const& operator*() const { return env_; }

  /// Get a pointer to the model environment for querying.
  ///
  /// \returns A const pointer to the modeled environment.
  EnvType const* operator->() const { return &env_; }

  /// Get the model environment for mutation or querying.
  ///
  /// \returns A mutable reference to the modeled environment.
  EnvType& operator*() { return env_; }

  /// Get a pointer to the model environment for mutation or querying.
  ///
  /// \returns A mutable pointer to the modeled environment.
  EnvType* operator->() { return &env_; }

  /// Update the process environment with the one in the stored model.
  /// Subsequent changes to the model do not alter the process environment. The
  /// state of the process environment during execution of this method is not
  /// defined. If the process environment is altered by another thread during the
  /// execution of this method the results are not defined.
  void setAsCurrentEnvironment();

  /// Get a copy of the model environment in the form used by `folly::Subprocess`
  ///
  /// \returns The model environment as a vector of "name=value" strings.
  std::vector<std::string> toVector() const;

  /// Get a copy of the model environment in the form commonly used by C
  /// routines such as execve, execle, etc. Example usage:
  ///
  /// EnvironmentState forChild{};
  /// ... manipulate `forChild` as needed ...
  /// execve("/bin/program",pArgs,forChild.toPointerArray().get());
  ///
  /// \returns The model environment as a null-terminated array of C strings.
  std::unique_ptr<char*, void (*)(char**)> toPointerArray() const;

 private:
  EnvironmentState() {}
  EnvType env_;
};

/// Exception thrown when the process environment cannot be modeled.
struct FOLLY_EXPORT MalformedEnvironment : std::runtime_error {
  /// Inherits the constructors of std::runtime_error.
  using std::runtime_error::runtime_error;
};
} // namespace experimental

/// Namespace for testing utilities.
namespace test {
/// RAII class allowing scoped changes to the process environment. The
/// environment state at the time of its construction is restored at the time
/// of its destruction.
struct EnvVarSaver {
  /// Constructs the saver, capturing the current process environment.
  EnvVarSaver()
      : state_(
            std::make_unique<experimental::EnvironmentState>(
                experimental::EnvironmentState::fromCurrentEnvironment())) {}

  /// Move-constructs the saver from another, taking over its saved state.
  ///
  /// \param other The saver to move from.
  EnvVarSaver(EnvVarSaver&& other) noexcept : state_(std::move(other.state_)) {}

  /// Move-assigns the saved state from another saver.
  ///
  /// \param other The saver to move from.
  /// \returns A reference to this saver.
  EnvVarSaver& operator=(EnvVarSaver&& other) noexcept {
    state_ = std::move(other.state_);
    return *this;
  }

  /// Restores the process environment captured at construction.
  ~EnvVarSaver() {
    if (state_) {
      state_->setAsCurrentEnvironment();
    }
  }

 private:
  std::unique_ptr<experimental::EnvironmentState> state_;
};
} // namespace test
} // namespace folly
