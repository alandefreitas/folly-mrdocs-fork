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

#include <string>

#include <boost/regex/pending/unicode_iterator.hpp>

#include <folly/Range.h>

namespace folly {

/// A view over a UTF-8 string that iterates it as UTF-32 code points.
class UTF8StringPiece {
 public:
  /// Iterator that decodes the underlying UTF-8 bytes into UTF-32 code points.
  using iterator = boost::u8_to_u32_iterator<const char*>;
  /// Unsigned integer type used for sizes.
  using size_type = std::size_t;

  /// Constructs a view over the given `StringPiece`.
  ///
  /// @param piece The UTF-8 byte range to view.
  /* implicit */ UTF8StringPiece(const folly::StringPiece piece)
      : begin_{piece.begin(), piece.begin(), piece.end()},
        end_{piece.end(), piece.begin(), piece.end()} {}
  /// Constructs a view over anything convertible to a `StringPiece`.
  ///
  /// @param t The value to convert to a `StringPiece` and view.
  template <typename T>
    requires std::convertible_to<T, folly::StringPiece>
  /* implicit */ UTF8StringPiece(const T& t)
      : UTF8StringPiece(folly::StringPiece(t)) {}

  /// Returns an iterator to the first code point.
  ///
  /// @return An iterator to the beginning of the range.
  iterator begin() const noexcept { return begin_; }
  /// Returns a const iterator to the first code point.
  ///
  /// @return An iterator to the beginning of the range.
  iterator cbegin() const noexcept { return begin_; }
  /// Returns an iterator past the last code point.
  ///
  /// @return An iterator to the end of the range.
  iterator end() const noexcept { return end_; }
  /// Returns a const iterator past the last code point.
  ///
  /// @return An iterator to the end of the range.
  iterator cend() const noexcept { return end_; }

  /// Reports whether the view contains no code points.
  ///
  /// @return `true` if the range is empty, `false` otherwise.
  bool empty() const noexcept { return begin_ == end_; }
  /// Counts the number of code points by walking the range.
  ///
  /// @return The number of UTF-32 code points in the view.
  size_type walk_size() const {
    return static_cast<size_type>(std::distance(begin_, end_));
  }

 private:
  iterator begin_;
  iterator end_;
};

} // namespace folly
