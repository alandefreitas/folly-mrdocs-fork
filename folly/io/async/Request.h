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
#include <string>
#include <type_traits>
#include <utility>

#include <folly/SharedMutex.h>
#include <folly/SingletonThreadLocal.h>
#include <folly/Synchronized.h>
#include <folly/concurrency/ProcessLocalUniqueId.h>
#include <folly/container/F14Map.h>
#include <folly/detail/Iterators.h>
#include <folly/synchronization/Hazptr.h>

/// Facebook Folly library namespace.
namespace folly {

/// A token used to fetch data from a RequestContext.
///
/// Generally you will want this to be a static, created only once using a
/// string, and then only copied. The string constructor is expensive.
class RequestToken {
 public:
  /// Constructs an empty token.
  RequestToken() = default;
  /// Constructs a token from a string identifier.
  ///
  /// \param str The string identifier for the token.
  explicit RequestToken(const std::string& str);

  /// Compares two tokens for equality.
  ///
  /// \param other The token to compare against.
  /// \returns True if both tokens refer to the same identifier.
  bool operator==(const RequestToken& other) const {
    return token_ == other.token_;
  }

  /// Returns a human-readable representation of the token.
  ///
  /// Slow, use only for debug log messages.
  ///
  /// \returns The string identifier associated with the token.
  std::string getDebugString() const;

  friend struct std::hash<folly::RequestToken>;

 private:
  static Synchronized<F14FastMap<std::string, uint32_t>>& getCache();

  uint32_t token_;
};
static_assert(
    std::is_trivially_destructible<RequestToken>::value,
    "must be trivially destructible");

} // namespace folly

/// C++ standard library namespace.
namespace std {
/// Hash specialization for folly::RequestToken.
template <>
struct hash<folly::RequestToken> {
  /// Computes the hash of a token.
  ///
  /// \param token The token to hash.
  /// \returns The hash value of the token.
  size_t operator()(const folly::RequestToken& token) const {
    return hash<uint32_t>()(token.token_);
  }
};
} // namespace std

namespace folly {

// Some request context that follows an async request through a process
// Everything in the context must be thread safe

/// Base class for data that follows an async request through a process.
///
/// Everything in the context must be thread safe.
class RequestData {
 public:
  /// Destroys the request data.
  virtual ~RequestData() = default;

  // Avoid calling RequestContext::setContextData, setContextDataIfAbsent, or
  // clearContextData from these callbacks. Doing so will cause deadlock. We
  // could fix these deadlocks, but only at significant performance penalty, so
  // just don't do it!

  /// Returns whether this instance has set/unset callbacks.
  ///
  /// hasCallback() applies only to onSet() and onUnset().
  /// onClear() is always executed exactly once.
  ///
  /// \returns True if onSet() and onUnset() should be invoked.
  virtual bool hasCallback() = 0;
  /// Callback executed when setting the RequestContext.
  ///
  /// Make sure your RequestData instance overrides the hasCallback method to
  /// return true otherwise the callback will not be executed.
  virtual void onSet() {}
  /// Callback executed when unsetting the RequestContext.
  ///
  /// Make sure your RequestData instance overrides the hasCallback method to
  /// return true otherwise the callback will not be executed.
  virtual void onUnset() {}
  /// Callback executed exactly once when the last reference is released.
  ///
  /// Executed upon the release of the last reference to the request data (as a
  /// result of either a call to clearContextData or the destruction of a
  /// request context that contains a reference to the data). It can be
  /// overridden in derived classes. There may be concurrent executions of
  /// onSet() and onUnset() with that of onClear().
  virtual void onClear() {}
  /// Returns the current reference count, for debugging.
  ///
  /// \returns The number of live references to this data.
  int refCount() { return keepAliveCounter_.load(std::memory_order_acquire); }

 private:
  // For efficiency, RequestContext provides a raw ptr interface.
  // To support shallow copy, we need a shared ptr.
  // To keep it as safe as possible (even if a raw ptr is passed back),
  // the counter lives directly in RequestData.

  friend class RequestContext;

  static constexpr int kDeleteCount = 0x1;
  static constexpr int kClearCount = 0x1000;
  static constexpr int kClearDeleteCounts = kClearCount + kDeleteCount;

