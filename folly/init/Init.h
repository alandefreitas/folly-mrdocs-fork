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
#include <bitset>

#include <folly/Portability.h>

namespace folly {

/// Environment variable name that controls folly logging configuration.
constexpr char const* kLoggingEnvVarName = "FOLLY_LOGGING";

/// Options controlling folly initialization.
class InitOptions {
 public:
  /// Constructs options with default values.
  InitOptions() noexcept;

  /// Whether to update argc/argv to remove recognized gflags.
  bool remove_flags{true};
  /// Whether to initialize gflags.
  bool use_gflags{true};
  /// Controls whether folly::symbolizer::installFatalSignalCallbacks() is called
  /// during init.
  bool install_fatal_signal_callbacks{true};

  /// Mask of all fatal (default handler of terminating the process) signals for
  /// which `init()` will install handler that print stack traces and invokes
  /// previously established handler  (or terminate if there were none).
  /// Signals that are not in `symbolizer::kAllFatalSignals` will be ignored
  /// if passed here.
  /// Defaults to all signal in `symbolizer::kAllFatalSignals`.
  std::bitset<64> fatal_signals;

  /// Sets whether recognized gflags are removed from argc/argv.
  ///
  /// \param remove New value for the flag.
  /// \returns Reference to this options object.
  InitOptions& removeFlags(bool remove) {
    remove_flags = remove;
    return *this;
  }

  /// Sets the mask of fatal signals to handle.
  ///
  /// \param val Bitmask of fatal signals.
  /// \returns Reference to this options object.
  InitOptions& fatalSignals(unsigned long val) {
    fatal_signals = val;
    return *this;
  }

  /// Sets whether gflags is initialized.
  ///
  /// \param useGFlags New value for the flag.
  /// \returns Reference to this options object.
  InitOptions& useGFlags(bool useGFlags) {
    use_gflags = useGFlags;
    return *this;
  }

  /// Sets whether fatal signal callbacks are installed.
  ///
  /// \param installFatalSignalCallbacks New value for the flag.
  /// \returns Reference to this options object.
  InitOptions& installFatalSignalCallbacks(bool installFatalSignalCallbacks) {
    install_fatal_signal_callbacks = installFatalSignalCallbacks;
    return *this;
  }
};

/// RAII object constructed at the beginning of main() and destructed
/// implicitly at the end of main().
///
/// The constructor calls common init functions in the necessary order.
/// Among other things, this ensures that folly::Singletons are initialized
/// correctly and installs signal handlers for a superior debugging experience.
/// It also initializes gflags and glog.
///
/// The destructor destroys all singletons managed by folly::Singleton, yielding
/// better shutdown behavior when performed at the end of main(). In particular,
/// this guarantees that all singletons managed by folly::Singleton are destroyed
/// before all Meyers singletons are destroyed.
class [[nodiscard]] Init {
 public:
  // Force ctor & dtor out of line for better stack traces even with LTO.
  /// Constructs the guard and runs folly initialization.
  ///
  /// \param argc Pointer to the argument count passed to main.
  /// \param argv Pointer to the argument vector passed to main.
  /// \param removeFlags If true, updates argc/argv to remove recognized gflags.
  FOLLY_NOINLINE Init(int* argc, char*** argv, bool removeFlags = true);

  /// Constructs the guard and runs folly initialization.
  ///
  /// \param argc Pointer to the argument count passed to main.
  /// \param argv Pointer to the argument vector passed to main.
  /// \param options Initialization options.
  FOLLY_NOINLINE Init(int* argc, char*** argv, InitOptions options);

  /// Destroys the guard and tears down folly singletons.
  FOLLY_NOINLINE ~Init();

  /// Deleted copy constructor.
  ///
  /// \param other Ignored (deleted).
  Init(Init const& other) = delete;
  /// Deleted move constructor.
  ///
  /// \param other Ignored (deleted).
  Init(Init&& other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param other Ignored (deleted).
  /// \returns Never returns (deleted).
  Init& operator=(Init const& other) = delete;
  /// Deleted move assignment operator.
  ///
  /// \param other Ignored (deleted).
  /// \returns Never returns (deleted).
  Init& operator=(Init&& other) = delete;
};

/// Initializes folly without an RAII scope guard.
///
/// \param argc Pointer to the argument count passed to main.
/// \param argv Pointer to the argument vector passed to main.
/// \param removeFlags If true, updates argc/argv to remove recognized gflags.
void unsafe_unscoped_init(int* argc, char*** argv, bool removeFlags = true);
/// Initializes folly without an RAII scope guard.
///
/// \param argc Pointer to the argument count passed to main.
/// \param argv Pointer to the argument vector passed to main.
/// \param options Initialization options.
void unsafe_unscoped_init(int* argc, char*** argv, InitOptions options);

/// Initializes folly (deprecated non-RAII form).
///
/// \param argc Pointer to the argument count passed to main.
/// \param argv Pointer to the argument vector passed to main.
/// \param removeFlags If true, updates argc/argv to remove recognized gflags.
[[deprecated("Use the RAII version Init")]] inline void init(
    int* argc, char*** argv, bool removeFlags = true) {
  unsafe_unscoped_init(argc, argv, removeFlags);
}
/// Initializes folly (deprecated non-RAII form).
///
/// \param argc Pointer to the argument count passed to main.
/// \param argv Pointer to the argument vector passed to main.
/// \param options Initialization options.
[[deprecated("Use the RAII version Init")]] inline void init(
    int* argc, char*** argv, InitOptions options) {
  unsafe_unscoped_init(argc, argv, std::move(options));
}

} // namespace folly
