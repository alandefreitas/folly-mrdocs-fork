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
#include <cstring>
#include <functional>
#include <iosfwd>
#include <string_view>

#include <fmt/format.h>

#include <folly/Portability.h>
#include <folly/Traits.h>
#include <folly/Utility.h>

static_assert(FOLLY_CPLUSPLUS >= 202002L, "__cplusplus >= 202002L");

namespace folly {

/// cstring_view
///
/// A string view type that privately inherits from std::string_view but
/// guarantees that the underlying buffer is null-terminated. This allows safe
/// use of .c_str() while maintaining all the benefits of string_view.
///
/// The type provides adjusted constructors and assignment operators to ensure
/// the null-termination guarantee is maintained.
///
/// mimic: std::cstring_view, p3655r3
///
/// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3655r3.html
template <typename Char, typename Traits = std::char_traits<Char>>
class basic_cstring_view {
  using self = basic_cstring_view;
  using view_type = std::basic_string_view<Char, Traits>;

  template <typename T>
  using detect_c_str = decltype(FOLLY_DECLVAL(T const&).c_str());

  static constexpr Char const* check_string(
      Char const* str, [[maybe_unused]] size_t len) noexcept {
    assert((!str && !len) || Traits::length(str) == len);
    return str;
  }
  template <typename String>
  static constexpr String const& check_string(String const& str) noexcept {
    check_string(str.c_str(), str.size());
    return str;
  }

 public:
  /// Constant iterator type over the characters.
  using const_iterator = typename view_type::const_iterator;
  /// Pointer to constant character type.
  using const_pointer = typename view_type::const_pointer;
  /// Reference to constant character type.
  using const_reference = typename view_type::const_reference;
  /// Constant reverse iterator type over the characters.
  using const_reverse_iterator = typename view_type::const_reverse_iterator;
  /// Signed integer type used for iterator differences.
  using difference_type = typename view_type::difference_type;
  /// Iterator type over the characters.
  using iterator = typename view_type::iterator;
  /// Pointer to character type.
  using pointer = typename view_type::pointer;
  /// Reference to character type.
  using reference = typename view_type::reference;
  /// Reverse iterator type over the characters.
  using reverse_iterator = typename view_type::reverse_iterator;
  /// Unsigned integer type used for sizes and positions.
  using size_type = typename view_type::size_type;
  /// Character traits type.
  using traits_type = typename view_type::traits_type;
  /// Character value type.
  using value_type = typename view_type::value_type;

  /// Special position value indicating "not found" or "until the end".
  static constexpr size_type npos = view_type::npos;

 private:
  view_type view_;

 public:
  /// Constructs an empty view.
  constexpr basic_cstring_view() noexcept = default;

  /// Construction from a null pointer literal is disallowed.
  ///
  /// @param p The null pointer literal (this overload is deleted).
  constexpr basic_cstring_view(std::nullptr_t p) = delete;

  /// Constructs a view from a null-terminated C string.
  ///
  /// @param str Null-terminated character array, or nullptr for an empty view.
  /* implicit */ constexpr basic_cstring_view(Char const* str) noexcept
      : view_(!str ? view_type{} : view_type{str}) {}

  /// Constructs a view from a pointer and length.
  ///
  /// Diverges from p3655r3 in that we allow construction from a null pointer.
  /// Members data() and c_str() will return nullptr. Mimics std::string_view
  /// behavior v.s. std::string behavior.
  ///
  /// The paper tries to make std::cstring_view work like a string and not like
  /// a view (a view is a structure with a non-owning pointer and a length),
  /// creating a discrepancy between std::string_view and std::cstring_view.
  ///
  /// @param str Pointer to the null-terminated character buffer.
  /// @param len Length of the string, excluding the null terminator.
  constexpr basic_cstring_view(Char const* str, std::size_t len) noexcept
      : view_(check_string(str, len), len) {}

  /// Constructs a view from any string type exposing a c_str() member.
  ///
  /// @param str String-like object whose null-terminated buffer is viewed.
  template <typename String, typename = detect_c_str<String>>
  /* implicit */ constexpr basic_cstring_view(String const& str) noexcept
      : view_(check_string(str)) {}

  /// Copy-constructs a view from another view.
  ///
  /// @param other The view to copy from.
  constexpr basic_cstring_view(basic_cstring_view const& other) noexcept =
      default;

