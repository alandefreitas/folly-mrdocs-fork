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

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <folly/Portability.h>

namespace folly {
/// Facilities for capturing and stacking exception stack traces.
namespace exception_tracer {

/// Maximum number of frames captured in a single stack trace.
constexpr size_t kMaxFrames = 500;

/// A captured stack trace as a fixed-capacity array of return addresses.
struct StackTrace {
  /// Construct an empty stack trace with no frames.
  StackTrace() : frameCount(0) {}

  /// Number of valid frames in addresses.
  size_t frameCount;
  /// The captured return addresses, from top to bottom of the stack.
  uintptr_t addresses[kMaxFrames];
};

/// An intrusive stack of captured stack traces.
class StackTraceStack {
  class Node;

 public:
  /// Construct an empty stack.
  constexpr StackTraceStack() = default;

  /// Deleted copy constructor; the stack is non-copyable.
  StackTraceStack(const StackTraceStack& other) = delete;
  /// Deleted copy assignment; the stack is non-copyable.
  void operator=(const StackTraceStack& other) = delete;

  /**
   * Push the current stack trace onto the stack.
   *
   * \returns false on failure (not enough memory, getting stack trace failed),
   * true on success.
   */
  bool pushCurrent();

  /**
   * Pop the top stack trace from the stack.
   *
   * \returns true on success, false on failure (stack was empty).
   */
  bool pop();

  /**
   * Move the top stack trace from other onto this.
   *
   * \param other The stack to move the top trace from.
   * \returns true on success, false on failure (other was empty).
   */
  bool moveTopFrom(StackTraceStack& other);

  /**
   * Clear the stack.
   */

  void clear();

  /**
   * Is the stack empty?
   *
   * \returns true if the stack holds no traces.
   */
  bool empty() const { return !state_; }

  /**
   * Return the top stack trace, or nullptr if the stack is empty.
   *
   * \returns The top stack trace, or nullptr if empty.
   */
  StackTrace* top();
  /// \copydoc top()
  ///
  /// \returns The top stack trace, or nullptr if empty.
  const StackTrace* top() const;

  /**
   * Return the stack trace following p, or nullptr if p is the bottom of
   * the stack.
   *
   * \param p The stack trace to advance from.
   * \returns The stack trace following p, or nullptr at the bottom.
   */
  StackTrace* next(StackTrace* p);
  /// \copydoc next(StackTrace*)
  ///
  /// \param p The stack trace to advance from.
  /// \returns The stack trace following p, or nullptr at the bottom.
  const StackTrace* next(const StackTrace* p) const;

 private:
  Node* state_ = nullptr;
};
} // namespace exception_tracer
} // namespace folly
