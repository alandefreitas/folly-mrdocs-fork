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

#include <array>
#include <cassert>
#include <chrono>
#include <iosfwd>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <folly/Chrono.h>
#include <folly/Function.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/container/Reserve.h>
#include <folly/hash/UniqueHashKey.h>
#include <folly/lang/Bits.h>

namespace folly {

/// RegexMatchCacheDynamicBitset
///
/// A dynamic bitset for use within, and optimized for, RegexMatchCache.
/// * Small, having the same size and alignment as a pointer.
/// * Optimistically non-allocating, using in-situ storage for small bitsets.
///
/// Intended for use only within RegexMatchCache.
///
/// Incomplete as a generic container.
class RegexMatchCacheDynamicBitset {
 private:
  template <typename Word>
  struct bit_span {
    Word* data;
    size_t size;

    bit_span(Word* const data_, size_t const size_) noexcept
        : data{data_}, size{size_} {}
    bit_span(bit_span const&) = default;
    bit_span& operator=(bit_span const&) = default;

    auto as_tuple() const noexcept { return std::tuple{data, size}; }

    /// Returns whether two spans refer to the same data and size.
    ///
    /// \param a The first span.
    /// \param b The second span.
    /// \returns True if `a` and `b` are equal.
    friend bool operator==(bit_span const& a, bit_span const& b) noexcept {
      return a.as_tuple() == b.as_tuple();
    }
    /// Returns whether two spans differ in data or size.
    ///
    /// \param a The first span.
    /// \param b The second span.
    /// \returns True if `a` and `b` are not equal.
    friend bool operator!=(bit_span const& a, bit_span const& b) noexcept {
      return a.as_tuple() != b.as_tuple();
    }
  };

 public:
  /// Constructs an empty bitset using in-situ storage.
  RegexMatchCacheDynamicBitset() = default;

  /// Deleted copy constructor; the bitset is move-only.
  ///
  /// \param that The bitset that would be copied.
  RegexMatchCacheDynamicBitset(RegexMatchCacheDynamicBitset const& that) = delete;
  /// Move-constructs, taking ownership of another bitset's storage.
  ///
  /// \param that The bitset to move from.
  RegexMatchCacheDynamicBitset(RegexMatchCacheDynamicBitset&& that) noexcept
      : data_{std::exchange(that.data_, {})} {}

  /// Destroys the bitset, releasing any heap storage.
  ~RegexMatchCacheDynamicBitset() { reset_(); }

  /// Deleted copy assignment; the bitset is move-only.
  ///
  /// \param that The bitset that would be copied.
  void operator=(RegexMatchCacheDynamicBitset const& that) = delete;
  /// Move-assigns, taking ownership of another bitset's storage.
  ///
  /// \param that The bitset to move from.
  /// \returns A reference to this bitset.
  RegexMatchCacheDynamicBitset& operator=(
      RegexMatchCacheDynamicBitset&& that) noexcept {
    reset_();
    data_ = std::exchange(that.data_, {});
    return *this;
  }

  /// Returns the bit at an index, or false when out of range.
  ///
  /// \param index The bit position to read.
  /// \returns The value of the bit at `index`.
  bool get_value(size_t const index) const noexcept {
    auto data = get_bit_span_();
    if (!(index < data.size)) {
      return false;
    }
    return get_value_(data, index);
  }

  /// Sets the bit at an index, growing storage if needed.
  ///
  /// \param index The bit position to write.
  /// \param value The value to store at `index`.
  void set_value(size_t const index, bool const value) {
    constexpr auto wordbits = sizeof(uintptr_t) * 8;
    auto data = get_bit_span_();

    if (!(index < data.size) ||
        (data.size == wordbits && index == wordbits - 1)) {
      if (!value) {
        return;
      }
      data = reserve_(index);
    }
    assert(index < data.size);
    set_value_(data, index, value);
  }

  /// Clears all bits and releases any heap storage.
  void reset() noexcept { reset_(); }

  /// A view iterating over the indices of the set bits.
  class index_set_view {
   private:
    friend RegexMatchCacheDynamicBitset;
    bit_span<uintptr_t const> bitset_;

    explicit index_set_view(RegexMatchCacheDynamicBitset const& bitset) noexcept
        : bitset_{bitset.get_bit_span_()} {}