  /// Copy-assigns from another view.
  ///
  /// @param other The view to copy from.
  /// @return Reference to this view.
  constexpr basic_cstring_view& operator=(
      basic_cstring_view const& other) noexcept = default;

  /// Assigns a null-terminated C string to this view.
  ///
  /// @param str Null-terminated character array to view.
  /// @return Reference to this view.
  constexpr basic_cstring_view& operator=(Char const* str) noexcept {
    view_ = str;
    return *this;
  }

  /// Assigns from any string type exposing a c_str() member.
  ///
  /// @param str String-like object whose null-terminated buffer is viewed.
  /// @return Reference to this view.
  template <typename String, typename..., typename = detect_c_str<String>>
  constexpr basic_cstring_view& operator=(String const& str) noexcept {
    view_ = check_string(str);
    return *this;
  }

  // Accessor methods

  /// Returns a reference to the character at the given position.
  ///
  /// @param pos Position of the character.
  /// @return Reference to the character at pos.
  constexpr const_reference operator[](size_type pos) const {
    return view_[pos];
  }
  /// Returns a reference to the character at the given position, bounds-checked.
  ///
  /// @param pos Position of the character.
  /// @return Reference to the character at pos.
  constexpr const_reference at(size_type pos) const { return view_.at(pos); }
  /// Returns a reference to the first character.
  ///
  /// @return Reference to the first character.
  constexpr const_reference front() const { return view_.front(); }
  /// Returns a reference to the last character.
  ///
  /// @return Reference to the last character.
  constexpr const_reference back() const { return view_.back(); }
  /// Returns a pointer to the underlying null-terminated character buffer.
  ///
  /// @return Pointer to the character data, or nullptr if empty.
  constexpr const_pointer data() const noexcept { return view_.data(); }

  // Iterator methods

  /// Returns an iterator to the first character.
  ///
  /// @return Iterator to the beginning.
  constexpr const_iterator begin() const noexcept { return view_.begin(); }
  /// Returns an iterator past the last character.
  ///
  /// @return Iterator to the end.
  constexpr const_iterator end() const noexcept { return view_.end(); }
  /// Returns a constant iterator to the first character.
  ///
  /// @return Constant iterator to the beginning.
  constexpr const_iterator cbegin() const noexcept { return view_.cbegin(); }
  /// Returns a constant iterator past the last character.
  ///
  /// @return Constant iterator to the end.
  constexpr const_iterator cend() const noexcept { return view_.cend(); }
  /// Returns a reverse iterator to the last character.
  ///
  /// @return Reverse iterator to the beginning of the reversed view.
  constexpr const_reverse_iterator rbegin() const noexcept {
    return view_.rbegin();
  }
  /// Returns a reverse iterator before the first character.
  ///
  /// @return Reverse iterator to the end of the reversed view.
  constexpr const_reverse_iterator rend() const noexcept {
    return view_.rend();
  }
  /// Returns a constant reverse iterator to the last character.
  ///
  /// @return Constant reverse iterator to the beginning of the reversed view.
  constexpr const_reverse_iterator crbegin() const noexcept {
    return view_.crbegin();
  }
  /// Returns a constant reverse iterator before the first character.
  ///
  /// @return Constant reverse iterator to the end of the reversed view.
  constexpr const_reverse_iterator crend() const noexcept {
    return view_.crend();
  }

  // Capacity methods

  /// Returns the number of characters in the view.
  ///
  /// @return The number of characters.
  constexpr size_type size() const noexcept { return view_.size(); }
  /// Returns the number of characters in the view.
  ///
  /// @return The number of characters.
  constexpr size_type length() const noexcept { return view_.length(); }
  /// Returns the maximum number of characters the view can reference.
  ///
  /// @return The maximum size.
  constexpr size_type max_size() const noexcept { return view_.max_size(); }
  /// Checks whether the view is empty.
  ///
  /// @return True if the view contains no characters.
  constexpr bool empty() const noexcept { return view_.empty(); }

  // Modifiers

  /// Removes the first n characters from the view.
  ///
  /// @param n Number of characters to remove from the front.
  constexpr void remove_prefix(size_type n) { view_.remove_prefix(n); }

  // String operations

