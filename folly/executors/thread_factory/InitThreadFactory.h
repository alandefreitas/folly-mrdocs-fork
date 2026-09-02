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

#include <memory>
#include <thread>

#include <folly/ScopeGuard.h>
#include <folly/executors/thread_factory/ThreadFactory.h>

namespace folly {

/// A thread factory that runs setup and teardown callbacks around each thread.
///
/// Wraps another thread factory and, on every thread it creates, runs an
/// initializer before the thread's function and a finalizer after it returns.
class InitThreadFactory : public ThreadFactory {
 public:
  /// Constructs the factory from a backing factory and its callbacks.
  ///
  /// \param threadFactory The factory used to create the underlying thread.
  /// \param threadInitializer Callback run on the new thread before its work.
  /// \param threadFinializer Callback run on the new thread after its work.
  explicit InitThreadFactory(
      std::shared_ptr<ThreadFactory> threadFactory,
      Func&& threadInitializer,
      Func&& threadFinializer = [] {})
      : threadFactory_(std::move(threadFactory)),
        threadInitFini_(
            std::make_shared<ThreadInitFini>(
                std::move(threadInitializer), std::move(threadFinializer))) {}

  /// Creates a thread that runs the initializer, the function, then the finalizer.
  ///
  /// \param func The function to run on the new thread.
  /// \returns The newly created thread.
  std::thread newThread(Func&& func) override {
    return threadFactory_->newThread(
        [func = std::move(func), threadInitFini = threadInitFini_]() mutable {
          threadInitFini->initializer();
          SCOPE_EXIT {
            threadInitFini->finalizer();
          };
          func();
        });
  }

  /// Returns the name prefix used by the backing thread factory.
  ///
  /// \returns The thread name prefix.
  const std::string& getNamePrefix() const override {
    return threadFactory_->getNamePrefix();
  }

 private:
  std::shared_ptr<ThreadFactory> threadFactory_;
  struct ThreadInitFini {
    ThreadInitFini(Func&& init, Func&& fini)
        : initializer(std::move(init)), finalizer(std::move(fini)) {}

    Func initializer;
    Func finalizer;
  };
  std::shared_ptr<ThreadInitFini> threadInitFini_;
};

} // namespace folly
