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
#include <memory>

#include <folly/CppAttributes.h>
#include <folly/SharedMutex.h>
#include <folly/ThreadLocal.h>
#include <folly/observer/Observer-pre.h>
#include <folly/observer/detail/Core.h>

/// The Folly library.
namespace folly {
/// Folly's observer library.
namespace observer {

/**
 * Observer - a library which lets you create objects which track updates of
 * their dependencies and get re-computed when any of the dependencies changes.
 *
 *
 * The preferred way to read the observed value is with(), which keeps the
 * snapshot alive for the duration of the lambda and prevents read-after-free:
 *
 *     Observer<Config> configObserver = ...;
 *     auto result = configObserver.with([](const Config& cfg) {
 *       return cfg.getSomeValue();
 *     });
 *
 * For cases where you need to store or pass the snapshot around, you can get
 * one explicitly:
 *
 *     Observer<int> myObserver = ...;
 *     Snapshot<int> mySnapshot = myObserver.getSnapshot();
 *
 * or simply
 *
 *     Snapshot<int> mySnapshot = *myObserver;
 *
 * Snapshot will hold a view of the object, even if object in the Observer
 * gets updated.
 *
 * Note: fetching a snapshot from Observer will never block/fail. And returned
 * snapshot will never contain a nullptr.
 *
 *
 * What makes Observer powerful is its ability to track updates to other
 * Observers. Imagine we have two separate Observers A and B which hold
 * integers.
 *
 *     Observer<int> observerA = ...;
 *     Observer<int> observerB = ...;
 *
 * To compute a sum of A and B we can create a new Observer which would track
 * updates to A and B and re-compute the sum only when necessary.
 *
 *     Observer<int> sumObserver = makeObserver(
 *         [observerA, observerB] {
 *           int a = **observerA;
 *           int b = **observerB;
 *           return a + b;
 *         });
 *
 *     int sum = **sumObserver;
 *
 * Notice that a + b will be only called when either a or b is changed. Getting
 * a snapshot from sumObserver won't trigger any re-computation.
 *
 * Getting an Observer snapshot involves acquiring a shared_ptr, which can be
 * expensive, especially if several threads do so concurrently. If the cost of
 * getSnapshot() is noticeable, alternative Observer implementations are
 * available, offering different trade-offs:
 *
 * - If T is a type for which std::atomic<T> is lock-free (all word-sized PODs
 *   for example), AtomicObserver and ReadMostlyAtomicObserver offer the best
 *   performance at no additional memory cost.
 *
 * - TLObserver stores a thread-local snapshot, so that it can be accessed
 *   without synchronization (except when it needs updating). This however can
 *   consume significant amounts of memory by stranding old snapshots in threads
 *   that do not access, and thus refresh, the observer.
 *
 * - HazptrObserver uses hazard pointers to protect the snapshot, which offer
 *   high read scalability and low cost, but the snapshot should be held as
 *   little as possible and should not cross coroutine suspension points.
 *
 * - ReadMostlyTLObserver returns a snapshot that can be used like a regular
 *   shared_ptr. Scalability and cost are comparable to HazptrObserver, but the
 *   snapshots can be held for arbitrary time. Memory cost is a small constant
 *   for each thread that acquires a snapshot.
 *
 * - CoreCachedObserver can be used if a std::shared_ptr<T> is strictly
 *   required. Read scalability is comparable to the previous options, but cost
 *   is moderately higher. Memory cost is a small constant for each CPU in the
 *   system.
 *
 * See ObserverCreator class if you want to wrap any existing subscription API
 * in an Observer object.
 */
template <typename T>
class Observer;

/**
 * An AtomicObserver provides read-optimized caching for an Observer using
 * `std::atomic`. Reading only requires atomic loads unless the cached value
 * is stale. If the cache needs to be refreshed, a mutex is used to
 * synchronize the update. This avoids creating a shared_ptr for every read.
 *
 * AtomicObserver models CopyConstructible and MoveConstructible. Copying or
 * moving simply invalidates the cache.
 *
 * AtomicObserver is ideal when there are lots of reads on a trivially-copyable
 * type. if `std::atomic<T>` is not possible but you still want to optimize
 * reads, consider a TLObserver.
 *
 *   Observer<int> observer = ...;
 *   AtomicObserver<int> atomicObserver(observer);
 *   auto value = *atomicObserver;
 */
template <typename T>
class AtomicObserver;

/**
 * A TLObserver provides read-optimized caching for an Observer using
 * thread-local storage. This avoids creating a shared_ptr for every read.
 *
 * The functionality is similar to that of AtomicObserver except it allows types
 * that don't support atomics. If possible, use AtomicObserver instead.
 *
 * TLObserver can consume significant amounts of memory if accessed from many
 * threads. The problem is exacerbated if you chain several TLObservers.
 * Therefore, TLObserver should be used sparingly.
 *
 *   Observer<int> observer = ...;
 *   TLObserver<int> tlObserver(observer);
 *   auto& snapshot = *tlObserver;
 */
template <typename T>
class TLObserver;

/**
 * A ReadMostlyAtomicObserver guarantees that reading is exactly one relaxed
 * atomic load and a read from a thread local bool. Like AtomicObserver, the
 * value is cached using `std::atomic`.  However, there is no version check when
 * reading which means that the cached value may be out-of-date with the
 * Observer value. The cached value will be updated asynchronously in a
 * background thread.
 *
 * When get() is called from makeObserver, the underlying observer is directly
 * snapshotted to ensure dependent observers have current values and capture
 * dependencies.
 *
 * ReadMostlyAtomicObserver is ideal for fastest possible reads on a
 * trivially-copyable type when a slightly out-of-date value will suffice. It is
 * perfect for very frequent reads coupled with very infrequent writes.
 *
 *   Observer<int> observer = ...;
 *   ReadMostlyAtomicObserver<int> atomicObserver(observer);
 *   auto value = *atomicObserver;
 */
template <typename T>
class ReadMostlyAtomicObserver;

/// A stable view of an observed value at a point in time.
template <typename T>
class Snapshot {
 public:
  /// Returns a reference to the observed value.
  ///
  /// \returns A reference to the observed value.
  const T& operator*() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return *get();
  }

