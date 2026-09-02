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

#include <fmt/format.h>
#include <folly/Range.h>
#include <folly/logging/xlog.h>

/// Example code demonstrating the Folly logging library.
namespace example {

/// Example object that logs to the folly.logging.example.lib category.
class ExampleObject {
 public:
  /// Construct an ExampleObject holding the given string value.
  ///
  /// \param str The string value to store.
  explicit ExampleObject(folly::StringPiece str) : value_{str.str()} {
    // All XLOG() statements in this file will log to the category
    // folly.logging.example.lib
    XLOGF(DBG1, "ExampleObject({}) constructed at {}", value_, fmt::ptr(this));
  }

  /// Destroy the ExampleObject.
  ~ExampleObject();

  /// Perform some example work, emitting log messages.
  void doStuff();

 private:
  std::string value_;
};
} // namespace example
