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

#include <folly/CancellationToken.h>
#include <folly/Executor.h>
#include <folly/Optional.h>
#include <folly/coro/AsyncGenerator.h>
#include <folly/coro/Task.h>
#include <folly/futures/Future.h>
#include <folly/synchronization/Baton.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

/// A Rust-style pollable adaptor over a `Task` or `SemiFuture`.
template <typename T>
class PollFuture final : private Executor {
 public:
  /// The poll result type: engaged when the future has a value ready.
  using Poll = Optional<lift_unit_t<T>>;
  /// Callback invoked to wake the poller when progress can be made.
  using Waker = Function<void()>;

  /// Constructs a pollable future that drives the given Task.
  /// @param task The task to poll for its result.
  explicit PollFuture(Task<T> task) {
    Executor* self = this;
    co_withExecutor(makeKeepAlive(self), std::move(task))
        .start(
            [&](Try<T>&& result) noexcept {
              // Rust doesn't support exceptions
              DCHECK(!result.hasException());
              if constexpr (!std::is_same_v<T, void>) {
                result_ = std::move(result).value();
              } else {
                result_ = unit;
              }
            },
            cancellationSource_.getToken());
  }

  /// Constructs a pollable future that drives the given SemiFuture.
  /// @param future The SemiFuture to poll for its result.
  explicit PollFuture(SemiFuture<lift_unit_t<T>> future) {
    Executor* self = this;
    std::move(future)
        .via(makeKeepAlive(self))
        .setCallback_([&](Executor::KeepAlive<>&&, Try<T>&& result) mutable {
          result_ = std::move(result).value();
        });
  }

  /// Destroys the future, cancelling and draining any pending work first.
  ~PollFuture() override {
    cancellationSource_.requestCancellation();
    if (keepAliveCount_.load(std::memory_order_relaxed) > 0) {
      folly::Baton<> b;
      while (!poll([&] { b.post(); })) {
        b.wait();
        b.reset();
      }
    }
  }

  /// Copy construction is disabled; a PollFuture is neither copyable nor movable.
  PollFuture(const PollFuture& other) = delete;
  /// Copy assignment is disabled; a PollFuture is neither copyable nor movable.
  PollFuture& operator=(const PollFuture& other) = delete;
  /// Move construction is disabled; a PollFuture is neither copyable nor movable.
  PollFuture(PollFuture&& other) = delete;
  /// Move assignment is disabled; a PollFuture is neither copyable nor movable.
  PollFuture& operator=(PollFuture&& other) = delete;

  /// Polls the future for its result, registering a waker if pending.
  /// @param waker The callback invoked when the future may make progress.
  /// @returns The result, or none if not yet ready.
  Poll poll(Waker waker) {
    while (true) {
      std::queue<Func> funcs;
      {
        auto wQueueAndWaker = queueAndWaker_.wlock();
        if (wQueueAndWaker->funcs.empty()) {
          wQueueAndWaker->waker = std::move(waker);
          break;
        }

        std::swap(funcs, wQueueAndWaker->funcs);
      }

      while (!funcs.empty()) {
        funcs.front()();
        funcs.pop();
      }
    }

    if (keepAliveCount_.load(std::memory_order_relaxed) == 0) {
      return std::move(result_);
    }
    return none;
  }

 private:
  void add(Func func) override {
    auto waker = [&] {
      auto wQueueAndWaker = queueAndWaker_.wlock();
      wQueueAndWaker->funcs.push(std::move(func));
      return std::exchange(wQueueAndWaker->waker, {});
    }();
    if (waker) {
      waker();
    }
  }

  bool keepAliveAcquire() noexcept override {
    auto keepAliveCount =
        keepAliveCount_.fetch_add(1, std::memory_order_relaxed);
    DCHECK(keepAliveCount > 0);
    return true;
  }

  void keepAliveRelease() noexcept override {
    auto keepAliveCount = keepAliveCount_.load(std::memory_order_relaxed);
    do {
      DCHECK(keepAliveCount > 0);
      if (keepAliveCount == 1) {
        add([this] {
          // the final count *must* be released from this executor so that we
          // don't race with poll.
          keepAliveCount_.fetch_sub(1, std::memory_order_relaxed);
        });
        return;
      }
    } while (!keepAliveCount_.compare_exchange_weak(
        keepAliveCount,
        keepAliveCount - 1,
        std::memory_order_release,
        std::memory_order_relaxed));
  }

  struct QueueAndWaker {
    std::queue<Func> funcs;
    Waker waker;
  };
  Synchronized<QueueAndWaker> queueAndWaker_;
  std::atomic<ssize_t> keepAliveCount_{1};
  Optional<lift_unit_t<T>> result_;
  CancellationSource cancellationSource_;
};

/// A Rust-style pollable adaptor over an `AsyncGenerator` stream.
template <typename T>
class PollStream {
 public:
  /// The poll result type: outer optional means ready, inner means end-of-stream.
  using Poll = Optional<Optional<T>>;
  /// Callback invoked to wake the poller when progress can be made.
  using Waker = Function<void()>;

  /// Constructs a pollable stream that drives the given async generator.
  /// @param asyncGenerator The generator to poll for elements.
  explicit PollStream(AsyncGenerator<T> asyncGenerator)
      : asyncGenerator_(std::move(asyncGenerator)) {}

  /// Polls the stream for the next element, registering a waker if pending.
  /// @param waker The callback invoked when the stream may make progress.
  /// @returns The next element, or none if not yet ready.
  Poll poll(Waker waker) {
    if (!nextFuture_) {
      nextFuture_.emplace(getNext());
    }

    auto nextPoll = nextFuture_->poll(std::move(waker));
    if (!nextPoll) {
      return none;
    }

    nextFuture_.reset();
    return nextPoll;
  }

 private:
  Task<Optional<T>> getNext() {
    auto next = co_await asyncGenerator_.next();
    if (next) {
      co_return std::move(next).value();
    }
    co_return none;
  }

  AsyncGenerator<T> asyncGenerator_;
  Optional<PollFuture<Optional<T>>> nextFuture_;
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
