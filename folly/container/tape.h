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
#include <folly/Range.h>
#include <folly/container/Iterator.h>
#include <folly/container/detail/tape_detail.h>
#include <folly/memory/UninitializedMemoryHacks.h>

#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <iterator>
#include <numeric>
#include <string_view>
#include <type_traits>
#include <vector>

#include <ranges>

namespace folly {

/* # Tape
 *
 * A container adapter, that builds a version of `vector<vector>` on top of a
 * random access underlying container.
 *
 * Instead of having a container of containers it's more efficient to have
 * a single container and store where the separators are.
 *
 * [string second string third string]
 *  ^      ^             ^
 *
 * One subrange of internal elements we call a `record`.
 *
 * You can `push` a new `record` or pop one from the back.
 * We also support an `erase` like `std::vector` but `insert` only for one
 * element. (there is no reason for limitation, except it's not implemented).
 *
 * NOTE: for when you don't have the `record` ready, you can use a
 * `record_builder` interface.
 *
 * Existing `records` can be accessed by index.
 * Existing `records` cannot be mutated, except for the last record (see record
 * builder).
 *
 * ## tape<tape>
 *
 * tape<tape> is supported, though not all of the APIs.
 * More apis can be implemented if/when needed.
 * Use `record_builder`.
 *
 * ## PERFORMANCE CHARACTERISTICS (folly/container/test/tape_bench):
 *
 * Reading (cache miss):
 * Container performs much better for access than vector<vector>/vector<string>
 * for cases where the data is out of cache.
 * If the data is in cache, reading is roughly the same.
 *
 * Construction
 * If you know for a fact that all the elements are fitting into SSO buffer,
 * and you always have complete records (not building) then `tape` does not help
 * you, or can even be a slight regression.
 *
 * Otherwise tape can give you good speedups, especially if you need to
 * `push_back` on individual records.
 *
 * Potential future perf improvements.
 * * it is possible to do a tape with one allocation for both metadata and
 *   data (in special cases).
 * * when converting indexes to pointers, compiler has to shift.
 *   For contigious containers we can store offsets in bytes.
 *
 * ## Exception safety
 * We provide only basic exception safety: the object is destructible or
 * assignable.
 * `std::bad_alloc` is assumed to never happen (a function that uses malloc can
 * be marked noexcept).
 *
 * ## NAME TAPE
 *
 * Name tape is taken from a lecture by Alexander Stepanov but we are not 100%
 * sure if this is the container he had in mind.
 */
template <std::ranges::random_access_range Container>
class tape;

/// string_tape - a common usecase.
using string_tape = tape<std::vector<char>>;

/// A container adapter that builds a version of `vector<vector>` on top of a
/// random access underlying container.
///
/// Instead of a container of containers, it stores a single container plus the
/// positions of the record separators. One subrange of internal elements is
/// called a `record`.
template <std::ranges::random_access_range Container>
class tape {
  using ref_traits = detail::tape_reference_traits<Container>;

 public:
  /// The underlying container type.
  using container_type = Container;

  /// A const reference to a record.
  using const_reference = typename ref_traits::reference;
  /// A reference to a record.
  using reference = const_reference;

  /// The value type of the tape.
  ///
  /// value_type for tape does not make much sense. The best we found is to
  /// make the reference type be the value type. This does not quite make sense
  /// but works well enough.
  using value_type = const_reference;
  /// The value type of an individual scalar element.
  using scalar_value_type = detail::maybe_range_value_t<container_type>;

  /// An unsigned integer type for sizes.
  using size_type = typename Container::size_type;
  /// A signed integer type for differences.
  using difference_type = typename Container::difference_type;

  /// A random access iterator over records.
  using iterator = folly::index_iterator<const tape>;
  /// A const iterator over records.
  using const_iterator = iterator;
  /// A reverse iterator over records.
  using reverse_iterator = std::reverse_iterator<iterator>;
  /// A const reverse iterator over records.
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  // concepts (defined as statics because no local concepts) ------