   public:
    /// The type of each yielded set-bit index.
    using value_type = size_t;

    /// Forward iterator over the indices of the set bits.
    class const_iterator {
     public:
      /// The type of the yielded index value.
      using value_type = size_t;
      /// The iterator difference type.
      using difference_type = ptrdiff_t;
      /// The iterator pointer type (unused).
      using pointer = void;
      /// The iterator category tag.
      using iterator_category = std::forward_iterator_tag;

      /// Proxy reference yielding a set-bit index.
      struct reference {
       private:
        friend class const_iterator;

        size_t const index_;

        explicit reference(size_t const index) noexcept : index_{index} {}

       public:
        /// Converts the proxy to the underlying index value.
        ///
        /// \returns The set-bit index.
        operator size_t() const noexcept { return index_; }
      };

     private:
      using self = const_iterator;

      bit_span<uintptr_t const> const data_;
      size_t index_;

      size_t ceil_valid_index(size_t index) const noexcept {
        constexpr auto wordbits = sizeof(uintptr_t) * 8;
        while (index < data_.size) {
          auto const wordidx = index / wordbits;
          auto const wordoff = index % wordbits;
          if (auto const word = data_.data[wordidx] >> wordoff) {
            return index + findFirstSet(word) - 1;
          }
          index = (wordidx + 1) * wordbits;
        }
        return index;
      }

     public:
      /// Constructs an iterator advanced to the first set bit at or after index.
      ///
      /// \param data The span of words to iterate over.
      /// \param index The starting bit position.
      const_iterator(
          bit_span<uintptr_t const> const data, size_t const index) noexcept
          : data_{data}, index_{ceil_valid_index(index)} {}

      /// Returns a proxy for the current set-bit index.
      ///
      /// \returns A reference proxy holding the current index.
      reference operator*() const noexcept { return reference{index_}; }
      /// Advances to the next set bit.
      ///
      /// \returns A reference to this iterator.
      const_iterator& operator++() noexcept {
        index_ = ceil_valid_index(index_ + 1);
        return *this;
      }

      /// Returns whether two iterators refer to the same bit position.
      ///
      /// \param a The first iterator.
      /// \param b The second iterator.
      /// \returns True if the iterators are equal.
      friend bool operator==(self const& a, self const& b) noexcept {
        return a.index_ == b.index_;
      }
      /// Returns whether two iterators refer to different bit positions.
      ///
      /// \param a The first iterator.
      /// \param b The second iterator.
      /// \returns True if the iterators are not equal.
      friend bool operator!=(self const& a, self const& b) noexcept {
        return a.index_ != b.index_;
      }
    };

    /// Returns an iterator to the first set-bit index.
    ///
    /// \returns An iterator to the first set bit.
    const_iterator begin() const noexcept { return const_iterator{bitset_, 0}; }
    /// Returns an iterator past the last set-bit index.
    ///
    /// \returns An iterator past the last set bit.
    const_iterator end() const noexcept {
      return const_iterator{bitset_, bitset_.size};
    }

    /// Returns whether the bitset has no set bits.
    ///
    /// \returns True if no bits are set.
    bool empty() const noexcept { return begin() == end(); }
  };

  /// Returns a view over the indices of the set bits.
  ///
  /// \returns An index_set_view bound to this bitset.
  index_set_view as_index_set_view() const noexcept {
    return index_set_view{*this};
  }

 private:
  bool has_capacity_(size_t const index) const noexcept {
    constexpr auto wordbits = sizeof(uintptr_t) * 8;
    auto const buf = get_bit_span_();
    return index < buf.size && !(buf.size == wordbits && index == wordbits - 1);
  }