  /// Finds the first occurrence of a substring.
  ///
  /// @param sv Substring to search for.
  /// @param pos Position to start the search from.
  /// @return Position of the first match, or npos if not found.
  constexpr size_type find(view_type sv, size_type pos = 0) const noexcept {
    return view_.find(sv, pos);
  }
  /// Finds the first occurrence of a character.
  ///
  /// @param ch Character to search for.
  /// @param pos Position to start the search from.
  /// @return Position of the first match, or npos if not found.
  constexpr size_type find(Char ch, size_type pos = 0) const noexcept {
    return view_.find(ch, pos);
  }
  /// Finds the first occurrence of the first count characters of a C string.
  ///
  /// @param s Character array to search for.
  /// @param pos Position to start the search from.
  /// @param count Number of characters of s to match.
  /// @return Position of the first match, or npos if not found.
  constexpr size_type find(
      Char const* s, size_type pos, size_type count) const {
    return view_.find(s, pos, count);
  }
  /// Finds the first occurrence of a null-terminated C string.
  ///
  /// @param s Null-terminated character array to search for.
  /// @param pos Position to start the search from.
  /// @return Position of the first match, or npos if not found.
  constexpr size_type find(Char const* s, size_type pos = 0) const {
    return view_.find(s, pos);
  }

  /// Finds the last occurrence of a substring.
  ///
  /// @param sv Substring to search for.
  /// @param pos Position at which to end the backward search.
  /// @return Position of the last match, or npos if not found.
  constexpr size_type rfind(view_type sv, size_type pos = npos) const noexcept {
    return view_.rfind(sv, pos);
  }
  /// Finds the last occurrence of a character.
  ///
  /// @param ch Character to search for.
  /// @param pos Position at which to end the backward search.
  /// @return Position of the last match, or npos if not found.
  constexpr size_type rfind(Char ch, size_type pos = npos) const noexcept {
    return view_.rfind(ch, pos);
  }
  /// Finds the last occurrence of the first count characters of a C string.
  ///
  /// @param s Character array to search for.
  /// @param pos Position at which to end the backward search.
  /// @param count Number of characters of s to match.
  /// @return Position of the last match, or npos if not found.
  constexpr size_type rfind(
      Char const* s, size_type pos, size_type count) const {
    return view_.rfind(s, pos, count);
  }
  /// Finds the last occurrence of a null-terminated C string.
  ///
  /// @param s Null-terminated character array to search for.
  /// @param pos Position at which to end the backward search.
  /// @return Position of the last match, or npos if not found.
  constexpr size_type rfind(Char const* s, size_type pos = npos) const {
    return view_.rfind(s, pos);
  }

  /// Finds the first character equal to any character in the given set.
  ///
  /// @param sv Set of characters to search for.
  /// @param pos Position to start the search from.
  /// @return Position of the first match, or npos if not found.
  constexpr size_type find_first_of(
      view_type sv, size_type pos = 0) const noexcept {
    return view_.find_first_of(sv, pos);
  }
  /// Finds the first occurrence of the given character.
  ///
  /// @param ch Character to search for.
  /// @param pos Position to start the search from.
  /// @return Position of the first match, or npos if not found.
  constexpr size_type find_first_of(Char ch, size_type pos = 0) const noexcept {
    return view_.find_first_of(ch, pos);
  }
  /// Finds the first character equal to any of the first count characters of s.
  ///
  /// @param s Character array holding the set to search for.
  /// @param pos Position to start the search from.
  /// @param count Number of characters of s forming the set.
  /// @return Position of the first match, or npos if not found.
  constexpr size_type find_first_of(
      Char const* s, size_type pos, size_type count) const {
    return view_.find_first_of(s, pos, count);
  }
  /// Finds the first character equal to any in the null-terminated set s.
  ///
  /// @param s Null-terminated character array holding the set to search for.
  /// @param pos Position to start the search from.
  /// @return Position of the first match, or npos if not found.
  constexpr size_type find_first_of(Char const* s, size_type pos = 0) const {
    return view_.find_first_of(s, pos);
  }