  /**
   * Never returns nullptr
   *
   * \returns A pointer to the observed value.
   */
  const T* operator->() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return get();
  }

  /**
   * Never returns nullptr
   *
   * \returns A pointer to the observed value.
   */
  const T* get() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return data_.get();
  }

  /**
   * Never returns nullptr
   *
   * \returns A shared pointer to the observed value.
   */
  std::shared_ptr<const T> getShared() const& { return data_; }

  /**
   * Never returns nullptr
   *
   * \returns A shared pointer to the observed value.
   */
  std::shared_ptr<const T> getShared() && { return std::move(data_); }

  /**
   * Return the version of the observed object.
   *
   * \returns The version number of the observed object.
   */
  size_t getVersion() const { return version_; }

  /**
   * Return the time at which the observed object was created.
   *
   * \returns The creation time of the observed object.
   */
  std::chrono::system_clock::time_point getTimeCreated() const {
    return timeCreated_;
  }

 private:
  friend class Observer<T>;

  using TimePoint = observer_detail::Core::VersionedData::TimePoint;
  Snapshot(
      const observer_detail::Core& core,
      std::shared_ptr<const T> data,
      size_t version,
      TimePoint timeCreated)
      : data_(std::move(data)),
        version_(version),
        timeCreated_(timeCreated),
        core_(&core) {
    DCHECK(data_);
  }

  std::shared_ptr<const T> data_;
  size_t version_;
  TimePoint timeCreated_;
  const observer_detail::Core* core_;
};

