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

#include <folly/CppAttributes.h>
#include <folly/Function.h>
#include <folly/OperationCancelled.h>

#include <atomic>
#include <memory>
#include <thread>
#include <type_traits>

namespace folly {

class CancellationCallback;
class CancellationSource;
struct cancellation_token_merge_fn;

namespace detail {
class CancellationState;
struct CancellationStateTokenDeleter {
  void operator()(CancellationState*) noexcept;
};
struct CancellationStateSourceDeleter {
  void operator()(CancellationState*) noexcept;
};
using CancellationStateTokenPtr =
    std::unique_ptr<CancellationState, CancellationStateTokenDeleter>;
using CancellationStateSourcePtr =
    std::unique_ptr<CancellationState, CancellationStateSourceDeleter>;
template <typename...>
struct WithDataTag;
} // namespace detail

/**
 * A CancellationToken is an object that can be passed into an function or
 * operation that allows the caller to later request that the operation be
 * cancelled.
 *
 * A CancellationToken object can be obtained by calling the .getToken()
 * method on a CancellationSource or by copying another CancellationToken
 * object. All CancellationToken objects obtained from the same original
 * CancellationSource object all reference the same underlying cancellation
 * state and will all be cancelled together.
 *
 * If your function needs to be cancellable but does not need to request
 * cancellation then you should take a CancellationToken as a parameter.
 * If your function needs to be able to request cancellation then you
 * should instead take a CancellationSource as a parameter.
 *
 * @refcode folly/docs/examples/folly/CancellationToken.cpp
 * @class folly::CancellationToken
 */
class CancellationToken {
 public:
  /**
   * Constructs to a token that can never be cancelled.
   *
   * Pass a default-constructed CancellationToken into an operation that
   * you never intend to cancel. These objects are very cheap to create.
   */
  CancellationToken() noexcept = default;

  /// Construct a copy of the token that shares the same underlying state.
  ///
  /// \param other The token to copy from.
  CancellationToken(const CancellationToken& other) noexcept;

  /// Construct a token by moving the underlying state.
  ///
  /// \param other The token to move from.
  CancellationToken(CancellationToken&& other) noexcept;

  /// Copy-assign the token so it shares the same underlying state as `other`.
  ///
  /// \param other The token to copy from.
  /// \returns A reference to this token.
  CancellationToken& operator=(const CancellationToken& other) noexcept;

  /// Move-assign the token, taking over the underlying state of `other`.
  ///
  /// \param other The token to move from.
  /// \returns A reference to this token.
  CancellationToken& operator=(CancellationToken&& other) noexcept;

  /**
   * Query whether someone has called .requestCancellation() on an instance
   * of CancellationSource object associated with this CancellationToken.
   *
   * @return true if cancellation has been requested, false otherwise
   */
  bool isCancellationRequested() const noexcept;

  /**
   * Query whether this CancellationToken can ever have cancellation requested
   * on it.
   *
   * This will return false if the CancellationToken is not associated with a
   * CancellationSource object. eg. because the CancellationToken was
   * default-constructed, has been moved-from or because the last
   * CancellationSource object associated with the underlying cancellation state
   * has been destroyed and the operation has not yet been cancelled and so
   * never will be.
   *
   * Implementations of operations may be able to take more efficient code-paths
   * if they know they can never be cancelled.
   *
   * @return true if cancellation can ever be requested, false otherwise
   */
  bool canBeCancelled() const noexcept;

  /**
   * Swaps the underlying state of the cancellation token with the token that is
   * passed-in.
   *
   * @param other The other token to swap the underlying state with
   */
  void swap(CancellationToken& other) noexcept;

  /// Return whether two tokens share the same underlying cancellation state.
  ///
  /// \param a The first token to compare.
  /// \param b The second token to compare.
  /// \returns true if both tokens share the same state, false otherwise.
  friend bool operator==(
      const CancellationToken& a, const CancellationToken& b) noexcept;

 private:
  friend class CancellationCallback;
  friend class CancellationSource;
  friend struct cancellation_token_merge_fn;

  explicit CancellationToken(detail::CancellationStateTokenPtr state) noexcept;

  detail::CancellationStateTokenPtr state_;
};

/// Return whether two tokens share the same underlying cancellation state.
///
/// \param a The first token to compare.
/// \param b The second token to compare.
/// \returns true if both tokens share the same state, false otherwise.
bool operator==(
    const CancellationToken& a, const CancellationToken& b) noexcept;

/// Return whether two tokens do not share the same underlying cancellation
/// state.
///
/// \param a The first token to compare.
/// \param b The second token to compare.
/// \returns true if the tokens differ, false otherwise.
bool operator!=(
    const CancellationToken& a, const CancellationToken& b) noexcept;

/**
 * A CancellationSource object provides the ability to request cancellation of
 * operations that an associated CancellationToken was passed to.
 *
 * Example usage:
 *
 *     CancellationSource cs;
 *     Future<void> f = startSomeOperation(cs.getToken());
 *
 *     // Later...
 *     cs.requestCancellation();
 *
 * @refcode folly/docs/examples/folly/CancellationSource.cpp
 * @class folly::CancellationSource
 */
class CancellationSource {
 public:
  /// Construct to a new, independent cancellation source.
  CancellationSource();

