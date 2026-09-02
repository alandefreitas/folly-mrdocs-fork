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

#include <concepts>
#include <cstddef>
#include <new>
#include <stdexcept>

#include <folly/io/IOBuf.h>

/// Memory management utilities.
namespace folly::memory {

/// Requirements for a singleton allocator usable with an IOBuf arena factory.
///
/// A conforming type exposes a static `initialized()` query convertible to
/// `bool` and a static `allocate(size_t)` returning a raw `void*` buffer.
template <typename T>
concept IOBufSingletonAllocator = requires(size_t sz) {
  { T::initialized() } -> std::convertible_to<bool>;
  { T::allocate(sz) } -> std::same_as<void*>;
};

/// Builds an IOBuf factory backed by the given singleton allocator.
///
/// The returned factory allocates buffers through `Allocator` and wraps them in
/// IOBuf objects that free the memory using `IOBuf::SIZED_FREE`. Invoking the
/// factory throws `std::runtime_error` if the allocator is not initialized and
/// `std::bad_alloc` if allocation fails.
///
/// \tparam Allocator The singleton allocator satisfying `IOBufSingletonAllocator`.
/// \returns An IOBuf factory that allocates through `Allocator`.
template <IOBufSingletonAllocator Allocator>
IOBufFactory makeIOBufArenaFactory() {
  return [](size_t capacity) -> std::unique_ptr<IOBuf> {
    if (!Allocator::initialized()) {
      throw std::runtime_error("Allocator is not initialized");
    }
    void* data = Allocator::allocate(capacity);
    if (!data) {
      throw std::bad_alloc();
    }
    return IOBuf::takeOwnership(IOBuf::SIZED_FREE, data, capacity, 0, 0);
  };
}

} // namespace folly::memory