  bit_span<uintptr_t> reserve_(size_t const index) {
    assert(!has_capacity_(index));
    constexpr auto wordbits = sizeof(uintptr_t) * 8;
    constexpr auto minsize = wordbits * 2; // min growth from in-situ to on-heap
    auto const newsize = std::max(strictNextPowTwo(index), minsize);
    assert(newsize >= minsize);
    assert(newsize % wordbits == 0);
    auto const newdata = new uintptr_t[newsize / wordbits];
    auto const buf = get_bit_span_();
    auto const buf2size = nextPowTwo(buf.size);
    std::memcpy(newdata, buf.data, buf2size / 8);
    std::memset(newdata + buf2size / wordbits, 0, (newsize - buf2size) / 8);
    if (!(to_signed(data_) < 0)) {
      auto const data = new bit_span<uintptr_t>{newdata, newsize};
      assert(!(reinterpret_cast<uintptr_t>(data) & 1));
      data_ = (reinterpret_cast<uintptr_t>(data) >> 1) | ~(~uintptr_t(0) >> 1);
      return *data;
    } else {
      auto const data = reinterpret_cast<bit_span<uintptr_t>*>(data_ << 1);
      delete[] data->data;
      *data = {newdata, newsize};
      return *data;
    }
  }

  void reset_() {
    if (!(to_signed(data_) < 0)) {
      data_ = 0;
    } else {
      auto const data = reinterpret_cast<bit_span<uintptr_t>*>(data_ << 1);
      delete[] data->data;
      delete data;
      data_ = 0;
    }
  }

  template <typename Word>
  static bool get_value_(
      bit_span<Word> const buf, size_t const index) noexcept {
    assert(index < buf.size);
    constexpr auto wordbits = sizeof(Word) * 8;
    auto const wordidx = index / wordbits;
    auto const wordoff = index % wordbits;
    auto const mask = Word(1) << wordoff;
    auto& word = buf.data[wordidx];
    return word & mask;
  }

  template <typename Word>
  static void set_value_(
      bit_span<Word> const buf, size_t const index, bool const value) noexcept {
    assert(index < buf.size);
    constexpr auto wordbits = sizeof(Word) * 8;
    assert(buf.size != wordbits || index != wordbits - 1);
    auto const wordidx = index / wordbits;
    auto const wordoff = index % wordbits;
    auto const mask = Word(1) << wordoff;
    auto& word = buf.data[wordidx];
    word = value ? word | mask : word & ~mask;
  }

  bit_span<uintptr_t const> get_bit_span_() const noexcept {
    if (!(to_signed(data_) < 0)) {
      return {&data_, sizeof(data_) * 8};
    } else {
      return *reinterpret_cast<bit_span<uintptr_t const> const*>(data_ << 1);
    }
  }

  bit_span<uintptr_t> get_bit_span_() noexcept {
    if (!(to_signed(data_) < 0)) {
      return {&data_, sizeof(data_) * 8};
    } else {
      return *reinterpret_cast<bit_span<uintptr_t> const*>(data_ << 1);
    }
  }

  uintptr_t data_{};
};

/// RegexMatchCacheIndexedVector
///
/// An indexed vector, which is a vector for which the index of any element can
/// be found efficiently.
///
/// Intended for use only within RegexMatchCache.
///
/// Incomplete as a generic container.
template <typename Value>
class RegexMatchCacheIndexedVector {
 public:
  /// Returns the number of elements in the vector.
  ///
  /// \returns The element count.
  size_t size() const noexcept { return forward_.size(); }

  /// Returns whether an index is currently assigned to a value.
  ///
  /// \param index The index to test.
  /// \returns True if `index` maps to a value.
  bool contains_index(size_t index) const noexcept {
    return reverse_.contains(index);
  }

  /// Returns whether a value is present in the vector.
  ///
  /// \param value The value to test.
  /// \returns True if `value` is present.
  bool contains_value(Value const& value) const noexcept {
    return forward_.contains(value);
  }

  /// Inserts a value, assigning it a stable index if it is new.
  ///
  /// \param value The value to insert.
  /// \returns The value's index and whether it was newly inserted.
  std::pair<size_t, bool> insert_value(Value const& value) {
    auto [iter, inserted] = forward_.try_emplace(value);
    if (inserted) {
      auto rollback_forward = makeGuard([&, iter_ = iter] {
        forward_.erase(iter_);
      });
      if (free_.capacity() < forward_.size()) {
        grow_capacity_by(free_, forward_.size() - free_.size());
      }
      assert(!(free_.capacity() < forward_.size()));
      auto const from_free = !free_.empty();
      auto const index = from_free ? free_.back() : forward_.size() - 1;
      from_free ? free_.pop_back() : void();
      iter->second = index;
      auto rollback_free = makeGuard([&] {
        from_free ? free_.push_back(index) : void();
      });
      assert(!reverse_.contains(index));
      reverse_[index] = value;
      rollback_free.dismiss();
      rollback_forward.dismiss();
    }
    return {iter->second, inserted};
  }

