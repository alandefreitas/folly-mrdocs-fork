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

#include <folly/Function.h>
#include <folly/Synchronized.h>
#include <folly/observer/Observer.h>
#include <folly/synchronization/DelayedInit.h>

/// The Folly library.
namespace folly {
/// Folly's observer library.
namespace observer {

/// An observable whose value is set manually and read through an `Observer`.
template <typename T>
class SimpleObservable {
 public:
  /// Constructs the observable holding a default-constructed value.
  template <typename U = T>
    requires std::is_default_constructible<U>::value
  SimpleObservable();

  /// Constructs the observable holding the given value.
  ///
  /// \param value The initial value.
  explicit SimpleObservable(T value);
  /// Constructs the observable holding the given shared value.
  ///
  /// \param value The initial shared value.
  explicit SimpleObservable(std::shared_ptr<const T> value);

  /// Replaces the current value.
  ///
  /// \param value The new value.
  void setValue(T value);
  /// Replaces the current value with a shared value.
  ///
  /// \param value The new shared value.
  void setValue(std::shared_ptr<const T> value);

  /// Returns an `Observer` tracking this observable's value.
  ///
  /// \returns An `Observer` reflecting the current value.
  auto getObserver() const;

 private:
  struct Context {
    folly::Synchronized<std::shared_ptr<const T>> value_;
    folly::Synchronized<folly::Function<void()>> callback_;

    Context() = default;
    explicit Context(std::shared_ptr<const T> value);
  };
  struct Wrapper;
  std::shared_ptr<Context> context_;

  mutable folly::DelayedInit<
      Observer<typename observer_detail::Unwrap<T>::type>>
      observer_;
};
} // namespace observer
} // namespace folly

#include <folly/observer/SimpleObservable-inl.h>