/// Owns a callback subscription to an Observer.
class CallbackHandle {
 public:
  /// Constructs an empty handle with no callback attached.
  CallbackHandle();
  /// Constructs a handle subscribing a callback to an Observer.
  ///
  /// \param observer The Observer to subscribe to.
  /// \param callback Function invoked with a snapshot on each update.
  template <typename T>
  CallbackHandle(Observer<T> observer, Function<void(Snapshot<T>)> callback);
  /// Deleted copy constructor.
  ///
  /// \param other The handle that would be copied.
  CallbackHandle(const CallbackHandle& other) = delete;
  /// Move constructor.
  ///
  /// \param other The handle to move from.
  CallbackHandle(CallbackHandle&& other) = default;
  /// Deleted copy assignment operator.
  ///
  /// \param other The handle that would be copied.
  /// \returns A reference to this handle.
  CallbackHandle& operator=(const CallbackHandle& other) = delete;
  /// Move assignment operator.
  ///
  /// \param other The handle to move from.
  /// \returns A reference to this handle.
  CallbackHandle& operator=(CallbackHandle&& other) noexcept;
  /// Destroys the handle, cancelling the subscription.
  ~CallbackHandle();

  /// Cancels the subscription.
  ///
  /// If callback is currently running, waits until it completes.
  /// Callback will never be called after cancel() returns.
  void cancel();

 private:
  struct Context;
  std::shared_ptr<Context> context_;
};

template <typename Observable, typename Traits>
class ObserverCreator;

template <typename T>
class Observer {
 public:
  /// Constructs an Observer wrapping the given core.
  ///
  /// \param core The shared core holding the observed state.
  explicit Observer(observer_detail::Core::Ptr core);

  /**
   * Never throws or blocks. Never returns an empty snapshot.
   * Prefer with() for short-lived access to avoid read-after-free bugs.
   *
   * \returns A snapshot of the current observed value.
   */
  Snapshot<T> getSnapshot() const noexcept;
  /// Returns a snapshot of the current observed value.
  ///
  /// \returns A snapshot of the current observed value.
  Snapshot<T> operator*() const noexcept { return getSnapshot(); }

  /**
   * Invoke a function with the current observed value. The snapshot is held
   * alive for the duration of the call, preventing read-after-free when
   * accessing members of the observed object.
   *
   * The return type is decayed to prevent accidentally returning a reference
   * into the snapshot's data, which would dangle after the snapshot is
   * destroyed.
   *
   * Example:
   *   observer.with([](const auto& cfg) {
   *     return cfg.getSomeValue();
   *   });
   *
   * \param f Function invoked with the current observed value.
   * \returns The decayed result of invoking f.
   */
  template <typename F>
  std::decay_t<std::invoke_result_t<F, const T&>> with(F&& f) const
      noexcept(noexcept(static_cast<F&&>(f)(std::declval<const T&>()))) {
    auto snapshot = getSnapshot();
    return static_cast<F&&>(f)(*snapshot);
  }

  /**
   * Check if we have a newer version of the observed object than the snapshot.
   * Snapshot should have been originally from this Observer.
   *
   * \param snapshot A snapshot previously obtained from this Observer.
   * \returns True if a newer version is available.
   */
  bool needRefresh(const Snapshot<T>& snapshot) const {
    DCHECK_EQ(core_.get(), snapshot.core_);
    return needRefresh(snapshot.getVersion());
  }

  /// Checks whether a newer version than the given one is available.
  ///
  /// \param version The version to compare against.
  /// \returns True if a newer version is available.
  bool needRefresh(size_t version) const {
    return version < core_->getVersionLastChange();
  }

  /**
   * Add a callback to be called when the Observer is updated. The callback
   * will be removed when the returned CallbackHandle is destroyed.
   *
   * \param callback Function invoked with a snapshot on each update.
   * \returns A handle that removes the callback when destroyed.
   */
  [[nodiscard]] CallbackHandle addCallback(
      Function<void(Snapshot<T>)> callback) const;

  /// Returns type info for the creator functor.
  ///
  /// \returns Pointer to the creator's type info.
  const std::type_info* getCreatorTypeInfo() const {
    return core_->getCreatorContext().typeInfo;
  }