  // Reference-counting functions.
  // Increment the reference count.
  void acquireRef();
  // Decrement the reference count. Clear only if last.
  void releaseRefClearOnly();
  // Decrement the reference count. Delete only if last.
  void releaseRefDeleteOnly();
  // Decrement the reference count. Clear and delete if last.
  void releaseRefClearDelete();
  void releaseRefClearDeleteSlow();

  std::atomic<int> keepAliveCounter_{0};
};

/**
 * ImmutableRequestData is a folly::RequestData that holds an immutable value.
 * It is thread-safe (a requirement of RequestData) because it is immutable.
 */
template <typename T>
class ImmutableRequestData : public folly::RequestData {
 public:
  /// Constructs the immutable value in place.
  ///
  /// \param args Arguments forwarded to the value constructor.
  template <
      typename... Args,
      typename = typename std::enable_if<
          std::is_constructible<T, Args...>::value>::type>
  explicit ImmutableRequestData(Args&&... args) noexcept(
      std::is_nothrow_constructible<T, Args...>::value)
      : val_(std::forward<Args>(args)...) {}

  /// Returns the held immutable value.
  ///
  /// \returns A reference to the stored value.
  const T& value() const { return val_; }

  /// Returns whether this instance has set/unset callbacks.
  ///
  /// \returns Always false, since immutable data needs no callbacks.
  bool hasCallback() override { return false; }

 private:
  const T val_;
};

/// A token/data pair used to populate a RequestContext.
using RequestDataItem = std::pair<RequestToken, std::unique_ptr<RequestData>>;

/// Holds data that follows an async request across queues and threads.
class RequestContext {
 public:
  /// Constructs an empty request context.
  RequestContext();
  /// Move construction is disabled.
  RequestContext(RequestContext&& ctx) = delete;
  /// Copy assignment is disabled.
  RequestContext& operator=(const RequestContext& other) = delete;
  /// Move assignment is disabled.
  RequestContext& operator=(RequestContext&& other) = delete;

  /// Copies a context as a new root context.
  ///
  /// The copy constructor is disabled, use copyAsRoot/copyAsChild instead.
  ///
  /// \param ctx The context to copy.
  /// \param rootid The root id for the new context.
  /// \returns The newly created root context.
  static std::shared_ptr<RequestContext> copyAsRoot(
      const RequestContext& ctx, intptr_t rootid);
  /// Copies a context as a child of the given one.
  ///
  /// \param ctx The context to copy.
  /// \returns The newly created child context.
  static std::shared_ptr<RequestContext> copyAsChild(const RequestContext& ctx);

  /// Creates a unique request context and sets it as the current context.
  ///
  /// It will be propagated between queues / threads (where implemented).
  ///
  /// Whenever possible, prefer RequestContextScopeGuard instead of create() to
  /// make sure that RequestContext is reset to the original value when we exit
  /// the scope.
  ///
  /// \returns The previous request context.
  static std::shared_ptr<RequestContext> create() {
    return setContext(std::make_shared<RequestContext>());
  }

  /// Returns the current context, or the default global request context.
  ///
  /// NOTE: This is a legacy method: there is almost never a good reason to use
  /// the default global request context. Prefer try_get() for new code.
  ///
  /// \returns The current context if set, otherwise the global default.
  static RequestContext* get();

  /// Returns the current context, if it has already been set, or nullptr.
  ///
  /// \returns The current context, or nullptr if none is set.
  static RequestContext* try_get();

  /// Returns the root id of this context.
  ///
  /// \returns The root id value.
  intptr_t getRootId() const { return rootId_; }

  /// Describes the root id associated with a thread.
  struct RootIdInfo {
    intptr_t id; ///< The root id value.
    std::thread::id tid; ///< The C++ thread id.
    uint64_t tidOS; ///< The OS-level thread id.
  };
  /// Collects the root id information from all threads.
  ///
  /// \returns The root id info for every thread with a context.
  static std::vector<RootIdInfo> getRootIdsFromAllThreads();

  // The following APIs are used to add, remove and access RequestData instance
  // in the RequestContext instance, normally used for per-RequestContext
  // tracking or callback on set and unset. These APIs are Thread-safe.
  // These APIs are performance sensitive, so please ask if you need help
  // profiling any use of these APIs.

