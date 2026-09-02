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

#include <cassert>
#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

#include <folly/coro/Coroutine.h>
#include <folly/coro/Invoke.h>
#include <folly/lang/Exception.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

/// A synchronous coroutine generator that lazily yields a sequence of values.
template <typename T>
class Generator {
 public:
  /// Coroutine promise type that drives a `Generator` and its nested children.
  class promise_type final {
   public:
    /// Constructs a promise for a root generator coroutine.
    promise_type() noexcept
        : m_value(nullptr),
          m_exception(nullptr),
          m_root(this),
          m_parentOrLeaf(this) {}

    /// Deleted copy constructor; the promise is non-copyable.
    ///
    /// @param other The promise that would be copied.
    promise_type(const promise_type& other) = delete;
    /// Deleted move constructor; the promise is non-movable.
    ///
    /// @param other The promise that would be moved.
    promise_type(promise_type&& other) = delete;

    /// Builds the `Generator` object returned to the coroutine's caller.
    ///
    /// @returns A `Generator` owning this promise's coroutine.
    auto get_return_object() noexcept { return Generator<T>{*this}; }

    /// Suspends the coroutine before it starts running.
    ///
    /// @returns An awaitable that always suspends.
    suspend_always initial_suspend() noexcept { return {}; }

    /// Suspends the coroutine after it completes.
    ///
    /// @returns An awaitable that always suspends.
    suspend_always final_suspend() noexcept { return {}; }

    /// Captures the currently propagating exception for later rethrow.
    void unhandled_exception() noexcept { m_exception = current_exception(); }

    /// Handles normal completion of the generator coroutine.
    void return_void() noexcept {}

    /// Yields an lvalue element from the coroutine.
    ///
    /// @param value The value to yield to the consumer.
    /// @returns An awaitable that always suspends the coroutine.
    suspend_always yield_value(T& value) noexcept {
      m_value = std::addressof(value);
      return {};
    }

    /// Yields an rvalue element from the coroutine.
    ///
    /// @param value The value to yield to the consumer.
    /// @returns An awaitable that always suspends the coroutine.
    suspend_always yield_value(T&& value) noexcept {
      m_value = std::addressof(value);
      return {};
    }

    /// Yields all values of a nested rvalue generator (recursive yield).
    ///
    /// @param generator The nested generator to delegate to.
    /// @returns An awaitable that drives the nested generator to completion.
    auto yield_value(Generator&& generator) noexcept {
      return yield_value(generator);
    }

    /// Yields all values of a nested generator (recursive yield).
    ///
    /// @param generator The nested generator to delegate to.
    /// @returns An awaitable that drives the nested generator to completion.
    auto yield_value(Generator& generator) noexcept {
      struct awaitable {
        awaitable(promise_type* childPromise) : m_childPromise(childPromise) {}

        bool await_ready() noexcept { return this->m_childPromise == nullptr; }

        void await_suspend(coroutine_handle<promise_type>) noexcept {}

        void await_resume() {
          if (this->m_childPromise != nullptr) {
            this->m_childPromise->throw_if_exception();
          }
        }

       private:
        promise_type* m_childPromise;
      };

      if (generator.m_promise != nullptr) {
        m_root->m_parentOrLeaf = generator.m_promise;
        generator.m_promise->m_root = m_root;
        generator.m_promise->m_parentOrLeaf = this;
        generator.m_promise->resume();

        // NB: This branch looks like a (premature?) optimization for empty
        // generators, and until proven otherwise in benchmarks, it may be
        // advantageous to simply return `awaitable{generator.m_promise}`.
        if (!generator.m_promise->is_complete() ||
            generator.m_promise->m_exception != nullptr) {
          return awaitable{generator.m_promise};
        }

        m_root->m_parentOrLeaf = this;
      }

      return awaitable{nullptr};
    }

    /// Deleted to forbid any use of `co_await` inside the generator coroutine.
    ///
    /// @param value The awaited value (never usable; the overload is deleted).
    template <typename U>
    void await_transform(U&& value) = delete;

    /// Destroys the coroutine frame associated with this promise.
    void destroy() noexcept {
      coroutine_handle<promise_type>::from_promise(*this).destroy();
    }

    /// Rethrows any exception captured while running the coroutine.
    void throw_if_exception() {
      if (m_exception != nullptr) {
        std::rethrow_exception(std::move(m_exception));
      }
    }

    /// Returns whether the coroutine has run to completion.
    ///
    /// @returns `true` if the coroutine is done, `false` otherwise.
    bool is_complete() noexcept {
      return coroutine_handle<promise_type>::from_promise(*this).done();
    }

    /// Returns the current value yielded by the active leaf coroutine.
    ///
    /// @returns A reference to the current value.
    T& value() noexcept {
      assert(this == m_root);
      assert(!is_complete());
      return *(m_parentOrLeaf->m_value);
    }

    /// Resumes the coroutine chain to produce the next value.
    void pull() noexcept {
      assert(this == m_root);
      assert(!m_parentOrLeaf->is_complete());

      m_parentOrLeaf->resume();

      while (m_parentOrLeaf != this && m_parentOrLeaf->is_complete()) {
        m_parentOrLeaf = m_parentOrLeaf->m_parentOrLeaf;
        m_parentOrLeaf->resume();
      }
    }

   private:
    void resume() noexcept {
      coroutine_handle<promise_type>::from_promise(*this).resume();
    }

    std::add_pointer_t<T> m_value;
    std::exception_ptr m_exception;

    promise_type* m_root;

    // If this is the promise of the root generator then this field
    // is a pointer to the leaf promise.
    // For non-root generators this is a pointer to the parent promise.
    promise_type* m_parentOrLeaf;
  };

