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
#include <cassert>
#include <climits>
#include <utility>

#include <folly/Function.h>
#include <folly/Optional.h>
#include <folly/Range.h>
#include <folly/Utility.h>
#include <folly/lang/Exception.h>

//  Compiles in the keep-alive object trace points below. Off by default: this
//  header reaches nearly every translation unit, so the trace points cost
//  binary size in every binary in the repo even with no hooks installed. Set it
//  for the whole build, never for a subset of targets -- the trace points sit
//  in inline and template functions whose out-of-line copies are comdat-folded
//  across translation units, so a mixed build silently gets whichever copy the
//  linker keeps.
#ifndef FOLLY_ENABLE_KEEPALIVE_OBJ_TRACE
#define FOLLY_ENABLE_KEEPALIVE_OBJ_TRACE 0
#endif

namespace folly {

/// A move-only callable holding a unit of work with no arguments or result.
using Func = Function<void()>;

class Executor;

namespace detail {

class ExecutorKeepAliveBase {
 public:
  //  A dummy keep-alive is a keep-alive to an executor which does not support
  //  the keep-alive mechanism.
  static constexpr uintptr_t kDummyFlag = uintptr_t(1) << 0;

  //  An alias keep-alive is a keep-alive to an executor to which there is
  //  known to be another keep-alive whose lifetime surrounds the lifetime of
  //  the alias.
  static constexpr uintptr_t kAliasFlag = uintptr_t(1) << 1;

