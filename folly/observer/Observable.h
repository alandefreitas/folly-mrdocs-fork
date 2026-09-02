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

#include <folly/observer/Observer.h>
#include <folly/synchronization/Baton.h>

/// The Folly library.
namespace folly {
/// Folly's observer library.
namespace observer {

/// Implementation details.
namespace detail {
template <typename Observable, typename Traits>
class ObserverCreatorContext;
}

/// Default traits describing how to read from and subscribe to an observable.
template <typename Observable>
struct ObservableTraits {
  /// The element type produced by the observable.
  using element_type =
      typename std::remove_reference<Observable>::type::element_type;

  /// Reads the current value from the observable.
  ///
  /// \param observable The observable to read from.
  /// \returns A shared pointer to the observable's current value.
  static std::shared_ptr<const element_type> get(Observable& observable) {
    return observable.get();
  }

  /// Subscribes a callback for value changes on the observable.
  ///
  /// \param observable The observable to subscribe to.
  /// \param callback The callable invoked when the value changes.
  template <typename F>
  static void subscribe(Observable& observable, F&& callback) {
    observable.subscribe(std::forward<F>(callback));
  }

  /// Cancels the subscription on the observable.
  ///
  /// \param observable The observable to unsubscribe from.
  static void unsubscribe(Observable& observable) { observable.unsubscribe(); }
};

/// Creates an `Observer` from an observable using the given traits.
template <typename Observable, typename Traits = ObservableTraits<Observable>>
class ObserverCreator {
 public:
  /// The observed element type.
  using T = typename Traits::element_type;

  /// Constructs the creator, forwarding arguments to the observable.
  ///
  /// \param args Arguments forwarded to construct the observable.
  template <typename... Args>
  explicit ObserverCreator(Args&&... args);

  /// Builds an `Observer` that tracks the observable's value.
  ///
  /// \returns An `Observer` reflecting the observable's current value.
  Observer<T> getObserver() &&;

 private:
  using Context = detail::ObserverCreatorContext<Observable, Traits>;
  class ContextPrimaryPtr;

  class NamedCreator;

  std::shared_ptr<Context> context_;
};
} // namespace observer
} // namespace folly

#include <folly/observer/Observable-inl.h>
