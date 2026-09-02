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
#include <string>
#include <vector>

#include <folly/Indestructible.h>
#include <folly/Synchronized.h>
#include <folly/ThreadLocal.h>

namespace folly {

/**
 * A hook for tracking which threads belong to which thread pools.
 * This is used only by a gdb extension to aid in debugging. You won't be able
 * to see any useful information from within C++ code.
 *
 * An instance of ThreadPoolListHook should be created in the thread pool class
 * that you want to keep track of. Then, to register a thread you call
 * registerThread() on your instance of ThreadPoolListHook from that thread.
 *
 * When a thread exits it will be removed from the list
 * When the thread pool is destroyed, it will be removed from the list
 */
class ThreadPoolListHook {
 public:
  /// Registers a new thread pool with the global list.
  ///
  /// \param name Name used to identify the thread pool when listing threads.
  explicit ThreadPoolListHook(std::string name);

  /// Removes the thread pool from the global list.
  ~ThreadPoolListHook();

  /**
   * Call this from any new thread that the thread pool creates.
   */
  void registerThread();

  /// Copy constructor (deleted).
  ///
  /// \param other The hook to copy from.
  ThreadPoolListHook(const ThreadPoolListHook& other) = delete;

  /// Copy assignment operator (deleted).
  ///
  /// \param other The hook to assign from.
  ThreadPoolListHook& operator=(const ThreadPoolListHook& other) = delete;

 private:
  ThreadPoolListHook();
};

} // namespace folly