  /// Adds a RequestData instance to this context under the given token.
  ///
  /// If the same identifier has already been used, will print a warning message
  /// for the first time, clear the existing RequestData instance, and **not**
  /// add the new data.
  ///
  /// \param token The identifier for the data.
  /// \param data The data to add.
  void setContextData(
      const RequestToken& token, std::unique_ptr<RequestData> data);
  /// Adds a RequestData instance to this context under the given identifier.
  ///
  /// \param val The string identifier for the data.
  /// \param data The data to add.
  void setContextData(
      const std::string& val, std::unique_ptr<RequestData> data) {
    setContextData(RequestToken(val), std::move(data));
  }

  /// Adds a RequestData instance only if the token is not already present.
  ///
  /// \param token The identifier for the data.
  /// \param data The data to add.
  /// \returns True if the data was added, false if the token already existed.
  bool setContextDataIfAbsent(
      const RequestToken& token, std::unique_ptr<RequestData> data);
  /// Adds a RequestData instance only if the identifier is not already present.
  ///
  /// \param val The string identifier for the data.
  /// \param data The data to add.
  /// \returns True if the data was added, false if the identifier existed.
  bool setContextDataIfAbsent(
      const std::string& val, std::unique_ptr<RequestData> data) {
    return setContextDataIfAbsent(RequestToken(val), std::move(data));
  }

  /// Removes the RequestData instance with the given token, if it exists.
  ///
  /// \param val The identifier of the data to remove.
  void clearContextData(const RequestToken& val);
  /// Removes the RequestData instance with the given identifier, if it exists.
  ///
  /// \param val The string identifier of the data to remove.
  void clearContextData(const std::string& val) {
    clearContextData(RequestToken(val));
  }

  /// Returns whether a RequestData instance exists for the given token.
  ///
  /// \param val The identifier to look up.
  /// \returns True if data exists for the identifier.
  bool hasContextData(const RequestToken& val) const;
  /// Returns whether a RequestData instance exists for the given identifier.
  ///
  /// \param val The string identifier to look up.
  /// \returns True if data exists for the identifier.
  bool hasContextData(const std::string& val) const {
    return hasContextData(RequestToken(val));
  }

  /// Returns a raw pointer to the RequestData instance for the given token.
  ///
  /// \param val The identifier to look up.
  /// \returns The data pointer, or null if not present.
  RequestData* getContextData(const RequestToken& val);
  /// Returns a const raw pointer to the RequestData instance for the token.
  ///
  /// \param val The identifier to look up.
  /// \returns The data pointer, or null if not present.
  const RequestData* getContextData(const RequestToken& val) const;
  /// Returns a raw pointer to the RequestData instance for the identifier.
  ///
  /// \param val The string identifier to look up.
  /// \returns The data pointer, or null if not present.
  RequestData* getContextData(const std::string& val) {
    return getContextData(RequestToken(val));
  }
  /// Returns a const raw pointer to the RequestData for the identifier.
  ///
  /// \param val The string identifier to look up.
  /// \returns The data pointer, or null if not present.
  const RequestData* getContextData(const std::string& val) const {
    return getContextData(RequestToken(val));
  }

  // Same as getContextData(), but caching the RequestData pointer in
  // thread-local storage to avoid the lookup cost. The thread cache is
  // invalidated if the current request context changes or it gets modified.
  //
  // This can be used for RequestData that are queried very frequently. It
  // should almost always be faster than getContextData(), but it consumes
  // thread-local storage space, so it is worth doing only when a high hit rate
  // is expected.
  //
  // The storage for the caches is associated to a type tag passed as template
  // argument. This tag also contains the RequestToken key as a static member
  // kToken. This guarantees that a given tag cannot be accidentally used with
  // multiple tokens.
  //
  // For example:
  //
  // struct MyRequestDataTraits {
  //   static inline const RequestToken kToken{"my_request_data"};
  // };
  //
  // ...
  // auto* data = ctx->getThreadCachedContextData<MyRequestDataTraits>();
  //
  /// Returns cached context data for the token, keyed by a type tag.
  ///
  /// Same as getContextData(), but caching the RequestData pointer in
  /// thread-local storage to avoid the lookup cost.
  ///
  /// \returns The data pointer, or null if not present.
  template <class Traits>
  RequestData* getThreadCachedContextData();

