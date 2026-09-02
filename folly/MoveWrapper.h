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

#include <folly/CppAttributes.h>

namespace folly {

/** C++11 closures don't support move-in capture. Nor does std::bind.
    facepalm.

    http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3610.html

    "[...] a work-around that should make people's stomach crawl:
    write a wrapper that performs move-on-copy, much like the deprecated
    auto_ptr"

    Unlike auto_ptr, this doesn't require a heap allocation.
    */
template <class T>
class MoveWrapper {
 public:
  /** If value can be default-constructed, why not?
      Then we don't have to move it in */
  MoveWrapper() = default;

  /// Move a value in.
  ///
  /// \param t The value to move into the wrapper.
  explicit MoveWrapper(T&& t) : value(std::move(t)) {}

  /// copy is move
  ///
  /// \param other The wrapper to move from.
  MoveWrapper(const MoveWrapper& other) : value(std::move(other.value)) {}

  /// move is also move
  ///
  /// \param other The wrapper to move from.
  MoveWrapper(MoveWrapper&& other) : value(std::move(other.value)) {}

  /// Access the wrapped value.
  ///
  /// \returns A const reference to the wrapped value.
  const T& operator*() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return value;
  }
  /// Access the wrapped value.
  ///
  /// \returns A reference to the wrapped value.
  T& operator*() [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] { return value; }

  /// Access members of the wrapped value.
  ///
  /// \returns A const pointer to the wrapped value.
  const T* operator->() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return &value;
  }
  /// Access members of the wrapped value.
  ///
  /// \returns A pointer to the wrapped value.
  T* operator->() [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] { return &value; }

  /// move the value out (sugar for std::move(*moveWrapper))
  ///
  /// \returns An rvalue reference to the wrapped value.
  T&& move() { return std::move(value); }

  /// Copy-assignment is deleted; a MoveWrapper cannot be copy-assigned.
  ///
  /// \param other The wrapper that would be assigned from.
  /// \returns A reference to this wrapper.
  MoveWrapper& operator=(MoveWrapper const& other) = delete;
  /// Move-assignment is deleted; a MoveWrapper cannot be move-assigned.
  ///
  /// \param other The wrapper that would be assigned from.
  /// \returns A reference to this wrapper.
  MoveWrapper& operator=(MoveWrapper&& other) = delete;

 private:
  mutable T value;
};

/// Make a MoveWrapper from the argument. Because the name "makeMoveWrapper"
/// is already quite transparent in its intent, this will work for lvalues as
/// if you had wrapped them in std::move.
///
/// \param t The value to wrap.
/// \returns A MoveWrapper holding the given value.
template <class T, class T0 = typename std::remove_reference<T>::type>
MoveWrapper<T0> makeMoveWrapper(T&& t) {
  return MoveWrapper<T0>(std::forward<T0>(t));
}

} // namespace folly