  /**
   * Construct a new reference to the same underlying cancellation state.
   *
   * Either the original or the new copy can be used to request cancellation
   * of associated work.
   *
   * @param other The source to copy from
   */
  CancellationSource(const CancellationSource& other) noexcept;

  /**
   * Construct a source by moving the underlying state of another source.
   *
   * This leaves 'other' in an empty state where 'requestCancellation()' is a
   * no-op and 'canBeCancelled()' returns false.
   *
   * @param other The source to move from
   */
  CancellationSource(CancellationSource&& other) noexcept;

  /// Copy-assign the source so it shares the same underlying state as `other`.
  ///
  /// \param other The source to copy from.
  /// \returns A reference to this source.
  CancellationSource& operator=(const CancellationSource& other) noexcept;

  /// Move-assign the source, taking over the underlying state of `other`.
  ///
  /// \param other The source to move from.
  /// \returns A reference to this source.
  CancellationSource& operator=(CancellationSource&& other) noexcept;

  /**
   * Construct a CancellationSource that cannot be cancelled.
   *
   * This factory function can be used to obtain a CancellationSource that
   * is equivalent to a moved-from CancellationSource object without needing
   * to allocate any shared-state.
   *
   * @return A CancellationSource that cannot be cancelled
   */
  static CancellationSource invalid() noexcept;

  /**
   * Query if cancellation has already been requested on this CancellationSource
   * or any other CancellationSource object copied from the same original
   * CancellationSource object.
   *
   * @return true if cancellation has been requested, false otherwise
   */
  bool isCancellationRequested() const noexcept;

  /**
   * Query if cancellation can be requested through this CancellationSource
   * object. This will only return false if the CancellationSource object has
   * been moved-from.
   *
   * @return true if cancellation can be requested, false otherwise
   */
  bool canBeCancelled() const noexcept;

  /**
   * Obtain a CancellationToken linked to this CancellationSource.
   *
   * This token can be passed into cancellable operations to allow the caller
   * to later request cancellation of that operation.
   *
   * @return A CancellationToken linked to this CancellationSource
   */
  CancellationToken getToken() const noexcept;

  /**
   * Request cancellation of work associated with this CancellationSource.
   *
   * This will ensure subsequent calls to isCancellationRequested() on any
   * CancellationSource or CancellationToken object associated with the same
   * underlying cancellation state to return true.
   *
   * If this is the first call to requestCancellation() on any
   * CancellationSource object with the same underlying state then this call
   * will also execute the callbacks associated with any CancellationCallback
   * objects that were constructed with an associated CancellationToken.
   *
   * Note that it is possible that another thread may be concurrently
   * registering a callback with CancellationCallback. This method guarantees
   * that either this thread will see the callback registration and will
   * ensure that the callback is called, or the CancellationCallback constructor
   * will see the cancellation-requested signal and will execute the callback
   * inline inside the constructor.
   *
   * @return The previous state of 'isCancellationRequested()'. i.e.
   *  - 'true' if cancellation had previously been requested.
   *  - 'false' if this was the first call to request cancellation.
   */
  bool requestCancellation() const noexcept;

  /**
   * Swaps the underlying state of the cancellation source with the source that
   * is passed-in.
   *
   * @param other The other cancellation source to copy the underlying state
   * from.
   */
  void swap(CancellationSource& other) noexcept;

  /// Return whether two sources share the same underlying cancellation state.
  ///
  /// \param a The first source to compare.
  /// \param b The second source to compare.
  /// \returns true if both sources share the same state, false otherwise.
  friend bool operator==(
      const CancellationSource& a, const CancellationSource& b) noexcept;

  /**
   * Returns a pair of <CancellationSource, Data> where the underlying state is
   * created using the arguments that is passed-in.
   *
   * @param tag Tag identifying the data types to associate with the state
   * @param args Arguments used to construct the associated data
   * @return A pair of the new CancellationSource and a pointer to its
   *         associated data tuple
   */
  template <typename... Data, typename... Args>
  static std::pair<CancellationSource, std::tuple<Data...>*> create(
      detail::WithDataTag<Data...> tag, Args&&... args);

 private:
  explicit CancellationSource(
      detail::CancellationStateSourcePtr&& state) noexcept;