  /// Removes a value, freeing its index for reuse.
  ///
  /// \param value The value to remove.
  /// \returns True if `value` was present and removed.
  bool erase_value(Value const& value) noexcept {
    auto iter = forward_.find(value);
    if (iter == forward_.end()) {
      return false;
    }
    assert(free_.size() < free_.capacity());
    auto index = iter->second;
    free_.push_back(index);
    forward_.erase(iter);
    reverse_.erase(index);
    return true;
  }

  /// Removes all values and indices.
  void clear() noexcept {
    reverse_.clear();
    forward_.clear();
    free_.clear();
  }

  /// Returns the value assigned to an index.
  ///
  /// \param index The index to look up.
  /// \returns A reference to the value at `index`.
  Value const& value_at_index(size_t index) const { return reverse_.at(index); }

  /// Returns the index assigned to a value.
  ///
  /// \param value The value to look up.
  /// \returns The index of `value`.
  size_t index_of_value(Value const& value) const { return forward_.at(value); }

  /// A read-only view over the value-to-index mapping.
  class forward_view {
   private:
    friend RegexMatchCacheIndexedVector;
    using map_t = folly::F14FastMap<Value, size_t>;
    map_t const& map;
    explicit forward_view(map_t const& map_) noexcept : map{map_} {}

   public:
    /// The key/index pair type exposed by the view.
    using value_type = typename map_t::value_type;
    /// The size type exposed by the view.
    using size_type = typename map_t::size_type;
    /// The const iterator type exposed by the view.
    using iterator = typename map_t::const_iterator;

    /// Returns the number of mapped values.
    ///
    /// \returns The element count.
    size_t size() const noexcept { return map.size(); }
    /// Returns an iterator to the first mapping.
    ///
    /// \returns An iterator to the first element.
    iterator begin() const noexcept { return map.begin(); }
    /// Returns an iterator past the last mapping.
    ///
    /// \returns An iterator past the last element.
    iterator end() const noexcept { return map.end(); }
  };

  /// Returns a read-only view over the value-to-index mapping.
  ///
  /// \returns A forward_view bound to this vector.
  forward_view as_forward_view() const noexcept {
    return forward_view{forward_};
  }

 private:
  std::vector<size_t> free_;
  folly::F14FastMap<Value, size_t> forward_;
  folly::F14FastMap<size_t, Value> reverse_;
};

/// The base hash-key type underlying RegexMatchCacheKey.
using RegexMatchCacheKeyBase = unique_hash_key_strong_sha256<32>;

/// RegexMatchCacheKey
///
/// A key derived from a string. Used with RegexMatchCache.
///
/// Intended for use only with RegexMatchCache.
///
/// Incomplete as a generic facility.
class RegexMatchCacheKey : public RegexMatchCacheKeyBase {
 public:
  /// Derives the key from a regex string.
  ///
  /// \param regex The regex string to hash into the key.
  explicit RegexMatchCacheKey(std::string_view regex) noexcept
      : RegexMatchCacheKeyBase{std::tuple(regex)} {}
};

} // namespace folly

/// Standard library customization points for folly::RegexMatchCacheKey.
namespace std {

/// std::hash specialization for folly::RegexMatchCacheKey.
template <>
struct hash<::folly::RegexMatchCacheKey>
    : hash<::folly::RegexMatchCacheKeyBase> {};

} // namespace std

namespace folly {

/// RegexMatchCacheKeyAndView
///
/// A composite key and view derived from a string. Used with RegexMatchCache.
///
/// Intended for use only with RegexMatchCache.
///
/// Incomplete as a generic facility.
class RegexMatchCacheKeyAndView {
 public:
  /// The regex key type stored in the composite.
  using regex_key = RegexMatchCacheKey;

  /// The key derived from the regex string.
  regex_key const key;
  /// The view of the regex string.
  std::string_view const view;

  /// Builds the composite from a regex string.
  ///
  /// \param regex The regex string to derive the key and view from.
  explicit RegexMatchCacheKeyAndView(std::string_view regex) noexcept
      : key{regex}, view{regex} {}