  /// Returns type info for the creator's invoke result.
  ///
  /// \returns Pointer to the creator's invoke result type info.
  const std::type_info* getCreatorInvokeResultTypeInfo() const {
    return core_->getCreatorContext().invokeResultTypeInfo;
  }

  /// Returns the creator's name.
  ///
  /// \returns The name associated with the creator.
  const std::string& getCreatorName() const {
    return core_->getCreatorContext().name;
  }

  /// Returns the underlying core.
  ///
  /// \returns A reference to the underlying core.
  folly::observer_detail::Core& getCore() const { return *core_; }

 private:
  template <typename Observable, typename Traits>
  friend class ObserverCreator;

  observer_detail::Core::Ptr core_;
};

/// Returns the observer unchanged.
///
/// \param observer The Observer to unwrap.
/// \returns The same Observer.
template <typename T>
Observer<T> unwrap(Observer<T> observer);

/// Returns the observer unchanged.
///
/// \param observer The Observer to unwrap.
/// \returns The same Observer.
template <typename T>
Observer<T> unwrapValue(Observer<T> observer);

/// Flattens a nested Observer into a single Observer.
///
/// \param observer The nested Observer to unwrap.
/// \returns An Observer tracking the inner Observer.
template <typename T>
Observer<T> unwrap(Observer<Observer<T>> observer);

/// Flattens a nested Observer into a single value Observer.
///
/// \param observer The nested Observer to unwrap.
/// \returns A value Observer tracking the inner Observer.
template <typename T>
Observer<T> unwrapValue(Observer<Observer<T>> observer);

/**
 * makeObserver(...) creates a new Observer<T> object given a functor to
 * compute it. The functor can return T or std::shared_ptr<const T>.
 *
 * makeObserver(...) blocks until the initial version of Observer is computed.
 * If creator functor fails (throws or returns a nullptr) during this first
 * call, the exception is re-thrown by makeObserver(...).
 *
 * For all subsequent updates if creator functor fails (throws or returs a
 * nullptr), the Observer (and all its dependents) is not updated.
 *
 * \param creator Functor computing the observed value.
 * \returns An Observer that recomputes its value when dependencies change.
 */
template <typename F>
Observer<observer_detail::ResultOf<F>> makeObserver(F&& creator);

/// Creates an Observer from a creator returning a shared pointer.
///
/// \param creator Functor computing the observed value.
/// \returns An Observer that recomputes its value when dependencies change.
template <typename F>
Observer<observer_detail::ResultOfUnwrapSharedPtr<F>> makeObserver(F&& creator);

/// Creates an Observer from a creator returning an Observer.
///
/// \param creator Functor computing the observed value.
/// \returns An Observer that recomputes its value when dependencies change.
template <typename F>
Observer<observer_detail::ResultOfUnwrapObserver<F>> makeObserver(F&& creator);

/**
 * The returned Observer will proxy updates from the input observer, but will
 * skip updates that contain the same (according to operator==) value even if
 * the actual object in the update is different.
 *
 * \param observer The Observer whose updates are proxied.
 * \returns An Observer that only updates when the value changes.
 */
template <typename T>
Observer<T> makeValueObserver(Observer<T> observer);

/**
 * A more efficient short-cut for makeValueObserver(makeObserver(...)).
 *
 * \param creator Functor computing the observed value.
 * \returns A value Observer wrapping the created Observer.
 */
template <typename F>
Observer<observer_detail::ResultOf<F>> makeValueObserver(F&& creator);

/// Creates a value Observer from a creator returning a shared pointer.
///
/// \param creator Functor computing the observed value.
/// \returns A value Observer wrapping the created Observer.
template <typename F>
Observer<observer_detail::ResultOfUnwrapSharedPtr<F>> makeValueObserver(
    F&& creator);

/**
 * The returned Observer will never update and always return the passed value.
 *
 * \param value The fixed value the Observer will always return.
 * \returns An Observer that always yields value.
 */
