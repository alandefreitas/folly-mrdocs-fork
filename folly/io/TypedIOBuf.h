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

#include <algorithm>
#include <iterator>
#include <type_traits>

#include <folly/io/IOBuf.h>
#include <folly/memory/Malloc.h>

/// Top-level Folly namespace.
namespace folly {

/**
 * Wrapper class to handle a IOBuf as a typed buffer (to a standard layout
 * class).
 *
 * This class punts on alignment, and assumes that you know what you're doing.
 *
 * All methods are wrappers around the corresponding IOBuf methods.  The
 * TypedIOBuf object is stateless, so it's perfectly okay to access the
 * underlying IOBuf in between TypedIOBuf method calls.
 */
template <class T>
class TypedIOBuf {
  static_assert(std::is_standard_layout<T>::value, "must be standard layout");

 public:
  /// The element type stored in the buffer.
  using value_type = T;
  /// Reference to an element.
  using reference = value_type&;
  /// Const reference to an element.
  using const_reference = const value_type&;
  /// Type used for element counts.
  using size_type = uint32_t;
  /// Mutable element iterator.
  using iterator = value_type*;
  /// Const element iterator.
  using const_iterator = const value_type*;

  /// Constructs a typed view over the given IOBuf.
  ///
  /// \param buf The IOBuf to view as a typed buffer.
  explicit TypedIOBuf(IOBuf* buf) : buf_(buf) {}

  /// Returns the underlying IOBuf.
  ///
  /// \returns The underlying IOBuf.
  IOBuf* ioBuf() { return buf_; }
  /// Returns the underlying IOBuf.
  ///
  /// \returns The underlying IOBuf.
  const IOBuf* ioBuf() const { return buf_; }

  /// Returns true if the buffer holds no elements.
  ///
  /// \returns true if the buffer holds no elements.
  bool empty() const { return buf_->empty(); }
  /// Returns a pointer to the first element.
  ///
  /// \returns A pointer to the first element.
  const T* data() const { return cast(buf_->data()); }
  /// Returns a writable pointer to the first element.
  ///
  /// \returns A writable pointer to the first element.
  T* writableData() { return cast(buf_->writableData()); }
  /// Returns a pointer just past the last element.
  ///
  /// \returns A pointer just past the last element.
  const T* tail() const { return cast(buf_->tail()); }
  /// Returns a writable pointer just past the last element.
  ///
  /// \returns A writable pointer just past the last element.
  T* writableTail() { return cast(buf_->writableTail()); }
  /// Returns the number of elements in the buffer.
  ///
  /// \returns The number of elements in the buffer.
  uint32_t length() const { return sdiv(buf_->length()); }
  /// Returns the number of elements in the buffer.
  ///
  /// \returns The number of elements in the buffer.
  uint32_t size() const { return length(); }

  /// Returns the available headroom, in elements.
  ///
  /// \returns The available headroom, in elements.
  uint32_t headroom() const { return sdiv(buf_->headroom()); }
  /// Returns the available tailroom, in elements.
  ///
  /// \returns The available tailroom, in elements.
  uint32_t tailroom() const { return sdiv(buf_->tailroom()); }
  /// Returns a pointer to the start of the allocated buffer.
  ///
  /// \returns A pointer to the start of the allocated buffer.
  const T* buffer() const { return cast(buf_->buffer()); }
  /// Returns a writable pointer to the start of the allocated buffer.
  ///
  /// \returns A writable pointer to the start of the allocated buffer.
  T* writableBuffer() { return cast(buf_->writableBuffer()); }
  /// Returns a pointer just past the end of the allocated buffer.
  ///
  /// \returns A pointer just past the end of the allocated buffer.
  const T* bufferEnd() const { return cast(buf_->bufferEnd()); }
  /// Returns the buffer capacity, in elements.
  ///
  /// \returns The buffer capacity, in elements.
  uint32_t capacity() const { return sdiv(buf_->capacity()); }
  /// Advances the data pointer forward by n elements.
  ///
  /// \param n The number of elements to advance by.
  void advance(uint32_t n) { buf_->advance(smul(n)); }
  /// Moves the data pointer backward by n elements.
  ///
  /// \param n The number of elements to move back by.
  void retreat(uint32_t n) { buf_->retreat(smul(n)); }
  /// Prepends n elements from the headroom.
  ///
  /// \param n The number of elements to prepend.
  void prepend(uint32_t n) { buf_->prepend(smul(n)); }
  /// Appends n elements from the tailroom.
  ///
  /// \param n The number of elements to append.
  void append(uint32_t n) { buf_->append(smul(n)); }
  /// Removes n elements from the start.
  ///
  /// \param n The number of elements to remove.
  void trimStart(uint32_t n) { buf_->trimStart(smul(n)); }
  /// Removes n elements from the end.
  ///
  /// \param n The number of elements to remove.
  void trimEnd(uint32_t n) { buf_->trimEnd(smul(n)); }
  /// Resets the buffer to empty.
  void clear() { buf_->clear(); }
  /// Reserves at least the given headroom and tailroom, in elements.
  ///
  /// \param minHeadroom The minimum headroom to reserve, in elements.
  /// \param minTailroom The minimum tailroom to reserve, in elements.
  void reserve(uint32_t minHeadroom, uint32_t minTailroom) {
    buf_->reserve(smul(minHeadroom), smul(minTailroom));
  }
  /// Reserves at least the given tailroom, in elements.
  ///
  /// \param minTailroom The minimum tailroom to reserve, in elements.
  void reserve(uint32_t minTailroom) { reserve(0, minTailroom); }