  /// Invokes the onSet() callbacks of all held RequestData instances.
  void onSet();
  /// Invokes the onUnset() callbacks of all held RequestData instances.
  void onUnset();

  /// Sets the given context as current and returns the previous one.
  ///
  /// This API is used to pass the context through queues / threads. saveContext
  /// is called to get a shared_ptr to the context, and setContext is used to
  /// reset it on the other side of the queue.
  ///
  /// Whenever possible, prefer RequestContextScopeGuard instead of setContext to
  /// make sure that RequestContext is reset to the original value when we exit
  /// the scope.
  ///
  /// \param ctx The context to set as current.
  /// \returns The previous request context.
  static std::shared_ptr<RequestContext> setContext(
      std::shared_ptr<RequestContext> const& ctx);
  /// Sets the given context as current and returns the previous one.
  ///
  /// \param newCtx_ The context to set as current.
  /// \returns The previous request context.
  static std::shared_ptr<RequestContext> setContext(
      std::shared_ptr<RequestContext>&& newCtx_);

  /// Signature for watcher callbacks invoked after a setContext.
  ///
  /// These watcher functions are called after a setContext with the previous
  /// and current context.
  using SetContextWatcherSig = void(
      const std::shared_ptr<RequestContext>& prev,
      const std::shared_ptr<RequestContext>& ctx);
  /// Registers a watcher invoked on every context change.
  ///
  /// \param func The watcher callback to register.
  static void addSetContextWatcher(SetContextWatcherSig& func);

 private:
  struct SetContextWatcherRegistry {
    using Sig = SetContextWatcherSig;

    struct Watcher {
      Sig* func_;
      Watcher* next_{nullptr};

      explicit Watcher(Sig& func) : func_(&func) {}
    };

    std::atomic<Watcher*> watchers_{nullptr};

    void addWatcher(Sig& func) {
      auto watcherPtr = new Watcher(func);
      auto* head = watchers_.load(std::memory_order_relaxed);
      do {
        watcherPtr->next_ = head;
      } while (!watchers_.compare_exchange_weak(
          head,
          watcherPtr,
          std::memory_order_acq_rel,
          std::memory_order_relaxed));
    }

    void invokeWatchers(
        const std::shared_ptr<RequestContext>& prev,
        const std::shared_ptr<RequestContext>& ctx) {
      auto* watcher = watchers_.load(std::memory_order_acquire);
      while (watcher != nullptr) {
        watcher->func_(prev, ctx);
        watcher = watcher->next_;
      }
    }

    ~SetContextWatcherRegistry() {
      auto* watcher = watchers_.exchange(nullptr, std::memory_order_acquire);
      while (watcher != nullptr) {
        delete std::exchange(watcher, watcher->next_);
      }
    }
  };

  static SetContextWatcherRegistry& getWatcherRegistry();

 public:
  /// Returns a shared pointer to the current context.
  ///
  /// \returns The current request context.
  static std::shared_ptr<RequestContext> saveContext();

 private:
  struct Tag {};
  RequestContext(const RequestContext& ctx) = default;

 public:
  /// Constructs a copy of a context with a new root id.
  ///
  /// \param ctx The context to copy.
  /// \param rootid The root id for the new context.
  /// \param tag Disambiguation tag.
  RequestContext(const RequestContext& ctx, intptr_t rootid, Tag tag);
  /// Constructs a copy of a context, keeping its root id.
  ///
  /// \param ctx The context to copy.
  /// \param tag Disambiguation tag.
  RequestContext(const RequestContext& ctx, Tag tag);
  /// Constructs a context with the given root id.
  ///
  /// \param rootId The root id for the new context.
  explicit RequestContext(intptr_t rootId);

  /// Thread-local holder of the current request context.
  struct StaticContext {
    /// Constructs an empty static context.
    StaticContext() = default;
    /// Destroys the static context.
    ~StaticContext();