  /// Converts to the underlying regex key.
  ///
  /// \returns A reference to the stored key.
  /* implicit */ operator RegexMatchCacheKey const&() const noexcept {
    return key;
  }
  /// Converts to the underlying regex string view.
  ///
  /// \returns A reference to the stored view.
  /* implicit */ operator std::string_view const&() const noexcept {
    return view;
  }

 private:
  RegexMatchCacheKeyAndView(
      regex_key const& k, std::string_view const v) noexcept
      : key{k}, view{v} {}
};

/// RegexMatchCache
///
/// A cache around boost::regex_match(string, regex).
///
/// For efficiency, assumes several constraints and makes several guarantees.
///
/// The data structure owns regexes but does not own strings. The lifetimes of
/// all strings in the cache must surround their additions to the cache and
/// their subsequent removals from the cache or destruction of the cache.
///
/// The data structure is in two parts:
/// * A bidirectional match-cache contains all known matches.
/// * a bidirectional string-queue contains unknown, hypothetical matches.
///
/// Cached lookup operates only over the match-cache. When the string-queue for
/// a given regex is not empty, that regex is said to be uncoalesced. Cached
/// lookups are not permitted for an uncoalesced regex; that regex must first be
/// coalesced.
///
/// Addition of a string adds the string to the string-queue corresponding to
/// all known regexes. It does not perform any regex-match operations.
///
/// Addition and coalesce of a regex performs regex-matches for that regex only.
/// The string-queue for the given regex is removed and all elements matched
/// against the regex, and matching strings are added to the match-cache.
///
/// Lookup must follow a pattern like this:
///
///    if (!cache.isReadyToFindMatches(regex)) { // const
///      cache.prepareToFindMatches(regex); // non-const
///    }
///    auto matches = cache.findMatches(regex); // const
///
/// This is to support concurrent lookups, where the cache is protected by a
/// shared mutex.
///
/// The data structure is exception-safe in a sense. If an exception is thrown
/// within any non-const member function and escapes, the data structure may
/// purge all cached regexes while leaving all strings. In most such member
/// functions, only a memory-allocation failure would cause an exception to be
/// thrown. But in prepareToFindMatches, the provided regex may be syntactically
/// invalid and parsing it may throw, or it may be pathological and evaluating
/// it over a string may throw. In any event, the resolution is to clear out all
/// added regexes and to leave only the added strings. The reason is that this
/// resolution is simple and likely to be correct, while any other mechanism
/// would be complex and would be likely to have bugs.
class RegexMatchCache {
 public:
  /// The clock used for regex last-access timestamps.
  using clock = folly::chrono::coarse_steady_clock;
  /// The time point type produced by the cache's clock.
  using time_point = clock::time_point;

  /// The key type identifying a regex.
  using regex_key = RegexMatchCacheKey;
  /// The composite key and view type identifying a regex.
  using regex_key_and_view = RegexMatchCacheKeyAndView;

 private:
  using regex_pointer = regex_key const*;
  using string_pointer = std::string const*;

  class RegexObject;

  struct RegexToMatchEntry : MoveOnly {
    mutable std::atomic<time_point> accessed_at{};

    folly::F14VectorSet<string_pointer> matches;
  };

  struct MatchToRegexEntry : MoveOnly {
    RegexMatchCacheDynamicBitset regexes;
  };

  struct StringQueueForwardEntry : MoveOnly {
    RegexMatchCacheDynamicBitset regexes;
  };

  struct StringQueueReverseEntry : MoveOnly {
    folly::F14VectorSet<string_pointer> strings;
  };

  RegexMatchCacheIndexedVector<regex_pointer> regexVector_;

  /// cacheRegexToMatch_
  ///
  /// A match-cache map from regexes to the sets of matching strings.
  ///
  /// The set of matching strings for a given regex may be incomplete. This
  /// happens when strings are added to the universe but have not yet been
  /// coalesced for the given regex. The set of uncoalesced strings for a
  /// given regex is in stringQueueReverse_.
  ///
  /// For each regex, includes a last-accessed-at timestamp. This timestamp
  /// is used when purging old regexes from the cache, for the caller's own
  /// definition of old.
  folly::F14NodeMap<regex_key, RegexToMatchEntry> cacheRegexToMatch_;