  static constexpr uintptr_t kFlagMask = kDummyFlag | kAliasFlag;
  static constexpr uintptr_t kExecutorMask = ~kFlagMask;
};

//  Optional, process-global tracing hooks for keep-alive ownership, keyed by
//  the address of the ExecutorKeepAlive object itself rather than by the
//  executor it refers to. Keying on the instance is what makes a release
//  pairable with its acquire: a move re-keys the existing record instead of
//  looking like a release plus an unrelated acquire, so the recorded set is
//  the exact set of live keep-alives rather than a biased sample of them.
//
//  Only keep-alives which hold an executor reference count are acquired and
//  released: dummy and alias keep-alives hold none, and are skipped on exactly
//  the same condition ExecutorKeepAlive::reset() uses to decide whether to call
//  keepAliveRelease(). The move hook is deliberately not filtered that way: it
//  fires for every move, so `from` may be an address no acquire ever reported.
//  A hook must read that as "`to` holds no counted reference either" and drop
//  any record it holds for `to` -- which is also what clears a record stranded
//  at a recycled address.
//
//  This is a debug facility. Without FOLLY_ENABLE_KEEPALIVE_OBJ_TRACE the trace
//  points compile away and the keep-alive path is unchanged. With it, and with
//  no hooks installed, each trace point is a relaxed load of a never-written
//  global and a FOLLY_UNLIKELY branch. With hooks installed it costs whatever
//  the hook costs on every acquire, release and move process-wide -- install
//  only for a diagnostic run. The hooks run from arbitrary threads: they must
//  be fast and must not throw.
using KeepAliveObjTraceHook = void (*)(const void* obj, Executor* target);
using KeepAliveObjMoveHook = void (*)(const void* from, const void* to);

//  Function-local statics rather than out-of-line globals: this header is
//  included nearly everywhere, and a global defined in Executor.cpp would give
//  every translation unit that instantiates a keep-alive an undefined symbol
//  unless it also took a link-time dependency on Executor.cpp. std::atomic's
//  constructor is constexpr, so these are constant-initialized and carry no
//  thread-safe-static guard: the load below stays a plain relaxed load.
FOLLY_EXPORT FOLLY_ALWAYS_INLINE std::atomic<KeepAliveObjTraceHook>&
keepAliveObjAcquireHook() {
  static std::atomic<KeepAliveObjTraceHook> hook{nullptr};
  return hook;
}

FOLLY_EXPORT FOLLY_ALWAYS_INLINE std::atomic<KeepAliveObjTraceHook>&
keepAliveObjReleaseHook() {
  static std::atomic<KeepAliveObjTraceHook> hook{nullptr};
  return hook;
}

FOLLY_EXPORT FOLLY_ALWAYS_INLINE std::atomic<KeepAliveObjMoveHook>&
keepAliveObjMoveHook() {
  static std::atomic<KeepAliveObjMoveHook> hook{nullptr};
  return hook;
}

inline void traceKeepAliveObjAcquire(
    [[maybe_unused]] const void* obj,
    [[maybe_unused]] Executor* target) noexcept {
#if FOLLY_ENABLE_KEEPALIVE_OBJ_TRACE
  auto hook = keepAliveObjAcquireHook().load(std::memory_order_relaxed);
  if (FOLLY_UNLIKELY(hook != nullptr)) {
    hook(obj, target);
  }
#endif
}

inline void traceKeepAliveObjRelease(
    [[maybe_unused]] const void* obj,
    [[maybe_unused]] Executor* target) noexcept {
#if FOLLY_ENABLE_KEEPALIVE_OBJ_TRACE
  auto hook = keepAliveObjReleaseHook().load(std::memory_order_relaxed);
  if (FOLLY_UNLIKELY(hook != nullptr)) {
    hook(obj, target);
  }
#endif
}

inline void traceKeepAliveObjMove(
    [[maybe_unused]] const void* from,
    [[maybe_unused]] const void* to) noexcept {
#if FOLLY_ENABLE_KEEPALIVE_OBJ_TRACE
  auto hook = keepAliveObjMoveHook().load(std::memory_order_relaxed);
  if (FOLLY_UNLIKELY(hook != nullptr)) {
    hook(from, to);
  }
#endif
}

} // namespace detail

/// Install (or, with all-null arguments, remove) the keep-alive object tracing
/// hooks.
///
/// Not synchronized with in-flight keep-alive operations: install once at
/// startup, before the threads being traced exist.
///
/// @param acquire Hook fired when a counted keep-alive is acquired.
/// @param release Hook fired when a counted keep-alive is released.
/// @param move Hook fired when a keep-alive object is moved.
/// @return false when FOLLY_ENABLE_KEEPALIVE_OBJ_TRACE is unset: the hooks are
///   stored, but no trace point will ever call them.
bool setKeepAliveObjTraceHooks(
    detail::KeepAliveObjTraceHook acquire,
    detail::KeepAliveObjTraceHook release,
    detail::KeepAliveObjMoveHook move);

/**
 * `ExecutorKeepAlive` is a safe pointer to an `Executor`.
 *
 * For any `Executor` that supports keep-alive functionality, its destructor
 * will block until all the `ExecutorKeepAlive` objects associated with that
 * executor are destroyed.  For executors that don't support the keep-alive
 * functionality, `ExecutorKeepAlive` doesn't provide such protection.
 *
 * `ExecutorKeepAlive` should *always* be used instead of `Executor*`.
 * `ExecutorKeepAlive` can be implicitly constructed from `Executor*`.
 *
 * The `getKeepAliveToken()` helper can be used to construct a keep-alive in
 * templated code if you need to preserve the original executor type.
 */
template <typename ExecutorT = Executor>
class ExecutorKeepAlive : private detail::ExecutorKeepAliveBase {
 public:
  /// A callable that consumes an rvalue keep-alive to this executor.
  using KeepAliveFunc = Function<void(ExecutorKeepAlive&&)>;

  /// Constructs an empty keep-alive that refers to no executor.
  ExecutorKeepAlive() = default;

  /// Releases the referenced executor keep-alive, if any.
  ~ExecutorKeepAlive() {
    static_assert(
        std::is_standard_layout<ExecutorKeepAlive>::value, "standard-layout");
    static_assert(sizeof(ExecutorKeepAlive) == sizeof(void*), "pointer size");
    static_assert(
        alignof(ExecutorKeepAlive) == alignof(void*), "pointer align");

    reset();
  }

  /// Move-constructs a keep-alive, taking over the reference held by `other`.
  ///
  /// \param other The keep-alive to move from.
  ExecutorKeepAlive(ExecutorKeepAlive&& other) noexcept
      : storage_(std::exchange(other.storage_, 0)) {
    detail::traceKeepAliveObjMove(&other, this);
  }

  /// Copy-constructs a keep-alive that shares the executor referred to by `other`.
  ///
  /// \param other The keep-alive to copy from.
  ExecutorKeepAlive(const ExecutorKeepAlive& other) noexcept;

  template <
      typename OtherExecutor,
      typename = typename std::enable_if<
          std::is_convertible<OtherExecutor*, ExecutorT*>::value>::type>
  /// Move-constructs from a keep-alive to a convertible executor type.
  ///
  /// \param other The keep-alive to move from.
  /* implicit */ ExecutorKeepAlive(
      ExecutorKeepAlive<OtherExecutor>&& other) noexcept
      : ExecutorKeepAlive(other.get(), other.storage_ & kFlagMask) {
    other.storage_ = 0;
    detail::traceKeepAliveObjMove(&other, this);
  }