    /// Copy construction is disabled.
    StaticContext(const StaticContext& other) = delete;
    /// Move construction is disabled.
    StaticContext(StaticContext&& other) = delete;
    /// Copy assignment is disabled.
    StaticContext& operator=(const StaticContext& other) = delete;
    /// Move assignment is disabled.
    StaticContext& operator=(StaticContext&& other) = delete;

    std::shared_ptr<RequestContext> requestContext; ///< The current context.
    std::atomic<intptr_t> rootId{0}; ///< The current root id.
  };

 private:
  static StaticContext& getStaticContext();
  static StaticContext* tryGetStaticContext();
  static std::shared_ptr<RequestContext> setContextHelper(
      std::shared_ptr<RequestContext>& newCtx, StaticContext& staticCtx);

  using StaticContextThreadLocal = SingletonThreadLocal<
      RequestContext::StaticContext,
      RequestContext /* Tag */>;

 public:
  /// Accessor that iterates the StaticContext of every thread.
  class StaticContextAccessor {
   private:
    using Inner = StaticContextThreadLocal::Accessor;
    using IteratorBase = Inner::Iterator;
    using IteratorTag = typename IteratorBase::iterator_category;

    Inner inner_;

    explicit StaticContextAccessor(Inner&& inner) noexcept
        : inner_(std::move(inner)) {}

   public:
    friend class RequestContext;

    /// Forward iterator over per-thread StaticContext objects.
    class Iterator
        : public detail::IteratorAdaptor<
              Iterator,
              IteratorBase,
              StaticContext,
              IteratorTag> {
      using Super = detail::
          IteratorAdaptor<Iterator, IteratorBase, StaticContext, IteratorTag>;

     public:
      /// Inherits the base iterator constructors.
      using Super::Super;

      /// Dereferences the iterator to the current StaticContext.
      ///
      /// \returns A reference to the current StaticContext.
      StaticContext& dereference() const { return *base(); }

      /// Returns the root id information for the current thread.
      ///
      /// \returns The root id info of the pointed-to context.
      RootIdInfo getRootIdInfo() const {
        return {
            base()->rootId.load(std::memory_order_relaxed),
            base().getThreadId(),
            base().getOSThreadId()};
      }
    };

    /// Copy construction is disabled.
    StaticContextAccessor(const StaticContextAccessor& other) = delete;
    /// Copy assignment is disabled.
    StaticContextAccessor& operator=(const StaticContextAccessor& other) =
        delete;
    /// Move construction is defaulted.
    ///
    /// \param other The accessor to move from.
    StaticContextAccessor(StaticContextAccessor&& other) = default;
    /// Move assignment is defaulted.
    ///
    /// \param other The accessor to move from.
    /// \returns A reference to this accessor.
    StaticContextAccessor& operator=(StaticContextAccessor&& other) = default;

    /// Returns an iterator to the first thread context.
    ///
    /// \returns An iterator to the beginning of the range.
    Iterator begin() const { return Iterator(inner_.begin()); }
    /// Returns an iterator past the last thread context.
    ///
    /// \returns An iterator to the end of the range.
    Iterator end() const { return Iterator(inner_.end()); }
  };
  /// Returns an accessor that pins the StaticContext of all threads.
  ///
  /// The accessor blocks the construction and destruction of StaticContext
  /// objects on all threads. This is useful to quickly introspect the context
  /// from all threads while ensuring that their thread-local StaticContext
  /// object is not destroyed.
  ///
  /// \returns An accessor over every thread's StaticContext.
  static StaticContextAccessor accessAllThreads();

  // Start shallow copy guard implementation details:
  // All methods are private to encourage proper use
  friend struct ShallowCopyRequestContextScopeGuard;

  /// Sets a shallow copy of the current context as current.
  ///
  /// \returns The previous context, so it can be reset later.
  static std::shared_ptr<RequestContext> setShallowCopyContext();

  /// Replaces the RequestData instance stored under the given token.
  ///
  /// For functions with a parameter safe, if safe is true then the caller
  /// guarantees that there are no concurrent readers or writers accessing the
  /// structure.
  ///
  /// \param token The identifier for the data.
  /// \param data The data to store.
  /// \param safe Whether the caller guarantees no concurrent access.
  void overwriteContextData(
      const RequestToken& token,
      std::unique_ptr<RequestData> data,
      bool safe = false);
  /// Replaces the RequestData instance stored under the given identifier.
  ///
  /// \param val The string identifier for the data.
  /// \param data The data to store.
  /// \param safe Whether the caller guarantees no concurrent access.
  void overwriteContextData(
      const std::string& val,
      std::unique_ptr<RequestData> data,
      bool safe = false) {
    overwriteContextData(RequestToken(val), std::move(data), safe);
  }