  /// Whether iterator `I` yields values convertible to a scalar.
  template <typename I>
  static constexpr bool iterator_of_scalars =
      std::is_convertible_v<iterator_value_type_t<I>, scalar_value_type>;

  /// Whether range `I` is a range of scalars.
  template <typename I>
  static constexpr bool range_of_scalars =
      iterator_of_scalars<detail::maybe_range_const_iterator_t<I>>;

  /// Whether iterator `I` yields records (ranges of scalars).
  template <typename I>
  static constexpr bool iterator_of_records =
      range_of_scalars<iterator_value_type_t<I>>;

  /// Whether range `I` is a range of records.
  template <typename I>
  static constexpr bool range_of_records =
      iterator_of_records<detail::maybe_range_const_iterator_t<I>>;

  // rule of 5
  /// Copy constructor.
  ///
  /// \param other The tape to copy from.
  tape(const tape& other) = default;
  /// Copy assignment operator.
  ///
  /// \param other The tape to copy from.
  /// \returns A reference to this tape.
  tape& operator=(const tape& other) = default;
  /// Destructor.
  ~tape() = default;
  /// Move constructor.
  ///
  /// \param other The tape to move from.
  tape(tape&& other) noexcept;
  /// Move assignment operator.
  ///
  /// \param other The tape to move from.
  /// \returns A reference to this tape.
  tape& operator=(tape&& other) noexcept;

  // constructors -----
  /// Default constructor.
  tape() noexcept = default;

  /// Constructs a tape from the range of records `[f, l)`.
  ///
  /// \param f Iterator to the first record.
  /// \param l Sentinel past the last record.
  template <typename I, std::sentinel_for<I> S>
  explicit tape(I f, S l)
    requires iterator_of_records<I>
  {
    range_constructor(f, l);
  }

  /// Constructs a tape from an initializer list of records.
  ///
  /// \param il The initializer list of records.
  template <typename R>
    requires(
        !std::is_same_v<std::remove_cvref_t<R>, tape> &&
        (std::is_convertible_v<R, const_reference> || // const char*
         range_of_records<R>))
  explicit tape(std::initializer_list<R> il) {
    range_constructor(il.begin(), il.end());
  }

  // access ------

  /// Returns the record at index `i`.
  ///
  /// \param i The index of the record to access.
  /// \returns A reference to the record at index `i`.
  [[nodiscard]] const_reference operator[](size_type i) const noexcept {
    return ref_traits::make(
        data_.begin() + markers_[i], data_.begin() + markers_[i + 1]);
  }

  /// Returns the record at index `i`, with bounds checking.
  ///
  /// \param i The index of the record to access.
  /// \returns A reference to the record at index `i`.
  [[nodiscard]] const_reference at(size_type i) const {
    if (FOLLY_UNLIKELY(i >= size())) {
      // libc++ doesn't provide index. This helps optimizations.
      throw std::out_of_range("tape");
    }
    return operator[](i);
  }

  /// Returns whether the tape has no records.
  ///
  /// \returns `true` if the tape is empty.
  [[nodiscard]] bool empty() const noexcept { return size() == 0; }
  /// Returns the number of records in the tape.
  ///
  /// \returns The number of records.
  [[nodiscard]] size_type size() const noexcept { return markers_.size() - 1; }
  /// Returns the total number of scalar elements across all records.
  ///
  /// \returns The total number of scalar elements.
  [[nodiscard]] size_type size_flat() const noexcept { return data_.size(); }

  /// Returns the first record.
  ///
  /// \returns A reference to the first record.
  [[nodiscard]] const_reference front() const noexcept { return operator[](0); }
  /// Returns the last record.
  ///
  /// \returns A reference to the last record.
  [[nodiscard]] const_reference back() const noexcept {
    return operator[](size() - 1);
  }

  // iterators ----

  /// Returns an iterator to the first record.
  ///
  /// \returns An iterator to the first record.
  [[nodiscard]] const_iterator begin() const noexcept { return {*this, 0}; }
  /// Returns a const iterator to the first record.
  ///
  /// \returns A const iterator to the first record.
  [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }

  /// Returns an iterator past the last record.
  ///
  /// \returns An iterator past the last record.
  [[nodiscard]] const_iterator end() const noexcept { return {*this, size()}; }
  /// Returns a const iterator past the last record.
  ///
  /// \returns A const iterator past the last record.
  [[nodiscard]] const_iterator cend() const noexcept { return end(); }

  /// Returns a reverse iterator to the last record.
  ///
  /// \returns A reverse iterator to the last record.
  [[nodiscard]] auto rbegin() const noexcept {
    return const_reverse_iterator{end()};
  }
  /// Returns a const reverse iterator to the last record.
  ///
  /// \returns A const reverse iterator to the last record.
  [[nodiscard]] auto crbegin() const noexcept { return rbegin(); }

  /// Returns a reverse iterator before the first record.
  ///
  /// \returns A reverse iterator before the first record.
  [[nodiscard]] auto rend() const noexcept {
    return const_reverse_iterator{begin()};
  }
  /// Returns a const reverse iterator before the first record.
  ///
  /// \returns A const reverse iterator before the first record.
  [[nodiscard]] auto crend() const noexcept { return rend(); }

  // push / emplace_back --------

  /// Appends a record from the range `[f, l)` to the tape.
  ///
  /// \param f Iterator to the first scalar of the new record.
  /// \param l Sentinel past the last scalar of the new record.
  template <typename I, std::sentinel_for<I> S>
  void push_back(I f, S l)
    requires iterator_of_scalars<I>
  {
    data_.insert(data_.end(), f, l);
    markers_.push_back(static_cast<difference_type>(data_.size()));
  }

  /// Appends a record built from the range `r` to the tape.
  ///
  /// \param r The range of scalars forming the new record.
  template <typename R>
  void push_back(R&& r)
    requires(
        range_of_scalars<R> &&
        !std::is_convertible_v<R, const_reference>) // handle \0 separately
  {
    push_back(std::begin(r), std::end(r));
  }

  /// Appends a copy of record `r` to the tape.
  ///
  /// \param r The record to append.
  void push_back(const_reference r) { push_back(r.begin(), r.end()); }

  /// Appends a record built from an initializer list to the tape.
  ///
  /// \param r The scalars forming the new record.
  void push_back(std::initializer_list<scalar_value_type> r) {
    push_back(r.begin(), r.end());
  }

  /// Appends an empty record to the tape.
  void emplace_back() { push_back({}); }

  /// Appends a record built from the given arguments to the tape.
  ///
  /// \param args Arguments forming the new record.
  template <typename... Args>
  void emplace_back(Args&&... args) {
    push_back(std::forward<Args>(args)...);
  }

  // push_back_unsafe --------

  /// Appends a record from the range `[f, l)` without checking capacity.
  ///
  /// Like push_back but requires you to have enough capacity for the added
  /// range. Happened to give a 2x performance improvement on certain
  /// benchmarks. Requires you to have enough capacity.
  ///
  /// \param f Iterator to the first scalar of the new record.
  /// \param l Sentinel past the last scalar of the new record.
  template <typename I, std::sentinel_for<I> S>
  void push_back_unsafe(I f, S l)
    requires iterator_of_scalars<I>
  {
    // basic exception guarantee is preserved here.
    detail::append_range_unsafe(data_, f, l);
    markers_.push_back(static_cast<difference_type>(data_.size()));
  }

  /// Appends a record from the range `r` without checking capacity.
  ///
  /// \param r The range of scalars forming the new record.
  template <typename R>
  void push_back_unsafe(R&& r)
    requires(
        range_of_scalars<R> &&
        !std::is_convertible_v<R, const_reference>) // handle \0 separately
  {
    push_back_unsafe(std::begin(r), std::end(r));
  }

  /// Appends a copy of record `r` without checking capacity.
  ///
  /// \param r The record to append.
  void push_back_unsafe(const_reference r) {
    push_back_unsafe(r.begin(), r.end());
  }