  /// Returns a const iterator to the first element.
  ///
  /// \returns A const iterator to the first element.
  const T* cbegin() const { return data(); }
  /// Returns a const iterator just past the last element.
  ///
  /// \returns A const iterator just past the last element.
  const T* cend() const { return tail(); }
  /// Returns a const iterator to the first element.
  ///
  /// \returns A const iterator to the first element.
  const T* begin() const { return cbegin(); }
  /// Returns a const iterator just past the last element.
  ///
  /// \returns A const iterator just past the last element.
  const T* end() const { return cend(); }
  /// Returns an iterator to the first element.
  ///
  /// \returns An iterator to the first element.
  T* begin() { return writableData(); }
  /// Returns an iterator just past the last element.
  ///
  /// \returns An iterator just past the last element.
  T* end() { return writableTail(); }

  /// Returns the first element.
  ///
  /// \returns The first element.
  const T& front() const {
    assert(!empty());
    return *begin();
  }
  /// Returns the first element.
  ///
  /// \returns The first element.
  T& front() {
    assert(!empty());
    return *begin();
  }
  /// Returns the last element.
  ///
  /// \returns The last element.
  const T& back() const {
    assert(!empty());
    return end()[-1];
  }
  /// Returns the last element.
  ///
  /// \returns The last element.
  T& back() {
    assert(!empty());
    return end()[-1];
  }

  /**
   * Simple wrapper to make it easier to treat this TypedIOBuf as an array of
   * T.
   * \param idx Zero-based index of the element.
   * \returns The element at the given index.
   */
  const T& operator[](ssize_t idx) const {
    assert(idx >= 0 && idx < length());
    return data()[idx];
  }

  /// Returns the writable element at the given index.
  ///
  /// \param idx Zero-based index of the element.
  /// \returns The writable element at the given index.
  T& operator[](ssize_t idx) {
    assert(idx >= 0 && idx < length());
    return writableData()[idx];
  }

  /**
   * Append one element.
   * \param data The element to append.
   */
  void push(const T& data) { push(&data, &data + 1); }
  /// Appends one element.
  ///
  /// \param data The element to append.
  void push_back(const T& data) { push(data); }

  /**
   * Append multiple elements in a sequence; will call distance().
   * \param begin Iterator to the first element to append.
   * \param end Iterator just past the last element to append.
   */
  template <class IT>
  void push(IT begin, IT end) {
    uint32_t n = std::distance(begin, end);
    if (usingJEMalloc()) {
      // Rely on xallocx() and avoid exponential growth to limit
      // amount of memory wasted.
      reserve(headroom(), n);
    } else if (tailroom() < n) {
      reserve(headroom(), std::max(n, 3 + size() / 2));
    }
    std::copy(begin, end, writableTail());
    append(n);
  }

  /// Move constructor.
  ///
  /// \param other The buffer view to move from.
  TypedIOBuf(TypedIOBuf&& other) = default;
  /// Move assignment operator.
  ///
  /// \param other The buffer view to move from.
  /// \returns A reference to this buffer view.
  TypedIOBuf& operator=(TypedIOBuf&& other) = default;

 private:
  // Non-copyable
  TypedIOBuf(const TypedIOBuf&) = delete;
  TypedIOBuf& operator=(const TypedIOBuf&) = delete;

  // cast to T*
  static T* cast(uint8_t* p) { return reinterpret_cast<T*>(p); }
  static const T* cast(const uint8_t* p) {
    return reinterpret_cast<const T*>(p);
  }
  // divide by size
  static uint32_t sdiv(uint32_t n) { return n / sizeof(T); }
  // multiply by size
  static uint32_t smul(uint32_t n) {
    // In debug mode, check for overflow
    assert((uint64_t(n) * sizeof(T)) < (uint64_t(1) << 32));
    return n * sizeof(T);
  }

  IOBuf* buf_;
};

} // namespace folly