  /// Finds the last character equal to any character in the given set.
  ///
  /// @param sv Set of characters to search for.
  /// @param pos Position at which to end the backward search.
  /// @return Position of the last match, or npos if not found.
  constexpr size_type find_last_of(
      view_type sv, size_type pos = npos) const noexcept {
    return view_.find_last_of(sv, pos);
  }
  /// Finds the last occurrence of the given character.
  ///
  /// @param ch Character to search for.
  /// @param pos Position at which to end the backward search.
  /// @return Position of the last match, or npos if not found.
  constexpr size_type find_last_of(
      Char ch, size_type pos = npos) const noexcept {
    return view_.find_last_of(ch, pos);
  }
  /// Finds the last character equal to any of the first count characters of s.
  ///
  /// @param s Character array holding the set to search for.
  /// @param pos Position at which to end the backward search.
  /// @param count Number of characters of s forming the set.
  /// @return Position of the last match, or npos if not found.
  constexpr size_type find_last_of(
      Char const* s, size_type pos, size_type count) const {
    return view_.find_last_of(s, pos, count);
  }
  /// Finds the last character equal to any in the null-terminated set s.
  ///
  /// @param s Null-terminated character array holding the set to search for.
  /// @param pos Position at which to end the backward search.
  /// @return Position of the last match, or npos if not found.
  constexpr size_type find_last_of(Char const* s, size_type pos = npos) const {
    return view_.find_last_of(s, pos);
  }

  /// Finds the first character not equal to any character in the given set.
  ///
  /// @param sv Set of characters to exclude.
  /// @param pos Position to start the search from.
  /// @return Position of the first non-matching character, or npos if none.
  constexpr size_type find_first_not_of(
      view_type sv, size_type pos = 0) const noexcept {
    return view_.find_first_not_of(sv, pos);
  }
  /// Finds the first character not equal to the given character.
  ///
  /// @param ch Character to exclude.
  /// @param pos Position to start the search from.
  /// @return Position of the first non-matching character, or npos if none.
  constexpr size_type find_first_not_of(
      Char ch, size_type pos = 0) const noexcept {
    return view_.find_first_not_of(ch, pos);
  }
  /// Finds the first character not in the first count characters of s.
  ///
  /// @param s Character array holding the set to exclude.
  /// @param pos Position to start the search from.
  /// @param count Number of characters of s forming the set.
  /// @return Position of the first non-matching character, or npos if none.
  constexpr size_type find_first_not_of(
      Char const* s, size_type pos, size_type count) const {
    return view_.find_first_not_of(s, pos, count);
  }
  /// Finds the first character not in the null-terminated set s.
  ///
  /// @param s Null-terminated character array holding the set to exclude.
  /// @param pos Position to start the search from.
  /// @return Position of the first non-matching character, or npos if none.
  constexpr size_type find_first_not_of(
      Char const* s, size_type pos = 0) const {
    return view_.find_first_not_of(s, pos);
  }

  /// Finds the last character not equal to any character in the given set.
  ///
  /// @param sv Set of characters to exclude.
  /// @param pos Position at which to end the backward search.
  /// @return Position of the last non-matching character, or npos if none.
  constexpr size_type find_last_not_of(
      view_type sv, size_type pos = npos) const noexcept {
    return view_.find_last_not_of(sv, pos);
  }
  /// Finds the last character not equal to the given character.
  ///
  /// @param ch Character to exclude.
  /// @param pos Position at which to end the backward search.
  /// @return Position of the last non-matching character, or npos if none.
  constexpr size_type find_last_not_of(
      Char ch, size_type pos = npos) const noexcept {
    return view_.find_last_not_of(ch, pos);
  }
  /// Finds the last character not in the first count characters of s.
  ///
  /// @param s Character array holding the set to exclude.
  /// @param pos Position at which to end the backward search.
  /// @param count Number of characters of s forming the set.
  /// @return Position of the last non-matching character, or npos if none.
  constexpr size_type find_last_not_of(
      Char const* s, size_type pos, size_type count) const {
    return view_.find_last_not_of(s, pos, count);
  }
  /// Finds the last character not in the null-terminated set s.
  ///
  /// @param s Null-terminated character array holding the set to exclude.
  /// @param pos Position at which to end the backward search.
  /// @return Position of the last non-matching character, or npos if none.
  constexpr size_type find_last_not_of(
      Char const* s, size_type pos = npos) const {
    return view_.find_last_not_of(s, pos);
  }