  /// cacheMatchToRegex_
  ///
  /// A match-cache map from strings to the sets of matching regexes.
  ///
  /// The set of matching regexes for a given string may be incomplete. This
  /// happens when strings are added to the universe but have not yet been
  /// coalesced for all regexes in the universe. The set of regexes for which
  /// a given string has not yet been coalesced is in stringQueueForward_.
  folly::F14FastMap<string_pointer, MatchToRegexEntry> cacheMatchToRegex_;

  /// stringQueueForward_
  ///
  /// A pending-coalesce map from strings to regexes for which the strings have
  /// not yet been coalesced, that is, for which it is not yet known that the
  /// strings do or do not match the given regexes.
  ///
  /// In a steady-state when all strings have been coalesced for all regexes,
  /// this map would be empty.
  folly::F14FastMap<string_pointer, StringQueueForwardEntry>
      stringQueueForward_;

  /// stringQueueReverse_
  ///
  /// A pending-coalesce map from regexes to strings which have not yet been
  /// coalesced for the given regex, that is, for which it is not yet known that
  /// the strings do or do not match the given regexes.
  ///
  /// In a steady-state when all strings have been coalesced for all regexes,
  /// this map would be empty.
  folly::F14FastMap<regex_pointer, StringQueueReverseEntry> stringQueueReverse_;

  void repair() noexcept;

 public:
  /// Interface mapping regex keys back to their string views.
  class KeyMap {
   public:
    /// The regex key type resolved by this map.
    using regex_key = RegexMatchCacheKey;
    /// The composite regex key and view type.
    using regex_key_and_view = RegexMatchCacheKeyAndView;

    /// Destroys the key map.
    virtual ~KeyMap() = 0;

    /// Returns the string view for a regex key.
    ///
    /// \param regex The regex key to resolve.
    /// \returns The string view corresponding to `regex`.
    virtual std::string_view lookup(regex_key const& regex) const = 0;
  };

  /// A printable view of a cache's internal state.
  class InspectView {
    friend RegexMatchCache;

   private:
    RegexMatchCache const& ref_;
    KeyMap const& keys_;

    explicit InspectView(
        RegexMatchCache const& ref, KeyMap const& keys) noexcept
        : ref_{ref}, keys_{keys} {}

    void print(std::ostream& o) const;

   public:
    /// Writes the inspected cache state to an output stream.
    ///
    /// \param o The destination output stream.
    /// \param view The view to print.
    /// \returns The output stream `o`.
    friend std::ostream& operator<<(std::ostream& o, InspectView const view) {
      return (view.print(o), o);
    }
  };

  /// Recomputes regex/string matches for the consistency check.
  class ConsistencyReportMatcher {
   private:
    struct state;
    std::unique_ptr<state> state_;

   public:
    /// The regex key type used by the matcher.
    using regex_key = RegexMatchCache::regex_key;
    /// The composite regex key and view type used by the matcher.
    using regex_key_and_view = RegexMatchCache::regex_key_and_view;
    /// The string pointer type used by the matcher.
    using string_pointer = RegexMatchCache::string_pointer;

    /// Constructs a matcher with empty internal state.
    ConsistencyReportMatcher();
    /// Destroys the matcher and its internal state.
    virtual ~ConsistencyReportMatcher();

    /// Returns whether a string matches a regex.
    ///
    /// \param keys Maps regex keys back to their string views.
    /// \param regex The regex to evaluate.
    /// \param string Pointer to the string to test.
    /// \returns True if `string` matches `regex`.
    virtual bool match(
        KeyMap const& keys, regex_key regex, string_pointer string);
  };

  /// A non-owning view over a regex's set of matching strings.
  class FindMatchesUnsafeResult {
   private:
    friend class RegexMatchCache;

    using map_t = folly::F14VectorSet<string_pointer>;

    map_t const& matches_;

    /* implicit */ FindMatchesUnsafeResult(map_t const& matches) noexcept
        : matches_{matches} {}

   public:
    /// The element type of the underlying match set.
    using value_type = map_t::value_type;

    /// Returns the number of matching strings.
    ///
    /// \returns The match count.
    auto size() const noexcept { return matches_.size(); }
    /// Returns an iterator to the first matching string.
    ///
    /// \returns An iterator to the first match.
    auto begin() const noexcept { return matches_.begin(); }
    /// Returns an iterator past the last matching string.
    ///
    /// \returns An iterator past the last match.
    auto end() const noexcept { return matches_.end(); }
  };