  /// Selects the behavior of doSetContextDataHelper.
  enum class DoSetBehaviour {
    SET, ///< Set the data, replacing any existing entry.
    SET_IF_ABSENT, ///< Set the data only if absent.
    OVERWRITE, ///< Overwrite the existing data.
  };

  /// Applies the requested set behavior for the given token.
  ///
  /// \param token The identifier for the data.
  /// \param data The data to store.
  /// \param behaviour Whether to set, set-if-absent, or overwrite.
  /// \param safe Whether the caller guarantees no concurrent access.
  /// \returns True if the data was changed.
  bool doSetContextDataHelper(
      const RequestToken& token,
      std::unique_ptr<RequestData>& data,
      DoSetBehaviour behaviour,
      bool safe = false);
  /// Applies the requested set behavior for the given identifier.
  ///
  /// \param val The string identifier for the data.
  /// \param data The data to store.
  /// \param behaviour Whether to set, set-if-absent, or overwrite.
  /// \param safe Whether the caller guarantees no concurrent access.
  /// \returns True if the data was changed.
  bool doSetContextDataHelper(
      const std::string& val,
      std::unique_ptr<RequestData>& data,
      DoSetBehaviour behaviour,
      bool safe = false) {
    return doSetContextDataHelper(RequestToken(val), data, behaviour, safe);
  }

  /// Internal state of a RequestContext.
  ///
  /// State implementation with single-writer multi-reader data structures
  /// protected by hazard pointers for readers and a lock for writers.
  struct State {
    /// Hazard pointer-protected combined structure for data and callbacks.
    struct Combined;
    hazptr_obj_cohort<> cohort_; ///< Cohort controlling destruction order.
    std::atomic<Combined*> combined_{nullptr}; ///< The current combined data.
    /// Version used to invalidate getThreadCachedContextData() caches.
    ///
    /// A process-wide unique id is used (instead of, for example, a local
    /// counter) so that it is not necessary to compare the request context
    /// pointer as well. This saves one word of TLS and one comparison.
    std::atomic<uint64_t> version_{processLocalUniqueId()};
    /// Small exclusive mutex guarding writers.
    ///
    /// This should never be used directly. Use LockGuard so that thread caches
    /// are invalidated at the end of the critical section.
    mutable folly::SharedMutex mutex_;

    /// Constructs an empty state.
    State();
    /// Copy-constructs a state from another.
    ///
    /// \param o The state to copy.
    State(const State& o);
    /// Move construction is disabled.
    State(State&& other) = delete;
    /// Copy assignment is disabled.
    State& operator=(const State& other) = delete;
    /// Move assignment is disabled.
    State& operator=(State&& other) = delete;
    /// Destroys the state.
    ~State();

   private:
    friend class RequestContext;

    struct SetContextDataResult {
      bool changed; // Changes were made
      bool unexpected; // Update was unexpected
      Combined* replaced; // The combined structure was replaced
    };

    class LockGuard;