  template <
      typename OtherExecutor,
      typename = typename std::enable_if<
          std::is_convertible<OtherExecutor*, ExecutorT*>::value>::type>
  /// Copy-constructs from a keep-alive to a convertible executor type.
  ///
  /// \param other The keep-alive to copy from.
  /* implicit */ ExecutorKeepAlive(
      const ExecutorKeepAlive<OtherExecutor>& other) noexcept;

  /// Constructs a keep-alive that refers to `executor`.
  ///
  /// \param executor The executor this keep-alive refers to.
  /* implicit */ ExecutorKeepAlive(ExecutorT* executor);

  /// Move-assigns, releasing any current reference and taking over `other`'s.
  ///
  /// \param other The keep-alive to move from.
  /// \returns A reference to this keep-alive.
  ExecutorKeepAlive& operator=(ExecutorKeepAlive&& other) noexcept {
    //  reset() first, so the release of the overwritten value is traced before
    //  the move re-keys the incoming one onto this address.
    reset();
    storage_ = std::exchange(other.storage_, 0);
    detail::traceKeepAliveObjMove(&other, this);
    return *this;
  }

  /// Copy-assigns, sharing the executor referred to by `other`.
  ///
  /// \param other The keep-alive to copy from.
  /// \returns A reference to this keep-alive.
  ExecutorKeepAlive& operator=(ExecutorKeepAlive const& other) {
    return operator=(folly::copy(other));
  }

  template <
      typename OtherExecutor,
      typename = typename std::enable_if<
          std::is_convertible<OtherExecutor*, ExecutorT*>::value>::type>
  /// Move-assigns from a keep-alive to a convertible executor type.
  ///
  /// \param other The keep-alive to move from.
  /// \returns A reference to this keep-alive.
  ExecutorKeepAlive& operator=(
      ExecutorKeepAlive<OtherExecutor>&& other) noexcept {
    return *this = ExecutorKeepAlive(std::move(other));
  }

  template <
      typename OtherExecutor,
      typename = typename std::enable_if<
          std::is_convertible<OtherExecutor*, ExecutorT*>::value>::type>
  /// Copy-assigns from a keep-alive to a convertible executor type.
  ///
  /// \param other The keep-alive to copy from.
  /// \returns A reference to this keep-alive.
  ExecutorKeepAlive& operator=(const ExecutorKeepAlive<OtherExecutor>& other) {
    return *this = ExecutorKeepAlive(other);
  }

  /// Releases the referenced executor keep-alive and empties this token.
  void reset() noexcept;

  /// Returns true if this token refers to an executor.
  ///
  /// \returns true if this token refers to an executor.
  explicit operator bool() const { return storage_; }

  /// Returns the referenced executor, or nullptr if empty.
  ///
  /// \returns The referenced executor, or nullptr if empty.
  ExecutorT* get() const {
    return reinterpret_cast<ExecutorT*>(storage_ & kExecutorMask);
  }

  /// Returns a reference to the referenced executor.
  ///
  /// \returns A reference to the referenced executor.
  ExecutorT& operator*() const { return *get(); }

  /// Returns a pointer to the referenced executor.
  ///
  /// \returns A pointer to the referenced executor.
  ExecutorT* operator->() const { return get(); }

  /// Returns a new keep-alive that shares this token's executor.
  ///
  /// \returns A new keep-alive sharing this token's executor.
  ExecutorKeepAlive copy() const;

  /// Returns an alias keep-alive whose lifetime is assumed to be surrounded by this one.
  ///
  /// \returns An alias keep-alive to the same executor.
  ExecutorKeepAlive get_alias() const {
    return ExecutorKeepAlive(storage_ | kAliasFlag);
  }

  /// Enqueues `f` on the referenced executor, passing it this consumed token.
  ///
  /// \param f The callable to enqueue, invoked with this consumed token.
  template <class KAF>
  void add(KAF&& f) && {
    static_assert(
        is_invocable<KAF, ExecutorKeepAlive&&>::value,
        "Parameter to add must be void(ExecutorKeepAlive&&)>");
    auto ex = get();
    ex->add([ka = std::move(*this), f_2 = std::forward<KAF>(f)]() mutable {
      f_2(std::move(ka));
    });
  }

