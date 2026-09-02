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

#include <folly/ThreadLocal.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/io/async/EventBase.h>

namespace folly {

/// Caches objects of type T that are bound to an EventBase from the global
/// IOExecutor.
///
/// Provide a factory that creates T objects given an EventBase, and get() will
/// lazily create T objects based on an EventBase from the global IOExecutor.
/// These are stored thread locally: for a given pair of event base and calling
/// thread there will only be one T object created.
///
/// The primary use case is for managing objects that need to do async IO on an
/// event base (e.g. thrift clients) that can be used outside the IO thread
/// without much hassle. For instance, you could use this to manage Thrift
/// clients that are only ever called from within other threads without the
/// calling thread needing to know anything about the IO threads that the
/// clients will do their work on.
///
/// \tparam T The type of object cached per EventBase.
template <class T>
class IOObjectCache {
 public:
  /// Factory that creates a T object bound to a given EventBase.
  using TFactory = std::function<std::shared_ptr<T>(folly::EventBase*)>;

  /// Construct an empty cache with no factory set.
  IOObjectCache() = default;

  /// Construct a cache that creates objects with the given factory.
  ///
  /// \param factory The factory used to create T objects for an EventBase.
  explicit IOObjectCache(TFactory factory) : factory_(std::move(factory)) {}

  /// Return the cached object for the current thread and IO event base.
  ///
  /// Lazily creates the object with the factory if none exists yet for the
  /// calling thread and the EventBase from the global IOExecutor.
  ///
  /// \returns The shared object for the current thread and event base.
  std::shared_ptr<T> get() {
    CHECK(factory_);
    auto eb = getIOExecutor()->getEventBase();
    CHECK(eb);
    auto it = cache_->find(eb);
    if (it == cache_->end()) {
      auto p = cache_->insert(std::make_pair(eb, factory_(eb)));
      it = p.first;
    }
    return it->second;
  }

  /// Set the factory used to create cached objects.
  ///
  /// \param factory The factory used to create T objects for an EventBase.
  void setFactory(TFactory factory) { factory_ = std::move(factory); }

 private:
  folly::ThreadLocal<std::map<folly::EventBase*, std::shared_ptr<T>>> cache_;
  TFactory factory_;
};

} // namespace folly
