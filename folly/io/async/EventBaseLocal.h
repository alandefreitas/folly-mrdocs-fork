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

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include <folly/Likely.h>
#include <folly/Synchronized.h>
#include <folly/container/F14Set.h>
#include <folly/io/async/EventBase.h>
#include <folly/lang/Thunk.h>

/// Facebook Folly library namespace.
namespace folly {

namespace detail {

class EventBaseLocalBase {
 public:
  friend class folly::EventBase;
  EventBaseLocalBase() = default;
  EventBaseLocalBase(const EventBaseLocalBase&) = delete;
  EventBaseLocalBase& operator=(const EventBaseLocalBase&) = delete;
  ~EventBaseLocalBase();
  void erase(EventBase& evb);

 protected:
  void setVoid(EventBase& evb, void* ptr, void (*dtor)(void*));
  void* getVoid(EventBase& evb);

 private:
  bool tryDeregister(EventBase& evb);

  static std::atomic<std::size_t> keyCounter_;

  const std::size_t key_{keyCounter_++};
  const folly::F14HashToken keyToken_{
      folly::EventBase::LocalStorageMap{}.prehash(key_)};
  folly::Synchronized<folly::F14FastSet<EventBase*>> eventBases_;
};

} // namespace detail

/**
 * A storage abstraction for data that should be tied to an EventBase.
 *
 *   struct Foo { Foo(int a, int b); };
 *   EventBaseLocal<Foo> myFoo;
 *   ...
 *   EventBase evb;
 *   myFoo.emplace(evb, Foo(1, 2));
 *   myFoo.emplace(evb, 1, 2);
 *   Foo* foo = myFoo.get(evb);
 *   myFoo.erase(evb);
 *   Foo& foo = myFoo.try_emplace(evb, 1, 2); // ctor if missing
 *   Foo& foo = myFoo.try_emplace(evb, 1, 2); // noop if present
 *   myFoo.erase(evb);
 *   Foo& foo = myFoo.try_emplace_with(evb, [] { return Foo(3, 4); })
 *
 * The objects will be deleted when the EventBaseLocal or the EventBase is
 * destructed (whichever comes first). All methods must be called from the
 * EventBase thread.
 *
 * The user is responsible for throwing away invalid references/ptrs returned
 * by the get() method after emplace/erase is called.  If shared ownership is
 * needed, use a EventBaseLocal<shared_ptr<...>>.
 */
template <typename T>
class EventBaseLocal : public detail::EventBaseLocalBase {
 private:
  template <typename U, typename... A>
  using if_ilist_emplaceable_t = std::enable_if_t<
      std::is_constructible<T, std::initializer_list<U>, A...>::value,
      int>;

  T& store(EventBase& evb, T* const ptr) {
    setVoid(evb, ptr, detail::thunk::ruin<T>);
    return *ptr;
  }

 public:
  /// Constructs an empty EventBaseLocal.
  EventBaseLocal() = default;

  /// Returns a pointer to the object stored for the given EventBase, or null.
  ///
  /// \param evb The EventBase whose stored object is requested.
  /// \returns A pointer to the stored object, or null if none exists.
  T* get(EventBase& evb) { return static_cast<T*>(getVoid(evb)); }

  /// Constructs the object for the given EventBase, replacing any existing one.
  ///
  /// \param evb The EventBase to store the object for.
  /// \param a The arguments forwarded to the object constructor.
  /// \returns A reference to the constructed object.
  template <typename... A>
  T& emplace(EventBase& evb, A&&... a) {
    return store(evb, new T(static_cast<A&&>(a)...));
  }

  /// Constructs the object from an initializer list, replacing any existing one.
  ///
  /// \param evb The EventBase to store the object for.
  /// \param i The initializer list forwarded to the object constructor.
  /// \param a The arguments forwarded to the object constructor.
  /// \returns A reference to the constructed object.
  template <typename U, typename... A, if_ilist_emplaceable_t<U, A...> = 0>
  T& emplace(EventBase& evb, std::initializer_list<U> i, A&&... a) {
    return store(evb, new T(i, static_cast<A&&>(a)...));
  }

  /// Constructs the object from the result of calling f, replacing any existing.
  ///
  /// \param evb The EventBase to store the object for.
  /// \param f The callable whose result initializes the object.
  /// \returns A reference to the constructed object.
  template <typename F>
  T& emplace_with(EventBase& evb, F f) {
    return store(evb, new T(f()));
  }

  /// Returns the existing object, or constructs it if none exists.
  ///
  /// \param evb The EventBase to store the object for.
  /// \param a The arguments forwarded to the object constructor.
  /// \returns A reference to the existing or newly constructed object.
  template <typename... A>
  T& try_emplace(EventBase& evb, A&&... a) {
    auto const ptr = get(evb);
    return FOLLY_LIKELY(!!ptr) ? *ptr : emplace(evb, static_cast<A&&>(a)...);
  }

  /// Returns the existing object, or constructs it from an initializer list.
  ///
  /// \param evb The EventBase to store the object for.
  /// \param i The initializer list forwarded to the object constructor.
  /// \param a The arguments forwarded to the object constructor.
  /// \returns A reference to the existing or newly constructed object.
  template <typename U, typename... A, if_ilist_emplaceable_t<U, A...> = 0>
  T& try_emplace(EventBase& evb, std::initializer_list<U> i, A&&... a) {
    auto const ptr = get(evb);
    return FOLLY_LIKELY(!!ptr) ? *ptr : emplace(evb, i, static_cast<A&&>(a)...);
  }

  /// Returns the existing object, or constructs it from the result of f.
  ///
  /// \param evb The EventBase to store the object for.
  /// \param f The callable whose result initializes the object.
  /// \returns A reference to the existing or newly constructed object.
  template <typename F>
  T& try_emplace_with(EventBase& evb, F f) {
    auto const ptr = get(evb);
    return FOLLY_LIKELY(!!ptr) ? *ptr : emplace_with(evb, std::ref(f));
  }
};

} // namespace folly