  // record builder (constructing last record) -------

  class record_builder;

  // get a record builder.

  /// Starts a builder for a new record.
  ///
  /// \returns A record builder for a new record.
  [[nodiscard]] record_builder new_record_builder();
  /// Allows you to append/mutate the last record.
  ///
  /// \returns A record builder for the last record.
  [[nodiscard]] record_builder last_record_builder();

  // insert one record ----------

  /// Inserts a record from the range `[f, l)` before `pos`.
  ///
  /// \param pos Iterator to the position to insert before.
  /// \param f Iterator to the first scalar of the new record.
  /// \param l Sentinel past the last scalar of the new record.
  /// \returns An iterator to the inserted record.
  template <typename I, std::sentinel_for<I> S>
  iterator insert(const_iterator pos, I f, S l)
    requires iterator_of_scalars<I>;

  /// Inserts a record built from the range `r` before `pos`.
  ///
  /// \param pos Iterator to the position to insert before.
  /// \param r The range of scalars forming the new record.
  /// \returns An iterator to the inserted record.
  template <typename R>
  iterator insert(const_iterator pos, R&& r)
    requires(range_of_scalars<R> && !std::is_convertible_v<R, const_reference>)
  {
    return insert(pos, std::begin(r), std::end(r));
  }

  /// Inserts a record built from an initializer list before `pos`.
  ///
  /// \param pos Iterator to the position to insert before.
  /// \param r The scalars forming the new record.
  /// \returns An iterator to the inserted record.
  iterator insert(
      const_iterator pos, std::initializer_list<scalar_value_type> r) {
    return insert(pos, r.begin(), r.end());
  }

  /// Inserts a copy of record `r` before `pos`.
  ///
  /// \param pos Iterator to the position to insert before.
  /// \param r The record to insert.
  /// \returns An iterator to the inserted record.
  iterator insert(const_iterator pos, const_reference r) {
    return insert(pos, r.begin(), r.end());
  }

  // capacity ------
  /// Reserves capacity for the given number of records and elements.
  ///
  /// \param records The number of records to reserve capacity for.
  /// \param elements The number of scalar elements to reserve capacity for.
  void reserve(size_type records, size_type elements) {
    markers_.reserve(records + 1);
    data_.reserve(elements);
  }

  /// Reserves capacity for the given number of records.
  ///
  /// Assumes that 1 element per record. This is likely to help a bit.
  ///
  /// \param records The number of records to reserve capacity for.
  void reserve(size_type records) {
    markers_.reserve(records + 1);
    data_.reserve(records);
  }

  /// Reduces memory usage by freeing unused capacity.
  void shrink_to_fit() {
    markers_.shrink_to_fit();
    data_.shrink_to_fit();
  }

  // resize/clear -------

  /// Resizes the tape to contain `new_size` records.
  ///
  /// Same args as for push_back/emplace back are accepted.
  ///
  /// \param new_size The desired number of records.
  /// \param args Arguments used to build any new records.
  template <typename... Args>
  void resize(size_type new_size, const Args&... args);

  /// Removes all records from the tape.
  void clear() noexcept {
    markers_.resize(1);
    data_.clear();
  }

  // erase -------

  /// Removes the last record from the tape.
  void pop_back() noexcept {
    assert(!empty());
    data_.resize(data_.size() - back().size());
    markers_.pop_back();
  }

  /// Erases the record at `pos`.
  ///
  /// Same behaviour as for std::vector, erasing end() is UB.
  ///
  /// \param pos Iterator to the record to erase.
  /// \returns An iterator to the record following the erased one.
  iterator erase(const_iterator pos) {
    assert(pos != end());
    return erase(pos, pos + 1);
  }

  /// Erases the records in the range `[f, l)`.
  ///
  /// \param f Iterator to the first record to erase.
  /// \param l Iterator past the last record to erase.
  /// \returns An iterator to the record following the erased range.
  iterator erase(const_iterator f, const_iterator l);