  detail::CancellationStateSourcePtr state_;
};

/// Return whether two sources share the same underlying cancellation state.
///
/// \param a The first source to compare.
/// \param b The second source to compare.
/// \returns true if both sources share the same state, false otherwise.
bool operator==(
    const CancellationSource& a, const CancellationSource& b) noexcept;

/// Return whether two sources do not share the same underlying cancellation
/// state.
///
/// \param a The first source to compare.
/// \param b The second source to compare.
/// \returns true if the sources differ, false otherwise.
bool operator!=(
    const CancellationSource& a, const CancellationSource& b) noexcept;

/**
 * A CancellationCallback object registers the callback with the specified
 * CancellationToken such that the callback will be
 * executed if the corresponding CancellationSource object has the
 * requestCancellation() method called on it.
 *
 * If the CancellationToken object already had cancellation requested
 * then the callback will be executed inline on the current thread before
 * the constructor returns. Otherwise, the callback will be executed on
 * in the execution context of the first thread to call requestCancellation()
 * on a corresponding CancellationSource.
 *
 * The callback object must not throw any unhandled exceptions. Doing so
 * will result in the program terminating via std::terminate().
 *
 * A CancellationCallback object is neither copyable nor movable.
 *
 * @refcode folly/docs/examples/folly/CancellationCallback.cpp
 * @class folly::CancellationCallback
 */
class CancellationCallback {
  using VoidFunction = folly::Function<void()>;

 public:
  /// Register a callback to run when the given token is cancelled.
  ///
  /// \param ct The cancellation token to register with.
  /// \param callable The callback to invoke on cancellation.
  template <typename Callable>
    requires std::
        is_constructible<CancellationCallback::VoidFunction, Callable>::value
      CancellationCallback(CancellationToken&& ct, Callable&& callable);

  /// Register a callback to run when the given token is cancelled.
  ///
  /// \param ct The cancellation token to register with.
  /// \param callable The callback to invoke on cancellation.
  template <typename Callable>
    requires std::
        is_constructible<CancellationCallback::VoidFunction, Callable>::value
      CancellationCallback(const CancellationToken& ct, Callable&& callable);

  /**
   * Deregisters the callback from the CancellationToken.
   *
   * If cancellation has been requested concurrently on another thread and the
   * callback is currently executing then the destructor will block until after
   * the callback has returned (otherwise it might be left with a dangling
   * reference).
   *
   * You should generally try to implement your callback functions to be lock
   * free to avoid deadlocks between the callback executing and the
   * CancellationCallback destructor trying to deregister the callback.
   *
   * If the callback has not started executing yet then the callback will be
   * deregistered from the CancellationToken before the destructor completes.
   *
   * Once the destructor returns you can be guaranteed that the callback will
   * not be called by a subsequent call to 'requestCancellation()' on a
   * CancellationSource associated with the CancellationToken passed to the
   * constructor.
   */
  ~CancellationCallback();

  /// Deleted copy constructor; CancellationCallback is not copyable.
  /// \param other The callback that would be copied from.
  CancellationCallback(const CancellationCallback& other) = delete;

  /// Deleted move constructor; CancellationCallback is not movable.
  /// \param other The callback that would be moved from.
  CancellationCallback(CancellationCallback&& other) = delete;

  /// Deleted copy assignment; CancellationCallback is not copyable.
  /// \param other The callback that would be assigned from.
  /// \returns Nothing; this operator is deleted.
  CancellationCallback& operator=(const CancellationCallback& other) = delete;

  /// Deleted move assignment; CancellationCallback is not movable.
  /// \param other The callback that would be assigned from.
  /// \returns Nothing; this operator is deleted.
  CancellationCallback& operator=(CancellationCallback&& other) = delete;

 private:
  friend class detail::CancellationState;

  void invokeCallback() noexcept;

  CancellationCallback* next_;

  // Pointer to the pointer that points to this node in the linked list.
  // This could be the 'next_' of a previous CancellationCallback or could
  // be the 'head_' pointer of the CancellationState.
  // If this node is inserted in the list then this will be non-null.
  CancellationCallback** prevNext_;

  detail::CancellationState* state_;
  VoidFunction callback_;

  // Pointer to a flag stored on the stack of the caller to invokeCallback()
  // that is used to indicate to the caller of invokeCallback() that the
  // destructor has run and it is no longer valid to access the callback
  // object.
  bool* destructorHasRunInsideCallback_;

  // Flag used to signal that the callback has completed executing on another
  // thread and it is now safe to exit the destructor.
  std::atomic<bool> callbackCompleted_;
};

/**
 * Obtain a CancellationToken linked to any number of other
 * CancellationTokens.
 *
 * This token will have cancellation requested when any of the passed-in
 * tokens do.
 * This token is cancellable if any of the passed-in tokens are at the time of
 * construction.
 *
 * Example:
 *   CancellationSource a,b;
 *   auto c = cancellation_token_merge(a.getToken(), b.getToken());
 */
struct cancellation_token_merge_fn {
  /// Merge the given tokens into a single CancellationToken.
  ///
  /// \param tokens The cancellation tokens to merge.
  /// \returns A token that is cancelled when any of `tokens` is cancelled.
  template <typename... Ts>
  CancellationToken operator()(Ts&&... tokens) const;
};

/// Callable object that merges several CancellationTokens into one.
inline constexpr cancellation_token_merge_fn cancellation_token_merge{};

} // namespace folly

#include <folly/CancellationToken-inl.h>