 private:
  friend class Executor;
  template <typename OtherExecutor>
  friend class ExecutorKeepAlive;

  ExecutorKeepAlive(ExecutorT* executor, uintptr_t flags) noexcept
      : storage_(reinterpret_cast<uintptr_t>(executor) | flags) {
    assert(executor);
    assert(!(reinterpret_cast<uintptr_t>(executor) & ~kExecutorMask));
    assert(!(flags & kExecutorMask));
  }

  explicit ExecutorKeepAlive(uintptr_t storage) noexcept : storage_(storage) {}

  //  Combined storage for the executor pointer and for all flags.
  uintptr_t storage_{reinterpret_cast<uintptr_t>(nullptr)};
};

/// An Executor accepts units of work with add(), which should be
/// threadsafe.
class Executor {
 public:
  /// Destroys the executor.
  virtual ~Executor() = default;

  /// Enqueue a function to be executed by this executor. This and all
  /// variants must be threadsafe.
  ///
  /// @param func The unit of work to enqueue.
  virtual void add(Func func) = 0;

  /// Enqueue a function with a given priority, where 0 is the medium priority
  /// This is up to the implementation to enforce
  ///
  /// @param func The unit of work to enqueue.
  /// @param priority The scheduling priority for the work.
  virtual void addWithPriority(Func func, int8_t priority);

  /// Returns the number of priority levels this executor supports.
  ///
  /// \returns The number of supported priority levels.
  virtual uint8_t getNumPriorities() const { return 1; }

  /// Lowest schedulable priority.
  static constexpr int8_t LO_PRI = SCHAR_MIN;
  /// Medium (default) schedulable priority.
  static constexpr int8_t MID_PRI = 0;
  /// Highest schedulable priority.
  static constexpr int8_t HI_PRI = SCHAR_MAX;

  /// Alias for ExecutorKeepAlive, a safe pointer to an Executor.
  ///
  /// Compatibility shim. Cannot be forward-declared, unlike
  /// `ExecutorKeepAlive`.
  template <typename ExecutorT = Executor>
  using KeepAlive = ExecutorKeepAlive<ExecutorT>;

  /// Returns a keep-alive token for `executor`, or an empty token if it is null.
  ///
  /// \param executor The executor to obtain a keep-alive token for.
  /// \returns A keep-alive token referring to `executor`, or an empty token.
  template <typename ExecutorT>
  static KeepAlive<ExecutorT> getKeepAliveToken(ExecutorT* executor) {
    static_assert(
        std::is_base_of<Executor, ExecutorT>::value,
        "getKeepAliveToken only works for folly::Executor implementations.");
    if (!executor) {
      return {};
    }
    folly::Executor* executorPtr = executor;
    if (executorPtr->keepAliveAcquire()) {
      return makeKeepAlive<ExecutorT>(executor);
    }
    return makeKeepAliveDummy<ExecutorT>(executor);
  }

  /// Returns a keep-alive token for `executor`.
  ///
  /// \param executor The executor to obtain a keep-alive token for.
  /// \returns A keep-alive token referring to `executor`.
  template <typename ExecutorT>
  static KeepAlive<ExecutorT> getKeepAliveToken(ExecutorT& executor) {
    static_assert(
        std::is_base_of<Executor, ExecutorT>::value,
        "getKeepAliveToken only works for folly::Executor implementations.");
    return getKeepAliveToken(&executor);
  }

  /// Invokes `f`, logging and swallowing any exception it throws.
  ///
  /// \param p A prefix used when logging a swallowed exception.
  /// \param f The callable to invoke.
  template <typename F>
  FOLLY_ERASE static void invokeCatchingExns(char const* p, F f) noexcept {
    catch_exception(f, invokeCatchingExnsLog, p);
  }

 protected:
  template <typename>
  friend class ExecutorKeepAlive;

  /**
   * Returns true if the KeepAlive is constructed from an executor that does
   * not support the keep alive ref-counting functionality
   *
   * @param keepAlive The keep-alive token to test.
   * @return true if `keepAlive` is a dummy token.
   */
  template <typename ExecutorT>
  static bool isKeepAliveDummy(const KeepAlive<ExecutorT>& keepAlive) {
    return keepAlive.storage_ & KeepAlive<ExecutorT>::kDummyFlag;
  }