  /// Constructs an empty generator not attached to any coroutine.
  Generator() noexcept : m_promise(nullptr) {}

  /// Constructs a generator that owns the coroutine of `promise`.
  ///
  /// @param promise The coroutine promise to take ownership of.
  Generator(promise_type& promise) noexcept : m_promise(&promise) {}

  /// Move-constructs a generator, transferring ownership of the coroutine.
  ///
  /// @param other The generator to move from.
  Generator(Generator&& other) noexcept : m_promise(other.m_promise) {
    other.m_promise = nullptr;
  }

  /// Deleted copy constructor; a generator is move-only.
  ///
  /// @param other The generator that would be copied.
  Generator(const Generator& other) = delete;
  /// Deleted copy assignment; a generator is move-only.
  ///
  /// @param other The generator that would be copied.
  /// @returns A reference to this generator.
  Generator& operator=(const Generator& other) = delete;

  /// Destroys the generator and its underlying coroutine, if any.
  ~Generator() {
    if (m_promise != nullptr) {
      m_promise->destroy();
    }
  }

  /// Move-assigns from another generator, destroying any current state.
  ///
  /// @param other The generator to move from.
  /// @returns A reference to this generator.
  Generator& operator=(Generator&& other) noexcept {
    if (this != &other) {
      if (m_promise != nullptr) {
        m_promise->destroy();
      }

      m_promise = other.m_promise;
      other.m_promise = nullptr;
    }

    return *this;
  }

  /// Input iterator over the values produced by the generator.
  class iterator {
   public:
    /// The iterator category (input iterator).
    using iterator_category = std::input_iterator_tag;
    // What type should we use for counting elements of a potentially infinite
    // sequence?
    /// The type used to represent distance between iterators.
    using difference_type = std::ptrdiff_t;
    /// The element value type, with references removed.
    using value_type = std::remove_reference_t<T>;
    /// The reference type returned when dereferencing the iterator.
    using reference = std::conditional_t<std::is_reference_v<T>, T, T&>;
    /// The pointer type returned by `operator->`.
    using pointer = std::add_pointer_t<T>;

    /// Constructs a past-the-end iterator.
    iterator() noexcept : m_promise(nullptr) {}

    /// Constructs an iterator referring to the given promise.
    ///
    /// @param promise The promise whose current value the iterator exposes.
    explicit iterator(promise_type* promise) noexcept : m_promise(promise) {}

    /// Compares two iterators for equality.
    ///
    /// @param other The iterator to compare with.
    /// @returns `true` if the iterators refer to the same position.
    bool operator==(const iterator& other) const noexcept {
      return m_promise == other.m_promise;
    }

    /// Compares two iterators for inequality.
    ///
    /// @param other The iterator to compare with.
    /// @returns `true` if the iterators refer to different positions.
    bool operator!=(const iterator& other) const noexcept {
      return m_promise != other.m_promise;
    }

    /// Advances the iterator to the next element (pre-increment).
    ///
    /// @returns A reference to this iterator.
    iterator& operator++() {
      assert(m_promise != nullptr);
      assert(!m_promise->is_complete());

      m_promise->pull();
      if (m_promise->is_complete()) {
        auto* temp = m_promise;
        m_promise = nullptr;
        temp->throw_if_exception();
      }

      return *this;
    }

    /// Advances the iterator to the next element (post-increment).
    ///
    /// @param n Unused post-increment disambiguator.
    void operator++(int n) { (void)operator++(); }

    /// Accesses the current element.
    ///
    /// @returns A reference to the current element.
    reference operator*() const noexcept {
      assert(m_promise != nullptr);
      return static_cast<reference>(m_promise->value());
    }

    /// Accesses the current element through a pointer.
    ///
    /// @returns A pointer to the current element.
    pointer operator->() const noexcept { return std::addressof(operator*()); }

   private:
    promise_type* m_promise;
  };

  /// Starts the generator and returns an iterator to the first element.
  ///
  /// @returns An iterator to the first element, or the end iterator if empty.
  iterator begin() {
    if (m_promise != nullptr) {
      m_promise->pull();
      if (!m_promise->is_complete()) {
        return iterator(m_promise);
      }

      m_promise->throw_if_exception();
    }

    return iterator(nullptr);
  }

  /// Returns an iterator marking the end of the sequence.
  ///
  /// @returns A past-the-end iterator.
  iterator end() noexcept { return iterator(nullptr); }

  /// Swaps the state of this generator with another.
  ///
  /// @param other The generator to swap with.
  void swap(Generator& other) noexcept {
    std::swap(m_promise, other.m_promise);
  }

  /// Builds a `Generator` by invoking `f` with `a...` via `co_invoke`.
  ///
  /// @param invokeTag Unused `co_invoke` customization tag.
  /// @param genTag Unused tag carrying the generator and callable types.
  /// @param f The callable to invoke.
  /// @param a The arguments to forward to `f`.
  /// @returns A `Generator` yielding the elements produced by `f`.
  template <typename F, typename... A, typename F_, typename... A_>
  friend Generator tag_invoke(
      tag_t<co_invoke_fn> invokeTag,
      tag_t<Generator, F, A...> genTag,
      F_ f,
      A_... a) {
    auto&& r = std::invoke(static_cast<F&&>(f), static_cast<A&&>(a)...);
    for (auto&& v : r) {
      co_yield std::move(v);
    }
  }

 private:
  friend class promise_type;

  promise_type* m_promise;
};

/// Swaps the state of two generators.
///
/// @param a The first generator.
/// @param b The second generator.
template <typename T>
void swap(Generator<T>& a, Generator<T>& b) noexcept {
  a.swap(b);
}
} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