template <typename T>
Observer<T> makeStaticObserver(T value);

/// Creates a static Observer holding a shared pointer value.
///
/// \param value The fixed shared pointer the Observer will always return.
/// \returns An Observer that always yields value.
template <typename T>
Observer<std::decay_t<T>> makeStaticObserver(std::shared_ptr<T> value);

template <typename T>
class AtomicObserver {
 public:
  /// Constructs an AtomicObserver caching the given Observer.
  ///
  /// \param observer The underlying Observer to cache.
  explicit AtomicObserver(Observer<T> observer);
  /// Copy constructor; invalidates the cache.
  ///
  /// \param other The AtomicObserver to copy from.
  AtomicObserver(const AtomicObserver<T>& other);
  /// Move constructor; invalidates the cache.
  ///
  /// \param other The AtomicObserver to move from.
  AtomicObserver(AtomicObserver<T>&& other) noexcept;
  /// Copy assignment operator; invalidates the cache.
  ///
  /// \param other The AtomicObserver to copy from.
  /// \returns A reference to this AtomicObserver.
  AtomicObserver<T>& operator=(const AtomicObserver<T>& other);
  /// Move assignment operator; invalidates the cache.
  ///
  /// \param other The AtomicObserver to move from.
  /// \returns A reference to this AtomicObserver.
  AtomicObserver<T>& operator=(AtomicObserver<T>&& other) noexcept;
  /// Rebinds this AtomicObserver to a new underlying Observer.
  ///
  /// \param observer The Observer to cache.
  /// \returns A reference to this AtomicObserver.
  AtomicObserver<T>& operator=(Observer<T> observer);

  /// Returns the cached value.
  ///
  /// \returns The current observed value.
  T get() const;
  /// Returns the cached value.
  ///
  /// \returns The current observed value.
  T operator*() const { return get(); }

  /// Returns the underlying Observer.
  ///
  /// \returns A reference to the wrapped Observer.
  const Observer<T>& getUnderlyingObserver() const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return observer_;
  }

 private:
  mutable std::atomic<T> cachedValue_{};
  mutable std::atomic<size_t> cachedVersion_{};
  mutable SharedMutex refreshLock_;
  Observer<T> observer_;
};

template <typename T>
class TLObserver {
 public:
  /// Constructs a TLObserver caching the given Observer.
  ///
  /// \param observer The underlying Observer to cache.
  explicit TLObserver(Observer<T> observer);
  /// Copy constructor.
  ///
  /// \param other The TLObserver to copy from.
  TLObserver(const TLObserver<T>& other);
  /// Move constructor.
  ///
  /// \param other The TLObserver to move from.
  TLObserver(TLObserver<T>&& other) noexcept;

  /// Returns a reference to the thread-local snapshot.
  ///
  /// \returns A reference to the current snapshot.
  const Snapshot<T>& getSnapshotRef() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]];
  /// Returns a reference to the thread-local snapshot.
  ///
  /// \returns A reference to the current snapshot.
  const Snapshot<T>& operator*() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return getSnapshotRef();
  }

  /**
   * Invoke a function with the current observed value. The snapshot is held
   * alive for the duration of the call, preventing read-after-free when
   * accessing members of the observed object.
   *
   * The return type is decayed to prevent accidentally returning a reference
   * into the snapshot's data, which would dangle after the snapshot is
   * destroyed.
   *
   * See Observer::with() for semantics.
   *
   * \param f Function invoked with the current observed value.
   * \returns The decayed result of invoking f.
   */
  template <typename F>
  std::decay_t<std::invoke_result_t<F, const T&>> with(F&& f) const
      noexcept(noexcept(static_cast<F&&>(f)(std::declval<const T&>()))) {
    const auto& snapshot = getSnapshotRef();
    return static_cast<F&&>(f)(*snapshot);
  }

  /// Returns the underlying Observer.
  ///
  /// \returns A reference to the wrapped Observer.
  const Observer<T>& getUnderlyingObserver() const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return observer_;
  }

 private:
  Observer<T> observer_;
  mutable ThreadLocalPtr<Snapshot<T>> snapshot_;
};