  /// Compares this view with another view.
  ///
  /// @param sv View to compare against.
  /// @return Negative, zero, or positive if this view is less than, equal to,
  ///     or greater than sv.
  constexpr int compare(view_type sv) const noexcept {
    return view_.compare(sv);
  }
  /// Compares a substring of this view with another view.
  ///
  /// @param pos1 Starting position of the substring in this view.
  /// @param count1 Length of the substring in this view.
  /// @param sv View to compare against.
  /// @return Negative, zero, or positive comparison result.
  constexpr int compare(size_type pos1, size_type count1, view_type sv) const {
    return view_.compare(pos1, count1, sv);
  }
  /// Compares a substring of this view with a substring of another view.
  ///
  /// @param pos1 Starting position of the substring in this view.
  /// @param count1 Length of the substring in this view.
  /// @param sv View to compare against.
  /// @param pos2 Starting position of the substring in sv.
  /// @param count2 Length of the substring in sv.
  /// @return Negative, zero, or positive comparison result.
  constexpr int compare(
      size_type pos1,
      size_type count1,
      view_type sv,
      size_type pos2,
      size_type count2) const {
    return view_.compare(pos1, count1, sv, pos2, count2);
  }
  /// Compares this view with a null-terminated C string.
  ///
  /// @param s Null-terminated character array to compare against.
  /// @return Negative, zero, or positive comparison result.
  constexpr int compare(Char const* s) const { return view_.compare(s); }
  /// Compares a substring of this view with a null-terminated C string.
  ///
  /// @param pos1 Starting position of the substring in this view.
  /// @param count1 Length of the substring in this view.
  /// @param s Null-terminated character array to compare against.
  /// @return Negative, zero, or positive comparison result.
  constexpr int compare(size_type pos1, size_type count1, Char const* s) const {
    return view_.compare(pos1, count1, s);
  }
  /// Compares a substring of this view with a prefix of a C string.
  ///
  /// @param pos1 Starting position of the substring in this view.
  /// @param count1 Length of the substring in this view.
  /// @param s Character array to compare against.
  /// @param count2 Number of characters of s to compare.
  /// @return Negative, zero, or positive comparison result.
  constexpr int compare(
      size_type pos1, size_type count1, Char const* s, size_type count2) const {
    return view_.compare(pos1, count1, s, count2);
  }

  /// Returns a pointer to the null-terminated character buffer.
  ///
  /// @return Pointer to the null-terminated data, or nullptr if empty.
  constexpr Char const* c_str() const noexcept { return data(); }

  /// Converts implicitly to the underlying string_view.
  ///
  /// @return A string_view referring to the same characters.
  constexpr operator view_type() const noexcept { return view_; }

  /// Returns the substring from pos to the end, preserving null-termination.
  ///
  /// @param pos Starting position of the substring.
  /// @return A view of the characters from pos to the end.
  constexpr basic_cstring_view substr(size_type pos = 0) const {
    auto sub = view_.substr(pos);
    return {sub.data(), sub.size()};
  }

  /// Swaps the contents of this view with another view.
  ///
  /// @param other View to swap with.
  constexpr void swap(basic_cstring_view& other) noexcept {
    view_.swap(other.view_);
  }

  /// Checks whether the view begins with the given prefix view.
  ///
  /// @param sv Prefix to test for.
  /// @return True if the view starts with sv.
  constexpr bool starts_with(view_type sv) const noexcept {
    return view_.starts_with(sv);
  }
  /// Checks whether the view begins with the given character.
  ///
  /// @param ch Character to test for.
  /// @return True if the first character equals ch.
  constexpr bool starts_with(Char ch) const noexcept {
    return view_.starts_with(ch);
  }
  /// Checks whether the view begins with the given null-terminated C string.
  ///
  /// @param s Null-terminated prefix to test for.
  /// @return True if the view starts with s.
  constexpr bool starts_with(Char const* s) const {
    return view_.starts_with(s);
  }

  /// Checks whether the view ends with the given suffix view.
  ///
  /// @param sv Suffix to test for.
  /// @return True if the view ends with sv.
  constexpr bool ends_with(view_type sv) const noexcept {
    return view_.ends_with(sv);
  }
  /// Checks whether the view ends with the given character.
  ///
  /// @param ch Character to test for.
  /// @return True if the last character equals ch.
  constexpr bool ends_with(Char ch) const noexcept {
    return view_.ends_with(ch);
  }
  /// Checks whether the view ends with the given null-terminated C string.
  ///
  /// @param s Null-terminated suffix to test for.
  /// @return True if the view ends with s.
  constexpr bool ends_with(Char const* s) const { return view_.ends_with(s); }

#if defined(__cpp_lib_string_contains) && __cpp_lib_string_contains >= 202011L
  /// Checks whether the view contains the given substring.
  ///
  /// @param sv Substring to search for.
  /// @return True if sv occurs within the view.
  constexpr bool contains(view_type sv) const noexcept {
    return view_.contains(sv);
  }
  /// Checks whether the view contains the given character.
  ///
  /// @param ch Character to search for.
  /// @return True if ch occurs within the view.
  constexpr bool contains(Char ch) const noexcept { return view_.contains(ch); }
  /// Checks whether the view contains the given null-terminated C string.
  ///
  /// @param s Null-terminated substring to search for.
  /// @return True if s occurs within the view.
  constexpr bool contains(Char const* s) const { return view_.contains(s); }
#endif

