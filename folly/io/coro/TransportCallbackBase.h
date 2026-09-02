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

#include <folly/Portability.h>

#include <folly/coro/Baton.h>
#include <folly/coro/Task.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/ssl/SSLErrors.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

//
// Common base for all callbcaks
//

/// Common base for all transport callbacks.
class TransportCallbackBase {
 public:
  /// Constructs a callback base bound to `transport`.
  ///
  /// \param transport Transport this callback operates on.
  explicit TransportCallbackBase(folly::AsyncTransport& transport)
      : transport_{transport} {}

  /// Destroys the callback base.
  virtual ~TransportCallbackBase() noexcept = default;

  /// Returns the stored error, if any.
  ///
  /// \returns A reference to the stored error wrapper.
  folly::exception_wrapper& error() noexcept { return error_; }
  /// Signals completion to the waiting coroutine.
  void post() noexcept { baton_.post(); }
  /// Waits for the callback to complete, honoring cancellation.
  ///
  /// \returns A task that completes when the callback is posted.
  Task<folly::Unit> wait() {
    auto cancelToken = co_await co_current_cancellation_token;
    if (cancelToken.isCancellationRequested()) {
      cancel();
      co_yield folly::coro::co_stopped_may_throw;
    }
    folly::CancellationCallback cancellationCallback{
        cancelToken, [this] {
          this->post();
          VLOG(5) << "Cancellation was called";
        }};

    co_await baton_;
    VLOG(5) << "After baton await";

    if (cancelToken.isCancellationRequested()) {
      cancel();
      co_yield folly::coro::co_stopped_may_throw;
    }
    co_return folly::unit;
  }

 protected:
  /// Baton used to notify the other side of completion.
  Baton baton_;
  /// Transport whose state the callback modifies, e.g. to cancel callbacks.
  folly::AsyncTransport& transport_;

  /// Wraps any AsyncTransport error.
  folly::exception_wrapper error_;

  /// Stores `ex` as the callback's error, preserving SSL error details.
  ///
  /// \param ex Exception to store.
  void storeException(const folly::AsyncSocketException& ex) {
    auto sslErr = dynamic_cast<const folly::SSLException*>(&ex);
    if (sslErr) {
      error_ = folly::make_exception_wrapper<folly::SSLException>(*sslErr);
    } else {
      error_ = folly::make_exception_wrapper<folly::AsyncSocketException>(ex);
    }
  }

 private:
  virtual void cancel() noexcept = 0;
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