  /// Acquires a keep-alive reference on `executor`.
  ///
  /// \param executor The executor to acquire a reference on.
  /// \returns true if a keep-alive reference was acquired.
  static bool keepAliveAcquire(Executor* executor) {
    return executor->keepAliveAcquire();
  }
  /// Releases a keep-alive reference on `executor`.
  ///
  /// \param executor The executor to release a reference on.
  static void keepAliveRelease(Executor* executor) {
    return executor->keepAliveRelease();
  }

  /// Acquire a keep alive token. Should return false if keep-alive mechanism
  /// is not supported.
  ///
  /// \returns true if a keep-alive reference was acquired.
  virtual bool keepAliveAcquire() noexcept;
  /// Release a keep alive token previously acquired by keepAliveAcquire().
  /// Will never be called if keepAliveAcquire() returns false.
  virtual void keepAliveRelease() noexcept;

  /// Makes a counted (non-dummy, non-alias) keep-alive referring to `executor`.
  ///
  /// This is the sole origin of a reference-counting keep-alive: every such
  /// keep-alive is either made here or moved from one that was, so tracing the
  /// acquire here -- rather than in the individual ExecutorKeepAlive
  /// constructors, all of which funnel through getKeepAliveToken() -- pairs one
  /// acquire with the one release in reset(). The captured stack still names the
  /// original caller, since the constructors are simply further up the same
  /// stack.
  ///
  /// \param executor The executor to make a keep-alive for.
  /// \returns A counted keep-alive referring to `executor`.
  template <typename ExecutorT>
  static KeepAlive<ExecutorT> makeKeepAlive(ExecutorT* executor) {
    static_assert(
        std::is_base_of<Executor, ExecutorT>::value,
        "makeKeepAlive only works for folly::Executor implementations.");
    KeepAlive<ExecutorT> keepAlive{executor, uintptr_t(0)};
    detail::traceKeepAliveObjAcquire(&keepAlive, executor);
    return keepAlive;
  }

 private:
  static void invokeCatchingExnsLog(char const* prefix) noexcept;

  template <typename ExecutorT>
  static KeepAlive<ExecutorT> makeKeepAliveDummy(ExecutorT* executor) {
    static_assert(
        std::is_base_of<Executor, ExecutorT>::value,
        "makeKeepAliveDummy only works for folly::Executor implementations.");
    return KeepAlive<ExecutorT>{executor, KeepAlive<ExecutorT>::kDummyFlag};
  }
};

template <typename ExecutorT>
ExecutorKeepAlive<ExecutorT>::ExecutorKeepAlive(
    const ExecutorKeepAlive<ExecutorT>& other) noexcept
    : ExecutorKeepAlive(Executor::getKeepAliveToken(other.get())) {}

template <typename ExecutorT>
template <typename OtherExecutor, typename>
ExecutorKeepAlive<ExecutorT>::ExecutorKeepAlive(
    const ExecutorKeepAlive<OtherExecutor>& other) noexcept
    : ExecutorKeepAlive(Executor::getKeepAliveToken(other.get())) {}

template <typename ExecutorT>
ExecutorKeepAlive<ExecutorT>::ExecutorKeepAlive(ExecutorT* executor) {
  *this = Executor::getKeepAliveToken(executor);
}

template <typename ExecutorT>
void ExecutorKeepAlive<ExecutorT>::reset() noexcept {
  if (Executor* executor = get()) {
    auto const flags = std::exchange(storage_, 0) & kFlagMask;
    if (!(flags & (kDummyFlag | kAliasFlag))) {
      detail::traceKeepAliveObjRelease(this, executor);
      executor->keepAliveRelease();
    }
  }
}

template <typename ExecutorT>
ExecutorKeepAlive<ExecutorT> ExecutorKeepAlive<ExecutorT>::copy() const {
  return Executor::isKeepAliveDummy(*this) //
      ? Executor::makeKeepAliveDummy(get())
      : Executor::getKeepAliveToken(get());
}

/// Returns a keep-alive token which guarantees that Executor will keep
/// processing tasks until the token is released (if supported by Executor).
/// KeepAlive always contains a valid pointer to an Executor.
///
/// @param executor The executor to obtain a keep-alive token for.
/// @return A keep-alive token referring to `executor`.
template <typename ExecutorT>
Executor::KeepAlive<ExecutorT> getKeepAliveToken(ExecutorT* executor) {
  static_assert(
      std::is_base_of<Executor, ExecutorT>::value,
      "getKeepAliveToken only works for folly::Executor implementations.");
  return Executor::getKeepAliveToken(executor);
}

/// Returns a keep-alive token referring to `executor`.
///
/// \param executor The executor to obtain a keep-alive token for.
/// \returns A keep-alive token referring to `executor`.
template <typename ExecutorT>
Executor::KeepAlive<ExecutorT> getKeepAliveToken(ExecutorT& executor) {
  static_assert(
      std::is_base_of<Executor, ExecutorT>::value,
      "getKeepAliveToken only works for folly::Executor implementations.");
  return getKeepAliveToken(&executor);
}

/// Returns a copy of the keep-alive token `ka`.
///
/// \param ka The keep-alive token to copy.
/// \returns A copy of `ka`.
template <typename ExecutorT>
Executor::KeepAlive<ExecutorT> getKeepAliveToken(
    Executor::KeepAlive<ExecutorT>& ka) {
  return ka.copy();
}

/// Records whether blocking is forbidden while an executor is running work.
struct ExecutorBlockingContext {
  /// Whether blocking calls are forbidden in this context.
  bool forbid;
  /// Whether termination is allowed when a forbidden blocking call occurs.
  bool allowTerminationOnBlocking;
  /// The executor this context applies to, if any.
  Executor* ex = nullptr;
  /// A label identifying this context.
  StringPiece tag;
};
static_assert(
    std::is_standard_layout<ExecutorBlockingContext>::value,
    "non-standard layout");

/// A node in the thread-local stack of executor blocking contexts.
struct ExecutorBlockingList {
  /// The previous (enclosing) blocking context node.
  ExecutorBlockingList* prev;
  /// The blocking context for this node.
  ExecutorBlockingContext curr;
};
static_assert(
    std::is_standard_layout<ExecutorBlockingList>::value,
    "non-standard layout");

/// Scoped guard that pushes an executor blocking context for its lifetime.
class ExecutorBlockingGuard {
 public:
  /// Tag selecting the constructor that permits blocking.
  struct PermitTag {};
  /// Tag selecting the constructor that tracks blocking.
  struct TrackTag {};
  /// Tag selecting the constructor that prohibits blocking.
  struct ProhibitTag {};