    Combined* combined() const;
    Combined* ensureCombined(); // Lazy allocation if needed
    void setCombined(Combined* p);
    Combined* expand(Combined* combined);
    bool doSetContextData(
        const RequestToken& token,
        std::unique_ptr<RequestData>& data,
        DoSetBehaviour behaviour,
        bool safe);
    bool hasContextData(const RequestToken& token) const;
    RequestData* getContextData(const RequestToken& token);
    const RequestData* getContextData(const RequestToken& token) const;
    void onSet();
    void onUnset();
    void clearContextData(const RequestToken& token);
    SetContextDataResult doSetContextDataHelper(
        const RequestToken& token,
        std::unique_ptr<RequestData>& data,
        DoSetBehaviour behaviour,
        bool safe);
    Combined* eraseOldData(
        Combined* cur,
        const RequestToken& token,
        RequestData* oldData,
        bool safe);
    Combined* insertNewData(
        Combined* cur,
        const RequestToken& token,
        std::unique_ptr<RequestData>& data,
        bool found);
  }; // State
  State state_; ///< The internal state of this context.
  /// The root id; shallow copies keep a note of the root context.
  intptr_t rootId_;
};
static_assert(sizeof(RequestContext) <= 64, "unexpected size");

/**
 * RequestContextSaverScopeGuard allows to replace the current context
 * without switching back to original context, while ensuring that the original
 * context is restored on guard destruction.
 *
 * The constructor saves the current context but does not replace it; instead,
 * RequestContext::setContext() should be called directly. The original context
 * will be restored on guard destruction. This is different from
 * RequestContextScopeGuard which replaces the current context in construction.
 *
 * This enables taking advantage of the optimization in setContext() which skips
 * invoking the RequestData callbacks if the new context is the the same as the
 * current one. The use case is processing tasks in a loop which are likely to
 * share the same context.
 */
class RequestContextSaverScopeGuard {
 public:
  /// Saves the current context to be restored on destruction.
  RequestContextSaverScopeGuard()
      : RequestContextSaverScopeGuard(RequestContext::saveContext()) {}

  /// Copy construction is disabled.
  RequestContextSaverScopeGuard(const RequestContextSaverScopeGuard& other) =
      delete;
  /// Copy assignment is disabled.
  RequestContextSaverScopeGuard& operator=(
      const RequestContextSaverScopeGuard& other) = delete;
  /// Move construction is disabled.
  RequestContextSaverScopeGuard(RequestContextSaverScopeGuard&& other) = delete;
  /// Move assignment is disabled.
  RequestContextSaverScopeGuard& operator=(
      RequestContextSaverScopeGuard&& other) = delete;

  /// Restores the saved context.
  ~RequestContextSaverScopeGuard() {
    RequestContext::setContext(std::move(prev_));
  }

 protected:
  /// Saves the given context to be restored on destruction.
  ///
  /// \param ctx The context to restore on destruction.
  explicit RequestContextSaverScopeGuard(std::shared_ptr<RequestContext>&& ctx)
      : prev_(std::move(ctx)) {}

 private:
  std::shared_ptr<RequestContext> prev_;
};

/**
 * Note: you probably want to use ShallowCopyRequestContextScopeGuard
 * This resets all other RequestData for the duration of the scope!
 */
class RequestContextScopeGuard : private RequestContextSaverScopeGuard {
 public:
  /// Creates a new RequestContext, restored to the original on scope exit.
  RequestContextScopeGuard()
      : RequestContextSaverScopeGuard(RequestContext::create()) {}

  /// Sets a previously captured RequestContext for the scope's duration.
  ///
  /// It will be automatically reset to the original value when this goes out of
  /// scope.
  ///
  /// \param ctx The context to set as current.
  explicit RequestContextScopeGuard(std::shared_ptr<RequestContext> const& ctx)
      : RequestContextSaverScopeGuard(RequestContext::setContext(ctx)) {}
  /// Sets a previously captured RequestContext for the scope's duration.
  ///
  /// \param ctx The context to set as current.
  explicit RequestContextScopeGuard(std::shared_ptr<RequestContext>&& ctx)
      : RequestContextSaverScopeGuard(
            RequestContext::setContext(std::move(ctx))) {}
};

/**
 * This guard maintains all the RequestData pointers of the parent.
 * This allows to overwrite a specific RequestData pointer for the
 * scope's duration, without breaking others.
 *
 * Only modified pointers will have their set/onset methods called
 */
struct ShallowCopyRequestContextScopeGuard {
  /// Shallow-copies the current context for the scope's duration.
  ShallowCopyRequestContextScopeGuard()
      : prev_(RequestContext::setShallowCopyContext()) {}