  // ordering --------

  /// Compares two tapes for equality.
  ///
  /// \param x The left-hand tape.
  /// \param y The right-hand tape.
  /// \returns `true` if the tapes are equal.
  friend bool operator==(const tape& x, const tape& y) {
    return x.markers_ == y.markers_ && x.data_ == y.data_;
  }

  /// Compares two tapes for inequality.
  ///
  /// \param x The left-hand tape.
  /// \param y The right-hand tape.
  /// \returns `true` if the tapes are not equal.
  friend bool operator!=(const tape& x, const tape& y) { return !(x == y); }

  /// Compares two tapes lexicographically.
  ///
  /// \param x The left-hand tape.
  /// \param y The right-hand tape.
  /// \returns `true` if `x` is less than `y`.
  friend bool operator<(const tape& x, const tape& y) {
    return std::lexicographical_compare(x.begin(), x.end(), y.begin(), y.end());
  }

  /// Compares two tapes lexicographically.
  ///
  /// \param x The left-hand tape.
  /// \param y The right-hand tape.
  /// \returns `true` if `x` is greater than `y`.
  friend bool operator>(const tape& x, const tape& y) { return y < x; }
  /// Compares two tapes lexicographically.
  ///
  /// \param x The left-hand tape.
  /// \param y The right-hand tape.
  /// \returns `true` if `x` is less than or equal to `y`.
  friend bool operator<=(const tape& x, const tape& y) { return !(y < x); }
  /// Compares two tapes lexicographically.
  ///
  /// \param x The left-hand tape.
  /// \param y The right-hand tape.
  /// \returns `true` if `x` is greater than or equal to `y`.
  friend bool operator>=(const tape& x, const tape& y) { return !(x < y); }

  /// Returns the record separator positions.
  ///
  /// \returns A range over the marker offsets.
  folly::Range<const difference_type*> markers() const { return markers_; }
  /// Returns all scalar elements as a single range.
  ///
  /// \returns A reference spanning all stored scalar elements.
  reference scalars() const {
    return ref_traits::make(data_.begin(), data_.end());
  }

 private:
  template <typename I, typename S>
  void range_constructor(I f, S l);