template <typename T>
class ReadMostlyAtomicObserver {
 public:
  /// Constructs a ReadMostlyAtomicObserver caching the given Observer.
  ///
  /// \param observer The underlying Observer to cache.
  explicit ReadMostlyAtomicObserver(Observer<T> observer);
  /// Deleted copy constructor.
  ///
  /// \param other The observer that would be copied.
  ReadMostlyAtomicObserver(const ReadMostlyAtomicObserver<T>& other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param other The observer that would be copied.
  /// \returns A reference to this observer.
  ReadMostlyAtomicObserver<T>& operator=(
      const ReadMostlyAtomicObserver<T>& other) = delete;

  /// Returns the cached value.
  ///
  /// \returns The most recently cached observed value.
  T get() const;
  /// Returns the cached value.
  ///
  /// \returns The most recently cached observed value.
  T operator*() const { return get(); }

  /// Returns the underlying Observer.
  ///
  /// \returns A reference to the wrapped Observer.
  const Observer<T>& getUnderlyingObserver() const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return observer_;
  }

 private:
  Observer<T> observer_;
  std::atomic<T> cachedValue_{};
  CallbackHandle callback_;
};

/**
 * Same as makeObserver(...), but creates AtomicObserver.
 *
 * \param observer The Observer to wrap.
 * \returns An AtomicObserver wrapping the given Observer.
 */
template <typename T>
AtomicObserver<T> makeAtomicObserver(Observer<T> observer) {
  return AtomicObserver<T>(std::move(observer));
}

/// Creates an AtomicObserver from a creator functor.
///
/// \param creator Functor computing the observed value.
/// \returns An AtomicObserver wrapping the created Observer.
template <typename F>
auto makeAtomicObserver(F&& creator) {
  return makeAtomicObserver(makeObserver(std::forward<F>(creator)));
}

/**
 * Same as makeObserver(...), but creates TLObserver.
 *
 * \param observer The Observer to wrap.
 * \returns A TLObserver wrapping the given Observer.
 */
template <typename T>
TLObserver<T> makeTLObserver(Observer<T> observer) {
  return TLObserver<T>(std::move(observer));
}

/// Creates a TLObserver from a creator functor.
///
/// \param creator Functor computing the observed value.
/// \returns A TLObserver wrapping the created Observer.
template <typename F>
auto makeTLObserver(F&& creator) {
  return makeTLObserver(makeObserver(std::forward<F>(creator)));
}

/**
 * Same as makeObserver(...), but creates ReadMostlyAtomicObserver.
 *
 * \param observer The Observer to wrap.
 * \returns A ReadMostlyAtomicObserver wrapping the given Observer.
 */
template <typename T>
ReadMostlyAtomicObserver<T> makeReadMostlyAtomicObserver(Observer<T> observer) {
  return ReadMostlyAtomicObserver<T>(std::move(observer));
}

/// Creates a ReadMostlyAtomicObserver from a creator functor.
///
/// \param creator Functor computing the observed value.
/// \returns A ReadMostlyAtomicObserver wrapping the created Observer.
template <typename F>
auto makeReadMostlyAtomicObserver(F&& creator) {
  return makeReadMostlyAtomicObserver(makeObserver(std::forward<F>(creator)));
}

/// Maps a value type to the observer type used to cache it.
template <typename T, bool CacheInThreadLocal>
struct ObserverTraits {};

template <typename T>
struct ObserverTraits<T, false> {
  using type = Observer<T>;
};

template <typename T>
struct ObserverTraits<T, true> {
  using type = TLObserver<T>;
};

/// Alias for the observer type selected by ObserverTraits.
template <typename T, bool CacheInThreadLocal>
using ObserverT = typename ObserverTraits<T, CacheInThreadLocal>::type;
} // namespace observer
} // namespace folly

#include <folly/observer/Observer-inl.h>