  /// Constructs an empty cache with no regexes or strings.
  RegexMatchCache() noexcept;
  /// Destroys the cache and its owned regexes.
  ~RegexMatchCache();

  /// Returns the views of all regexes currently in the cache.
  ///
  /// \param keys Maps regex keys back to their string views.
  /// \returns The list of regex views held by the cache.
  std::vector<std::string_view> getRegexList(KeyMap const& keys) const;
  /// Returns pointers to all strings currently in the cache.
  ///
  /// \returns The list of string pointers held by the cache.
  std::vector<string_pointer> getStringList() const;
  /// Returns a printable view of the cache's internal state.
  ///
  /// \param keys Maps regex keys back to their string views.
  /// \returns An InspectView bound to this cache.
  InspectView inspect(KeyMap const& keys) const noexcept {
    return InspectView{*this, keys};
  }
  /// Checks the cache for internal consistency, reporting any problems found.
  ///
  /// \param crcache Matcher used to recompute expected regex/string matches.
  /// \param keys Maps regex keys back to their string views.
  /// \param report Callback invoked with a message for each inconsistency.
  void consistency(
      ConsistencyReportMatcher& crcache,
      KeyMap const& keys,
      FunctionRef<void(std::string)> report) const;

  /// Returns whether the given regex is in the cache.
  ///
  /// \param regex The regex key to look up.
  /// \returns True if the regex is present, false otherwise.
  bool hasRegex(regex_key const& regex) const noexcept;
  /// Adds a regex to the cache.
  ///
  /// \param regex The regex key to add.
  void addRegex(regex_key const& regex);
  /// Removes a regex from the cache.
  ///
  /// \param regex The regex key to remove.
  void eraseRegex(regex_key const& regex);

  /// Returns whether the given string is in the cache.
  ///
  /// \param string Pointer to the string to look up.
  /// \returns True if the string is present, false otherwise.
  bool hasString(string_pointer string) const noexcept;
  /// Adds a string to the cache without performing any regex matches.
  ///
  /// \param string Pointer to the string to add.
  void addString(string_pointer string);
  /// Removes a string from the cache.
  ///
  /// \param string Pointer to the string to remove.
  void eraseString(string_pointer string);

  /// Computes the strings matching a regex without consulting the cache.
  ///
  /// \param regex The regex to evaluate.
  /// \returns Pointers to the strings that match `regex`.
  std::vector<string_pointer> findMatchesUncached(std::string_view regex) const;

  /// Returns whether cached matches for the regex are ready to query.
  ///
  /// \param regex The regex key to check.
  /// \returns True if the regex is coalesced and ready for cached lookup.
  bool isReadyToFindMatches(regex_key const& regex) const noexcept;
  /// Coalesces queued strings for a regex so its matches can be queried.
  ///
  /// \param regex The composite key and view of the regex to prepare.
  void prepareToFindMatches(regex_key_and_view const& regex);
  /// Returns the cached matches for a regex as a view over internal storage.
  ///
  /// \param regex The regex key to look up.
  /// \param now The current time, recorded as the regex's last access time.
  /// \returns A view over the set of matching strings.
  FindMatchesUnsafeResult findMatchesUnsafe(
      regex_key const& regex, time_point now) const;
  /// Returns a copy of the cached matches for a regex.
  ///
  /// \param regex The regex key to look up.
  /// \param now The current time, recorded as the regex's last access time.
  /// \returns Pointers to the strings matching `regex`.
  std::vector<string_pointer> findMatches(
      regex_key const& regex, time_point now) const;

  /// Returns whether any regex has not been accessed since the expiry time.
  ///
  /// \param expiry The cutoff time; regexes last accessed before it are old.
  /// \returns True if there is at least one purgeable regex.
  bool hasItemsToPurge(time_point expiry) const noexcept;

  /// Removes all regexes and strings from the cache.
  void clear();
  /// Removes regexes not accessed since the expiry time.
  ///
  /// \param expiry The cutoff time; regexes last accessed before it are purged.
  void purge(time_point expiry);
};

} // namespace folly