  // NOTE: using container difference_type might be too much here but,
  // on the other hand, there should be reasonably few items on the tape and
  // this makes interface simpler.
  std::vector<difference_type> markers_ = {0};
  container_type data_;
};

/// Provides a way to construct a last record similar
/// to how you would `std::vector`.
///
/// Typical workflow is you `push_back` a bunch of individual elements and then
/// `commit()`.
template <std::ranges::random_access_range Container>
class tape<Container>::record_builder {
 public:
  /// Deleted copy constructor.
  ///
  /// \param other The record builder to copy from.
  record_builder(const record_builder& other) = delete;
  /// Deleted move constructor.
  ///
  /// \param other The record builder to move from.
  record_builder(record_builder&& other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param other The record builder to copy from.
  /// \returns A reference to this record builder.
  record_builder& operator=(const record_builder& other) = delete;
  /// Deleted move assignment operator.
  ///
  /// \param other The record builder to move from.
  /// \returns A reference to this record builder.
  record_builder& operator=(record_builder&& other) = delete;

  /// The iterator type over the record's elements.
  using iterator = typename container_type::iterator;
  /// The const iterator type over the record's elements.
  using const_iterator = typename container_type::const_iterator;
  /// A reference to an element of the record.
  using reference = typename std::iterator_traits<iterator>::reference;
  /// A const reference to an element of the record.
  using const_reference =
      typename std::iterator_traits<const_iterator>::reference;
  /// An unsigned integer type for sizes.
  using size_type = typename container_type::size_type;
  /// A signed integer type for differences.
  using difference_type =
      typename std::iterator_traits<iterator>::difference_type;

  // mutators ---

  /// Appends an element to the record being built.
  ///
  /// \param x The element to append.
  void push_back(scalar_value_type x) { self_->data_.push_back(std::move(x)); }

  /// Constructs an element in place at the end of the record being built.
  ///
  /// \param args Arguments forwarded to the element's constructor.
  /// \returns A reference to the newly constructed element.
  template <typename... Args>
  reference emplace_back(Args&&... args) {
    self_->data_.emplace_back(std::forward<Args>(args)...);
    // cannot rely on the container doing the right thing here.
    return self_->data_.back();
  }

  /// Constructed record is added to the tape.
  void commit() { self_->markers_.push_back(self_->data_.size()); }

  /// Discards elements of the constructed record. (automatic on destruction)
  void abort() { self_->data_.resize(self_->markers_.back()); }

  // iterators -----

  /// Returns an iterator to the first element of the record being built.
  ///
  /// \returns An iterator to the first element.
  [[nodiscard]] iterator begin() noexcept {
    return self_->data_.begin() + self_->markers_.back();
  }
  /// Returns a const iterator to the first element of the record being built.
  ///
  /// \returns A const iterator to the first element.
  [[nodiscard]] const_iterator begin() const noexcept {
    return self_->data_.cbegin() + self_->markers_.back();
  }
  /// Returns a const iterator to the first element of the record being built.
  ///
  /// \returns A const iterator to the first element.
  [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }

  /// Returns an iterator past the last element of the record being built.
  ///
  /// \returns An iterator past the last element.
  [[nodiscard]] iterator end() noexcept { return self_->data_.end(); }
  /// Returns a const iterator past the last element of the record being built.
  ///
  /// \returns A const iterator past the last element.
  [[nodiscard]] const_iterator end() const noexcept {
    return self_->data_.cend();
  }
  /// Returns a const iterator past the last element of the record being built.
  ///
  /// \returns A const iterator past the last element.
  [[nodiscard]] const_iterator cend() const noexcept { return end(); }

  /// Returns a back inserter for appending elements to the record.
  ///
  /// Sometimes functions (like fmt) optimize for a vector back inserter, so
  /// better expose that.
  ///
  /// \returns A back inserter into the underlying container.
  [[nodiscard]] auto back_inserter() noexcept {
    return std::back_inserter(self_->data_);
  }

  // access ---

  /// Returns whether the record being built has no elements.
  ///
  /// \returns `true` if the record is empty.
  [[nodiscard]] bool empty() const noexcept { return begin() == end(); }

  /// Returns the number of elements in the record being built.
  ///
  /// \returns The number of elements.
  [[nodiscard]] size_type size() const noexcept {
    return static_cast<size_type>(end() - begin());
  }

  /// Returns a reference to the element at index `i`.
  ///
  /// \param i The index of the element to access.
  /// \returns A reference to the element at index `i`.
  [[nodiscard]] reference operator[](size_type i) noexcept {
    return begin()[static_cast<difference_type>(i)];
  }

  /// Returns a const reference to the element at index `i`.
  ///
  /// \param i The index of the element to access.
  /// \returns A const reference to the element at index `i`.
  [[nodiscard]] const_reference operator[](size_type i) const noexcept {
    return begin()[static_cast<difference_type>(i)];
  }

  /// Returns a reference to the element at index `i`, with bounds checking.
  ///
  /// \param i The index of the element to access.
  /// \returns A reference to the element at index `i`.
  [[nodiscard]] reference at(size_type i) {
    if (FOLLY_UNLIKELY(i >= size())) {
      // libc++ doesn't provide index. This helps optimizations.
      throw std::out_of_range("tape::scoped_record_builder");
    }
    return operator[](i);
  }

  /// Returns a const reference to the element at index `i`, with bounds
  /// checking.
  ///
  /// \param i The index of the element to access.
  /// \returns A const reference to the element at index `i`.
  [[nodiscard]] const_reference at(size_type i) const {
    if (FOLLY_UNLIKELY(i >= size())) {
      // libc++ doesn't provide index. This helps optimizations.
      throw std::out_of_range("tape::scoped_record_builder");
    }
    return operator[](i);
  }

  /// Returns a reference to the last element of the record being built.
  ///
  /// \returns A reference to the last element.
  [[nodiscard]] reference back() { return self_->data_.back(); }
  /// Returns a const reference to the last element of the record being built.
  ///
  /// \returns A const reference to the last element.
  [[nodiscard]] const_reference back() const { return self_->data_.back(); }

  /// Destroys the record builder, discarding any uncommitted elements.
  ~record_builder() noexcept { abort(); }

 private:
  friend class tape;

  explicit record_builder(tape& self) : self_(&self) {}

  tape* self_;
};

template <std::ranges::random_access_range Container>
auto tape<Container>::new_record_builder() -> record_builder {
  return record_builder{*this};
}

template <std::ranges::random_access_range Container>
auto tape<Container>::last_record_builder() -> record_builder {
  assert(!empty());
  markers_.pop_back();
  return new_record_builder();
}

// tape methods -----

template <std::ranges::random_access_range Container>
tape<Container>::tape(tape&& x) noexcept
    : markers_(std::move(x.markers_)), data_(std::move(x.data_)) {
  // we assume that allocations never fail
  x.markers_ = {0};
  x.data_.clear();
}

template <std::ranges::random_access_range Container>
tape<Container>& tape<Container>::operator=(tape&& x) noexcept {
  if (this != &x) {
    markers_ = std::move(x.markers_);
    data_ = std::move(x.data_);
  }
  // we assume that allocations never fail
  x.markers_ = {0};
  x.data_.clear();
  return *this;
}

template <std::ranges::random_access_range Container>
template <typename I, typename S>
void tape<Container>::range_constructor(I f, S l) {
  if constexpr (auto maybe = detail::compute_total_tape_len_if_possible(f, l);
                std::is_same_v<decltype(maybe), detail::fake_type>) {
    while (f != l) {
      push_back(*f);
      ++f;
    }
  } else {
    auto [nrecords, total_len] = maybe;
    reserve(nrecords, total_len);

    while (f != l) {
      push_back_unsafe(*f);
      ++f;
    }
  }
}

template <std::ranges::random_access_range Container>
template <typename... Args>
void tape<Container>::resize(size_type new_size, const Args&... args) {
  if (new_size >= size()) {
    new_size -= size();
    while (new_size--) {
      emplace_back(args...);
    }
    return;
  }

  data_.resize(markers_[new_size]);
  markers_.resize(new_size + 1);
}

template <std::ranges::random_access_range Container>
template <typename I, std::sentinel_for<I> S>
auto tape<Container>::insert(const_iterator pos, I f, S l) -> iterator
  requires iterator_of_scalars<I>
{
  auto data_pos = data_.begin() + markers_[pos.get_index()];
  size_type old_size = data_.size();
  data_.insert(data_pos, f, l);

  auto inserted_len = static_cast<difference_type>(data_.size() - old_size);

  difference_type start = markers_[pos.get_index()];

  auto markers_tail =
      markers_.insert(markers_.begin() + pos.get_index(), start);
  ++markers_tail;

  std::transform(
      markers_tail, markers_.end(), markers_tail, [&](difference_type m) {
        return m + inserted_len;
      });

  // both tape* and index stayed the same
  return pos;
}

template <std::ranges::random_access_range Container>
auto tape<Container>::erase(const_iterator f, const_iterator l) -> iterator {
  difference_type from = f.get_index();
  difference_type to = l.get_index();

  auto markers_f = markers_.begin() + from;
  auto markers_l = markers_.begin() + to;
  auto data_f = data_.begin() + *markers_f;
  auto data_l = data_.begin() + *markers_l;

  std::ptrdiff_t removed_length = data_l - data_f;
  std::transform(markers_l, markers_.end(), markers_l, [&](difference_type m) {
    return m - removed_length;
  });

  markers_.erase(markers_f, markers_l);
  data_.erase(data_f, data_l);

  // both tape* and index stayed the same
  return f;
}

} // namespace folly