  /**
   * Shallow copy then overwrite one specific RequestData.
   *
   * Helper constructor which is a more efficient equivalent to
   * "clearRequestData" then "setRequestData" after the guard.
   *
   * \param token The identifier for the data to overwrite.
   * \param data The data to store.
   */
  ShallowCopyRequestContextScopeGuard(
      const RequestToken& token, std::unique_ptr<RequestData> data)
      : ShallowCopyRequestContextScopeGuard() {
    RequestContext::get()->overwriteContextData(token, std::move(data), true);
  }
  /// Shallow copy then overwrite one specific RequestData.
  ///
  /// \param val The string identifier for the data to overwrite.
  /// \param data The data to store.
  ShallowCopyRequestContextScopeGuard(
      const std::string& val, std::unique_ptr<RequestData> data)
      : ShallowCopyRequestContextScopeGuard() {
    RequestContext::get()->overwriteContextData(val, std::move(data), true);
  }

  /**
   * Shallow copy then overwrite multiple RequestData instances.
   *
   * Helper constructor which is more efficient than using multiple scope guards.
   * Accepts iterators to a container of <string/RequestToken, RequestData
   * pointer> pairs.
   *
   * \param first The first token/data pair to overwrite.
   * \param rest The remaining token/data pairs to overwrite.
   */
  template <typename... Item>
  explicit ShallowCopyRequestContextScopeGuard(
      RequestDataItem&& first, Item&&... rest)
      : ShallowCopyRequestContextScopeGuard(MultiTag{}, first, rest...) {}

  /// Restores the previous context.
  ~ShallowCopyRequestContextScopeGuard() {
    RequestContext::setContext(std::move(prev_));
  }

  /// Copy construction is disabled.
  ShallowCopyRequestContextScopeGuard(
      const ShallowCopyRequestContextScopeGuard& other) = delete;
  /// Copy assignment is disabled.
  ShallowCopyRequestContextScopeGuard& operator=(
      const ShallowCopyRequestContextScopeGuard& other) = delete;
  /// Move construction is disabled.
  ShallowCopyRequestContextScopeGuard(
      ShallowCopyRequestContextScopeGuard&& other) = delete;
  /// Move assignment is disabled.
  ShallowCopyRequestContextScopeGuard& operator=(
      ShallowCopyRequestContextScopeGuard&& other) = delete;

 private:
  struct MultiTag {};
  template <typename... Item>
  explicit ShallowCopyRequestContextScopeGuard(MultiTag, Item&... item)
      : ShallowCopyRequestContextScopeGuard() {
    auto rc = RequestContext::get();
    auto go = [&](RequestDataItem& i) {
      rc->overwriteContextData(i.first, std::move(i.second), true);
    };

    ((go(item)), ...);
  }

  std::shared_ptr<RequestContext> prev_;
};

/// Debug-only guard that checks the context is unchanged across a scope.
///
/// Ensures that the current request context at destruction is the same as the
/// one at construction.
class [[maybe_unused]] DCheckRequestContextRestoredGuard {
#ifndef NDEBUG
 public:
  /// Captures the current context at construction.
  [[nodiscard]] DCheckRequestContextRestoredGuard()
      : prev_(RequestContext::saveContext()) {}

  /// Checks the context matches the captured one and destroys the guard.
  ~DCheckRequestContextRestoredGuard();

  /// Copy construction is disabled.
  DCheckRequestContextRestoredGuard(
      const DCheckRequestContextRestoredGuard& other) = delete;
  /// Copy assignment is disabled.
  DCheckRequestContextRestoredGuard& operator=(
      const DCheckRequestContextRestoredGuard& other) = delete;
  /// Move construction is disabled.
  DCheckRequestContextRestoredGuard(DCheckRequestContextRestoredGuard&& other) =
      delete;
  /// Move assignment is disabled.
  DCheckRequestContextRestoredGuard& operator=(
      DCheckRequestContextRestoredGuard&& other) = delete;

 private:
  std::shared_ptr<RequestContext> prev_;
#endif
};

template <class Traits>
/* static */ FOLLY_EXPORT RequestData*
RequestContext::getThreadCachedContextData() {
  thread_local RequestData* cachedData = nullptr;
  thread_local uint64_t cachedVersion = 0; // Allowed sentinel value.

  // In case cache is invalid, version snapshot must be taken before performing
  // the lookup.
  uint64_t curVersion = state_.version_.load(std::memory_order_acquire);

  if (curVersion == cachedVersion) {
    return cachedData;
  }

  cachedData = getContextData(Traits::kToken);
  cachedVersion = curVersion;
  return cachedData;
}

} // namespace folly