  /// Pops the blocking context pushed by this guard.
  ~ExecutorBlockingGuard();
  /// Deleted: a guard must be constructed with one of the tagged constructors.
  ExecutorBlockingGuard() = delete;

  /// Pushes a context that permits blocking.
  ///
  /// \param permit Tag selecting the permit-blocking constructor.
  explicit ExecutorBlockingGuard(PermitTag permit) noexcept;
  /// Pushes a context that tracks blocking on `ex`, labeled by `tag`.
  ///
  /// \param track Tag selecting the track-blocking constructor.
  /// \param ex The executor the context applies to.
  /// \param tag A label identifying the context.
  explicit ExecutorBlockingGuard(
      TrackTag track, Executor* ex, StringPiece tag) noexcept;
  /// Pushes a context that prohibits blocking on `ex`, labeled by `tag`.
  ///
  /// \param prohibit Tag selecting the prohibit-blocking constructor.
  /// \param ex The executor the context applies to.
  /// \param tag A label identifying the context.
  explicit ExecutorBlockingGuard(
      ProhibitTag prohibit, Executor* ex, StringPiece tag) noexcept;

  /// Deleted: the guard is not movable.
  ///
  /// \param other The guard that would be moved from.
  ExecutorBlockingGuard(ExecutorBlockingGuard&& other) = delete;
  /// Deleted: the guard is not copyable.
  ///
  /// \param other The guard that would be copied from.
  ExecutorBlockingGuard(ExecutorBlockingGuard const& other) = delete;

  /// Deleted: the guard is not copy-assignable.
  ///
  /// \param other The guard that would be copied from.
  ExecutorBlockingGuard& operator=(ExecutorBlockingGuard const& other) = delete;
  /// Deleted: the guard is not move-assignable.
  ///
  /// \param other The guard that would be moved from.
  ExecutorBlockingGuard& operator=(ExecutorBlockingGuard&& other) = delete;

 private:
  ExecutorBlockingList list_;
};

/// Returns the current thread's executor blocking context, if any is active.
///
/// \returns The active blocking context, or an empty optional if none.
Optional<ExecutorBlockingContext> getExecutorBlockingContext() noexcept;

} // namespace folly