  // Explicitly delete dangerous operations that could break null-termination

  /// Two-argument substr is deleted because it may break null-termination.
  ///
  /// @param pos Starting position of the substring.
  /// @param len Length of the substring.
  /// @return A view of the requested range.
  constexpr basic_cstring_view substr(size_type pos, size_type len) const =
      delete;
  /// Removing a suffix is deleted because it would break null-termination.
  ///
  /// @param n Number of characters that would be removed from the end.
  constexpr void remove_suffix(size_type n) = delete;

  /// Checks two views for equality.
  ///
  /// @param lhs Left-hand view.
  /// @param rhs Right-hand view.
  /// @return True if both views hold the same characters.
  friend bool operator==(self lhs, self rhs) noexcept {
    return view_type(lhs) == view_type(rhs);
  }
  /// Checks two views for inequality.
  ///
  /// @param lhs Left-hand view.
  /// @param rhs Right-hand view.
  /// @return True if the views differ.
  friend bool operator!=(self lhs, self rhs) noexcept {
    return view_type(lhs) != view_type(rhs);
  }
  /// Orders two views lexicographically.
  ///
  /// @param lhs Left-hand view.
  /// @param rhs Right-hand view.
  /// @return True if lhs compares less than rhs.
  friend bool operator<(self lhs, self rhs) noexcept {
    return view_type(lhs) < view_type(rhs);
  }
  /// Orders two views lexicographically.
  ///
  /// @param lhs Left-hand view.
  /// @param rhs Right-hand view.
  /// @return True if lhs compares less than or equal to rhs.
  friend bool operator<=(self lhs, self rhs) noexcept {
    return view_type(lhs) <= view_type(rhs);
  }
  /// Orders two views lexicographically.
  ///
  /// @param lhs Left-hand view.
  /// @param rhs Right-hand view.
  /// @return True if lhs compares greater than rhs.
  friend bool operator>(self lhs, self rhs) noexcept {
    return view_type(lhs) > view_type(rhs);
  }
  /// Orders two views lexicographically.
  ///
  /// @param lhs Left-hand view.
  /// @param rhs Right-hand view.
  /// @return True if lhs compares greater than or equal to rhs.
  friend bool operator>=(self lhs, self rhs) noexcept {
    return view_type(lhs) >= view_type(rhs);
  }
};

/// A null-terminated string view over char.
using cstring_view = basic_cstring_view<char>;

/// Writes a view to an output stream.
///
/// @param out Output stream to write to.
/// @param str View to write.
/// @return Reference to the output stream.
template <typename Char, typename Traits>
std::basic_ostream<Char, Traits>& operator<<(
    std::basic_ostream<Char, Traits>& out,
    basic_cstring_view<Char, Traits> str) {
  return out << std::basic_string_view<Char, Traits>(str);
}

inline namespace literals {
inline namespace string_literals {

/// User-defined literal building a cstring_view from a string literal.
///
/// @param str Pointer to the literal's null-terminated character buffer.
/// @param len Length of the literal, excluding the null terminator.
/// @return A cstring_view referring to the literal.
constexpr cstring_view operator""_csv(
    const char* str, std::size_t len) noexcept {
  return cstring_view(str, len);
}

} // namespace string_literals
} // namespace literals

} // namespace folly

// std::hash specialization
namespace std {
/// Hash specialization for basic_cstring_view, matching string_view hashing.
template <typename Char, typename Traits>
struct hash<folly::basic_cstring_view<Char, Traits>>
    : hash<std::basic_string_view<Char, Traits>> {};
} // namespace std

/// The {fmt} formatting library namespace.
namespace fmt {
/// Formatter specialization for basic_cstring_view, reusing string_view formatting.
template <typename Char, typename Traits>
struct formatter<folly::basic_cstring_view<Char, Traits>, Char>
    : formatter<std::basic_string_view<Char, Traits>, Char> {};
} // namespace fmt
