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

//
// Docs: https://fburl.com/fbcref_string
//

/**
 * Convenience functions for working with strings.
 *
 * @file String.h
 */

#pragma once
#define FOLLY_STRING_H_

#include <cstdarg>
#include <exception>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <folly/Conv.h>
#include <folly/ExceptionString.h>
#include <folly/Optional.h>
#include <folly/Portability.h>
#include <folly/Range.h>
#include <folly/ScopeGuard.h>
#include <folly/Traits.h>
#include <folly/Unit.h>
#include <folly/detail/SimpleSimdStringUtils.h>
#include <folly/detail/SplitStringSimd.h>

namespace folly {

/// SplitOptions
///
/// Options for controlling split() behavior. This class uses a builder pattern
/// to allow for easy configuration and method chaining.
class SplitOptions {
 public:
  /// Default values for the split options.
  struct Defaults {
    /// preallocate
    ///
    /// If true, split will count the expected number of tokens first
    /// and pre-allocate container capacity using grow_capacity_by.
    /// This can improve performance when splitting large strings with
    /// many tokens, but may add overhead for small strings.
    static constexpr bool preallocate = false;

    /// ignoreEmpty
    ///
    /// If true, adjacent delimiters are treated as one single separator
    /// (ignoring empty tokens), otherwise empty tokens are generated.
    static constexpr bool ignore_empty = false;
  };

  /// Returns whether preallocation is enabled.
  /// \returns True when split preallocates container capacity.
  constexpr bool preallocate() const { return preallocate_; }
  /// Enables or disables preallocation.
  /// \param enable Whether to preallocate container capacity.
  /// \returns A reference to this options object for chaining.
  constexpr SplitOptions& preallocate(bool enable) {
    preallocate_ = enable;
    return *this;
  }

  /// Returns whether empty tokens are ignored.
  /// \returns True when adjacent delimiters are merged.
  constexpr bool ignore_empty() const { return ignore_empty_; }
  /// Enables or disables ignoring of empty tokens.
  /// \param enable Whether to treat adjacent delimiters as one separator.
  /// \returns A reference to this options object for chaining.
  constexpr SplitOptions& ignore_empty(bool enable) {
    ignore_empty_ = enable;
    return *this;
  }

 private:
  bool preallocate_ = Defaults::preallocate;
  bool ignore_empty_ = Defaults::ignore_empty;
};

/**
 * @overloadbrief C-escape a string.
 *
 * Make the string suitable for representation as a C string
 * literal.  Appends the result to the output string.
 *
 * Backslashes all occurrences of backslash, double-quote, and question mark:
 *   "  ->  \"
 *   \  ->  \\
 *   ?  ->  \?
 *
 * (Question marks are escaped in order to prevent creating trigraphs in
 * the output -- "??x" where x is one of "=/'()!<>-")
 *
 * Also backslashes certain whitespace characters: \n, \r, \t
 *
 * Replaces all non-printable ASCII characters with backslash-octal
 * representation:
 *   <ASCII 254> -> \376
 *
 * Note that we use backslash-octal instead of backslash-hex because the octal
 * representation is guaranteed to consume no more than 3 characters; "\3760"
 * represents two characters, one with value 254, and one with value 48 ('0'),
 * whereas "\xfe0" represents only one character (with value 4064, which leads
 * to implementation-defined behavior).
 *
 * \param str The input string to escape.
 * \param out The output string that the escaped result is appended to.
 */
template <class String>
void cEscape(StringPiece str, String& out);

/**
 * Similar to cEscape above, but returns the escaped string.
 *
 * \param str The input string to escape.
 * \returns The escaped string.
 */
template <class String>
String cEscape(StringPiece str) {
  String out;
  cEscape(str, out);
  return out;
}

/**
 * @overloadbrief C-Unescape a string.
 *
 * The opposite of cEscape above.  Appends the result
 * to the output string.
 *
 * Recognizes the standard C escape sequences:
 *
 * \code
 * \' \" \? \\ \a \b \f \n \r \t \v
 * \[0-7]+
 * \x[0-9a-fA-F]+
 * \endcode
 *
 * In strict mode (default), throws std::invalid_argument if it encounters
 * an unrecognized escape sequence.  In non-strict mode, it leaves
 * the escape sequence unchanged.
 *
 * \param str The input string to unescape.
 * \param out The output string that the unescaped result is appended to.
 * \param strict Whether to throw on an unrecognized escape sequence.
 */
template <class String>
void cUnescape(StringPiece str, String& out, bool strict = true);

/**
 * Similar to cUnescape above, but returns the escaped string.
 *
 * \param str The input string to unescape.
 * \param strict Whether to throw on an unrecognized escape sequence.
 * \returns The unescaped string.
 */
template <class String>
String cUnescape(StringPiece str, bool strict = true) {
  String out;
  cUnescape(str, out, strict);
  return out;
}

/**
 * @overloadbrief URI-escape a string.
 *
 * Appends the result to the output string.
 *
 * Alphanumeric characters and other characters marked as "unreserved" in RFC
 * 3986 ( -_.~ ) are left unchanged.  In PATH mode, the forward slash (/) is
 * also left unchanged.  In QUERY mode, spaces are replaced by '+'.  All other
 * characters are percent-encoded.
 */
enum class UriEscapeMode : unsigned char {
  // The values are meaningful, see generate_escape_tables.py
  ALL = 0, ///< Percent-encode every reserved character.
  QUERY = 1, ///< Query mode, where spaces become '+'.
  PATH = 2 ///< Path mode, where the forward slash is left unchanged.
};
/// Percent-encodes a string according to the given escape mode.
/// \param str The input string to escape.
/// \param out The output string that the escaped result is appended to.
/// \param mode The escaping mode to apply.
template <class String>
void uriEscape(
    StringPiece str, String& out, UriEscapeMode mode = UriEscapeMode::ALL);

/**
 * Similar to uriEscape above, but returns the escaped string.
 *
 * \param str The input string to escape.
 * \param mode The escaping mode to apply.
 * \returns The escaped string.
 */
template <class String>
String uriEscape(StringPiece str, UriEscapeMode mode = UriEscapeMode::ALL) {
  String out;
  uriEscape(str, out, mode);
  return out;
}

/**
 * @overloadbrief URI-unescape a string.
 *
 * Appends the result to the output string.
 *
 * In QUERY mode, '+' are replaced by space.  %XX sequences are decoded if
 * XX is a valid hex sequence, otherwise we return an unexpected
 * std::invalid_argument.
 *
 * \param str The input string to unescape.
 * \param out The output string that the unescaped result is appended to.
 * \param mode The escaping mode that was used.
 * \returns True on success, false on malformed input.
 */
template <class String>
bool tryUriUnescape(
    StringPiece str, String& out, UriEscapeMode mode = UriEscapeMode::ALL);

/**
 * Similar to tryUriUnescape above, but returning the unescaped string as a
 * folly::Expected.
 *
 * \param str The input string to unescape.
 * \param mode The escaping mode that was used.
 * \returns The unescaped string, or folly::none on malformed input.
 */
template <class String>
folly::Optional<String> tryUriUnescape(
    StringPiece str, UriEscapeMode mode = UriEscapeMode::ALL) {
  String out;
  auto success = tryUriUnescape(str, out, mode);

  if (!success) {
    return folly::none;
  }

  return out;
}

/**
 * Similar to tryUriUnescape above, but without folly::Expected wrapping, and
 * throwing std::invalid_argument on malformed input.
 *
 * \param str The input string to unescape.
 * \param out The output string that the unescaped result is appended to.
 * \param mode The escaping mode that was used.
 */
template <class String>
void uriUnescape(
    StringPiece str, String& out, UriEscapeMode mode = UriEscapeMode::ALL);

/**
 * Similar to uriUnescape above, but returns the unescaped string.
 *
 * \param str The input string to unescape.
 * \param mode The escaping mode that was used.
 * \returns The unescaped string.
 */
template <class String>
String uriUnescape(StringPiece str, UriEscapeMode mode = UriEscapeMode::ALL) {
  String out;
  uriUnescape(str, out, mode);
  return out;
}

/**
 * @overloadbrief printf into a string.
 *
 * stringPrintf is much like printf but deposits its result into a
 * string. Two signatures are supported: the first simply returns the
 * resulting string, and the second appends the produced characters to
 * the specified string and returns a reference to it.
 *
 * \param format The printf-style format string.
 * \returns The formatted string.
 */
std::string stringPrintf(FOLLY_PRINTF_FORMAT const char* format, ...)
    FOLLY_PRINTF_FORMAT_ATTR(1, 2);

/**
 * Similar to stringPrintf, with different signature.
 *
 * \param out The string that the formatted output is written to.
 * \param format The printf-style format string.
 */
void stringPrintf(std::string* out, FOLLY_PRINTF_FORMAT const char* format, ...)
    FOLLY_PRINTF_FORMAT_ATTR(2, 3);

/**
 * Append printf-style output to string.
 *
 * \param output The string that the formatted output is appended to.
 * \param format The printf-style format string.
 * \returns A reference to the output string.
 */
std::string& stringAppendf(
    std::string* output, FOLLY_PRINTF_FORMAT const char* format, ...)
    FOLLY_PRINTF_FORMAT_ATTR(2, 3);

/**
 * @overloadbrief stringPrintf with va_list argument
 *
 * As with vsnprintf() itself, the value of ap is undefined after the call.
 * These functions do not call va_end() on ap.
 *
 * \param format The printf-style format string.
 * \param ap The variadic argument list.
 * \returns The formatted string.
 */
std::string stringVPrintf(const char* format, va_list ap);
/**
 * Similar to stringVPrintf, writing to an existing string.
 *
 * \param out The string that the formatted output is written to.
 * \param format The printf-style format string.
 * \param ap The variadic argument list.
 */
void stringVPrintf(std::string* out, const char* format, va_list ap);

/**
 * Append va_list printf-style output to string.
 *
 * \param out The string that the formatted output is appended to.
 * \param format The printf-style format string.
 * \param ap The variadic argument list.
 * \returns A reference to the output string.
 */
std::string& stringVAppendf(std::string* out, const char* format, va_list ap);

/**
 * Backslashify a string.
 *
 * That is, replace non-printable characters
 * with C-style (but NOT C compliant) "\xHH" encoding.  If hex_style
 * is false, then shorthand notations like "\0" will be used instead
 * of "\x00" for the most common backslash cases.
 *
 * There are two forms, one returning the input string, and one
 * creating output in the specified output string.
 *
 * This is mainly intended for printing to a terminal, so it is not
 * particularly optimized.
 *
 * Do *not* use this in situations where you expect to be able to feed
 * the string to a C or C++ compiler, as there are nuances with how C
 * parses such strings that lead to failures.  This is for display
 * purposed only.  If you want a string you can embed for use in C or
 * C++, use cEscape instead.  This function is for display purposes
 * only.
 *
 * \param input The input string to backslashify.
 * \param output The output string that the result is written to.
 * \param hex_style Whether to always use "\xHH" instead of shorthand escapes.
 */
template <class OutputString>
void backslashify(
    folly::StringPiece input, OutputString& output, bool hex_style = false);

/// Backslashify a string, returning the result.
/// \param input The input string to backslashify.
/// \param hex_style Whether to always use "\xHH" instead of shorthand escapes.
/// \returns The backslashified string.
template <class OutputString = std::string>
OutputString backslashify(StringPiece input, bool hex_style = false) {
  OutputString output;
  backslashify(input, output, hex_style);
  return output;
}

/**
 * Take a string and "humanify" it -- that is, make it look better.
 *
 * Since "better" is subjective, caveat emptor.  The basic approach is
 * to count the number of unprintable characters.  If there are none,
 * then the output is the input.  If there are relatively few, or if
 * there is a long "enough" prefix of printable characters, use
 * backslashify.  If it is mostly binary, then simply hex encode.
 *
 * This is an attempt to make a computer smart, and so likely is wrong
 * most of the time.
 *
 * \param input The input string to humanify.
 * \param output The output string that the result is written to.
 */
template <class String1, class String2>
void humanify(const String1& input, String2& output);

/// Humanify a string, returning the result.
/// \param input The input string to humanify.
/// \returns The humanified string.
template <class String>
String humanify(const String& input) {
  String output;
  humanify(input, output);
  return output;
}

/**
 * Convert input to hexadecimal representation.
 *
 * Same functionality as Python's binascii.hexlify.  Returns true
 * on successful conversion.
 *
 * If append_output is true, append data to the output rather than
 * replace it.
 *
 * \param input The binary data to convert.
 * \param output The output string that the hex representation is written to.
 * \param append Whether to append to output rather than replace it.
 * \returns True on successful conversion.
 */
template <class InputString, class OutputString>
bool hexlify(
    const InputString& input, OutputString& output, bool append = false);

/// Convert byte range to hexadecimal representation, returning the result.
/// \param input The binary data to convert.
/// \returns The hex representation of the input.
template <class OutputString = std::string>
OutputString hexlify(ByteRange input) {
  OutputString output;
  if (!hexlify(input, output)) {
    // hexlify() currently always returns true, so this can't really happen
    throw_exception<std::runtime_error>("hexlify failed");
  }
  return output;
}

/// Convert string to hexadecimal representation, returning the result.
/// \param input The data to convert.
/// \returns The hex representation of the input.
template <class OutputString = std::string>
OutputString hexlify(StringPiece input) {
  return hexlify<OutputString>(ByteRange{input});
}

/**
 * Get binary data from hexadecimal representation.
 *
 * Same functionality as Python's binascii.unhexlify.  Returns true
 * on successful conversion.
 *
 * \param input The hexadecimal representation to convert.
 * \param output The output string that the binary data is written to.
 * \returns True on successful conversion.
 */
template <class InputString, class OutputString>
bool unhexlify(const InputString& input, OutputString& output);

/// Get binary data from hexadecimal representation, returning the result.
/// \param input The hexadecimal representation to convert.
/// \returns The decoded binary data.
template <class OutputString = std::string>
OutputString unhexlify(StringPiece input) {
  OutputString output;
  if (!unhexlify(input, output)) {
    // unhexlify() fails if the input has non-hexidecimal characters,
    // or if it doesn't consist of a whole number of bytes
    throw_exception<std::domain_error>("unhexlify() called with non-hex input");
  }
  return output;
}

/// Unit families understood by prettyPrint and prettyToDouble.
enum PrettyType {
  PRETTY_TIME, ///< Time units: s, ms, us, ns, etc.
  PRETTY_TIME_HMS, ///< Time units including hours and minutes.

  PRETTY_BYTES_METRIC, ///< Byte units scaling by 1000 (kB, MB, GB).
  PRETTY_BYTES_BINARY, ///< Byte units scaling by 1024 (kB, MB, GB).
  PRETTY_BYTES = PRETTY_BYTES_BINARY, ///< Alias for PRETTY_BYTES_BINARY.
  PRETTY_BYTES_BINARY_IEC, ///< IEC byte units (KiB, MiB, GiB).
  PRETTY_BYTES_IEC = PRETTY_BYTES_BINARY_IEC, ///< Alias for PRETTY_BYTES_BINARY_IEC.

  PRETTY_UNITS_METRIC, ///< Generic units scaling by 1000 (k, M, G).
  PRETTY_UNITS_BINARY, ///< Generic units scaling by 1024 (k, M, G).
  PRETTY_UNITS_BINARY_IEC, ///< Generic IEC units (Ki, Mi, Gi).

  PRETTY_SI, ///< Full SI metric prefixes from yocto to Yotta.

  PRETTY_BITS_METRIC, ///< Bit units scaling by 1000 (kb, Mb, Gb).
  PRETTY_BITS = PRETTY_BITS_METRIC, ///< Alias for PRETTY_BITS_METRIC.

  PRETTY_NUM_TYPES, ///< Number of pretty types.
};

/**
 * Pretty printer for numbers with units.
 *
 * A pretty-printer for numbers that appends suffixes of units of the
 * given type.  It prints 4 sig-figs of value with the most
 * appropriate unit.
 *
 * If `addSpace' is true, we put a space between the units suffix and
 * the value.
 *
 * Current types are:
 *     PRETTY_TIME         - s, ms, us, ns, etc.
 *     PRETTY_TIME_HMS     - h, m, s, ms, us, ns, etc.
 *     PRETTY_BYTES_METRIC - kB, MB, GB, etc (goes up by 10^3 = 1000 each time)
 *     PRETTY_BYTES        - kB, MB, GB, etc (goes up by 2^10 = 1024 each time)
 *     PRETTY_BYTES_IEC    - KiB, MiB, GiB, etc
 *     PRETTY_UNITS_METRIC - k, M, G, etc (goes up by 10^3 = 1000 each time)
 *     PRETTY_UNITS_BINARY - k, M, G, etc (goes up by 2^10 = 1024 each time)
 *     PRETTY_UNITS_BINARY_IEC - Ki, Mi, Gi, etc
 *     PRETTY_SI           - full SI metric prefixes from yocto to Yotta
 *                           http://en.wikipedia.org/wiki/Metric_prefix
 *     PRETTY_BITS_METRIC  - kb, Mb, Gb, etc (goes up by 10^3 = 1000 each time)
 *                           Useful for network bandwidth (e.g., 1 Gbps)
 *     PRETTY_BITS         - alias for PRETTY_BITS_METRIC
 *
 * \param val The value to format.
 * \param type The unit family to use.
 * \param addSpace Whether to insert a space between the value and its unit.
 * \returns The pretty-printed value.
 */
std::string prettyPrint(double val, PrettyType type, bool addSpace = true);

/**
 * @overloadbrief Reverse prettyPrint.
 *
 * This utility converts StringPiece in pretty format (look above) to double,
 * with progress information. Alters the  StringPiece parameter
 * to get rid of the already-parsed characters.
 * Expects string in form <floating point number> {space}* [<suffix>]
 * If string is not in correct format, utility finds longest valid prefix and
 * if there at least one, returns double value based on that prefix and
 * modifies string to what is left after parsing. Throws and std::range_error
 * exception if there is no correct parse.
 * Examples(for PRETTY_UNITS_METRIC):
 * '10M' => 10 000 000
 * '10 M' => 10 000 000
 * '10' => 10
 * '10 Mx' => 10 000 000, prettyString == "x"
 * 'abc' => throws std::range_error
 *
 * \param prettyString The pretty-formatted string to parse; advanced past the
 *   parsed prefix.
 * \param type The unit family to interpret.
 * \returns The parsed numeric value.
 */
double prettyToDouble(
    folly::StringPiece* const prettyString, const PrettyType type);

/**
 * Same as prettyToDouble(folly::StringPiece*, PrettyType), but
 * expects whole string to be correctly parseable. Throws std::range_error
 * otherwise
 *
 * \param prettyString The pretty-formatted string to parse.
 * \param type The unit family to interpret.
 * \returns The parsed numeric value.
 */
double prettyToDouble(folly::StringPiece prettyString, const PrettyType type);

/**
 * @overloadbrief Write a hex dump of size bytes starting at ptr to out.
 *
 * The hex dump is formatted as follows:
 *
 * for the string "abcdefghijklmnopqrstuvwxyz\x02"
00000000  61 62 63 64 65 66 67 68  69 6a 6b 6c 6d 6e 6f 70  |abcdefghijklmnop|
00000010  71 72 73 74 75 76 77 78  79 7a 02                 |qrstuvwxyz.     |
 *
 * that is, we write 16 bytes per line, both as hex bytes and as printable
 * characters.  Non-printable characters are replaced with '.'
 * Lines are written to out one by one (one StringPiece at a time) without
 * delimiters.
 *
 * \param ptr Pointer to the start of the data to dump.
 * \param size Number of bytes to dump.
 * \param out Output iterator that the formatted lines are written to.
 */
template <class OutIt>
void hexDump(const void* ptr, size_t size, OutIt out);

/**
 * Return the hex dump of size bytes starting at ptr as a string.
 *
 * \param ptr Pointer to the start of the data to dump.
 * \param size Number of bytes to dump.
 * \returns The formatted hex dump.
 */
std::string hexDump(const void* ptr, size_t size);

/**
 * Pretty print an errno.
 *
 * Return a string containing the description of the given errno value.
 * Takes care not to overwrite the actual system errno, so calling
 * errnoStr(errno) is valid.
 *
 * \param err The errno value to describe.
 * \returns A string describing the errno value.
 */
std::string errnoStr(int err);

/// Forward declaration of the small_vector container.
template <typename T, std::size_t M, typename P>
class small_vector;

/// Forward declaration of the fbvector container.
template <typename T, typename Allocator>
class fbvector;

namespace detail {

// We don't use SimdSplitByCharIsDefinedFor because
// we would like the user to get an error where they could use SIMD
// implementation but didn't use quite correct parameters.
template <typename>
struct IsSplitSupportedContainer : std::false_type {};

template <typename T>
using HasSimdSplitCompatibleValueType =
    std::is_convertible<typename T::value_type, folly::StringPiece>;

template <typename T, typename A>
struct IsSplitSupportedContainer<std::vector<T, A>> : std::true_type {};

template <typename T, typename A>
struct IsSplitSupportedContainer<fbvector<T, A>> : std::true_type {};

template <typename T, std::size_t M, typename P>
struct IsSplitSupportedContainer<small_vector<T, M, P>> : std::true_type {};

template <typename>
struct IsSimdSupportedDelim : std::false_type {};

template <>
struct IsSimdSupportedDelim<char> : std::true_type {};

} // namespace detail

/**
 * Split a string into a list of tokens by delimiter.
 *
 * The split interface here supports different output types, selected
 * at compile time: StringPiece, fbstring, or std::string.  If you are
 * using a vector to hold the output, it detects the type based on
 * what your vector contains.  If the output vector is not empty, split
 * will append to the end of the vector.
 *
 * You can also use splitTo() to write the output to an arbitrary
 * OutputIterator (e.g. std::inserter() on a std::set<>), in which
 * case you have to tell the function the type.  (Rationale:
 * OutputIterators don't have a value_type, so we can't detect the
 * type in splitTo without being told.)
 *
 * Examples:
 *
 *   std::vector<folly::StringPiece> v;
 *   folly::split(':', "asd:bsd", v);
 *
 *   folly::small_vector<folly::StringPiece, 3> v;
 *   folly::split(':', "asd:bsd:csd", v)
 *
 *   std::set<StringPiece> s;
 *   folly::splitTo<StringPiece>("::", "asd::bsd::asd::csd",
 *    std::inserter(s, s.begin()));
 *
 * Split also takes a flag (ignoreEmpty) that indicates whether adjacent
 * delimiters should be treated as one single separator (ignoring empty tokens)
 * or not (generating empty tokens).
 *
 * \param delimiter The delimiter to split on.
 * \param input The string to split.
 * \param out The container that the resulting tokens are stored in.
 * \param ignoreEmpty Whether to merge adjacent delimiters and drop empty tokens.
 */

template <class Delim, class String, class OutputType>
  requires(
      detail::IsSimdSupportedDelim<Delim>::value &&
      detail::HasSimdSplitCompatibleValueType<OutputType>::value &&
      detail::IsSplitSupportedContainer<OutputType>::value)
FOLLY_ALWAYS_INLINE void split(
    const Delim& delimiter,
    const String& input,
    OutputType& out,
    const bool ignoreEmpty = false) {
  return detail::simdSplitByChar(delimiter, input, out, ignoreEmpty);
}

/// Split a string into a list of tokens by delimiter (non-SIMD overload).
/// \param delimiter The delimiter to split on.
/// \param input The string to split.
/// \param out The container that the resulting tokens are stored in.
/// \param ignoreEmpty Whether to merge adjacent delimiters and drop empty
///   tokens.
template <class Delim, class String, class OutputType>
  requires(
      (!detail::IsSimdSupportedDelim<Delim>::value ||
       !detail::HasSimdSplitCompatibleValueType<OutputType>::value) &&
      detail::IsSplitSupportedContainer<OutputType>::value)
void split(
    const Delim& delimiter,
    const String& input,
    OutputType& out,
    const bool ignoreEmpty = false);

/**
 * Split a string into a list of tokens by delimiter with options.
 *
 * Same as split() above but with additional options to control behavior.
 * The SplitOptions allow enabling preallocation which can improve performance
 * when splitting large strings with many expected tokens.
 *
 * \param delimiter The delimiter to split on.
 * \param input The string to split.
 * \param out The container that the resulting tokens are stored in.
 * \param options Options controlling split behavior.
 */
template <class Delim, class String, class OutputType>
  requires detail::IsSplitSupportedContainer<OutputType>::value
void split(
    const Delim& delimiter,
    const String& input,
    OutputType& out,
    const SplitOptions& options);

/**
 * split, to an output iterator
 *
 * \param delimiter The delimiter to split on.
 * \param input The string to split.
 * \param out The output iterator that the resulting tokens are written to.
 * \param ignoreEmpty Whether to merge adjacent delimiters and drop empty tokens.
 */
template <
    class OutputValueType,
    class Delim,
    class String,
    class OutputIterator>
void splitTo(
    const Delim& delimiter,
    const String& input,
    OutputIterator out,
    const bool ignoreEmpty = false);

namespace detail {
template <typename Void, typename OutputType>
struct IsConvertible : std::false_type {};

template <>
struct IsConvertible<void, decltype(std::ignore)> : std::true_type {};

template <typename OutputType>
struct IsConvertible<
    std::void_t<decltype(parseTo(StringPiece{}, std::declval<OutputType&>()))>,
    OutputType> : std::true_type {};
} // namespace detail
/// Trait detecting whether a type can be a split() output field.
template <typename OutputType>
struct IsConvertible : detail::IsConvertible<void, OutputType> {};

/**
 * Split a string into a fixed number of string pieces and/or numeric types
 * by delimiter. Conversions are supported for any type which folly:to<> can
 * target, including all overloads of parseTo(). Returns 'true' if the fields
 * were all successfully populated.  Returns 'false' if there were too few
 * fields in the input, or too many fields if exact=true.  Casting exceptions
 * will not be caught.
 *
 * Examples:
 *
 *  folly::StringPiece name, key, value;
 *  if (folly::split('\t', line, name, key, value))
 *    ...
 *
 *  folly::StringPiece name;
 *  double value;
 *  int id;
 *  if (folly::split('\t', line, name, value, id))
 *    ...
 *
 * The 'exact' template parameter specifies how the function behaves when too
 * many fields are present in the input string. When 'exact' is set to its
 * default value of 'true', a call to split will fail if the number of fields in
 * the input string does not exactly match the number of output parameters
 * passed. If 'exact' is overridden to 'false', all remaining fields will be
 * stored, unsplit, in the last field, as shown below:
 *
 *  folly::StringPiece x, y.
 *  if (folly::split<false>(':', "a:b:c", x, y))
 *    assert(x == "a" && y == "b:c");
 *
 * Note that this will likely not work if the last field's target is of numeric
 * type, in which case folly::to<> will throw an exception.
 *
 * \param delimiter The delimiter to split on.
 * \param input The string to split.
 * \param outputs The output fields to populate, in order.
 * \returns True if all fields were successfully populated.
 */
template <bool exact = true, class Delim, class... OutputTypes>
  requires(
      StrictConjunction<IsConvertible<OutputTypes>...>::value &&
      sizeof...(OutputTypes) >= 1)
bool split(const Delim& delimiter, StringPiece input, OutputTypes&... outputs);

/// Error type for trySplitTo(), below.
struct SubstringConversionCode {
  StringPiece substring; ///< The substring that failed to convert.
  ConversionCode code; ///< The conversion error code.
  /// Compares two error values for equality.
  /// \param other The error to compare against.
  /// \returns True when both the substring and code are equal.
  bool operator==(const SubstringConversionCode& other) const;
};

/**
 * Try to split a string into a fixed number of fields by delimiter, using
 * folly::tryTo<> for conversions. types by delimiter.
 * - On success, all output values will be initialized and the 'Unit{}' value is
 *   returned. Arguments are assigned in reverse order.
 * - On failure, the first failing 'ConversionCode' is returned with its
 *   associated substring in a 'SubstringConversionCode'.
 * - String splitting is performed prior to each conversion; field values will
 *   not contain the delimiter.
 * - All custom error codes are mapped to ConversionCode::CUSTOM.
 *
 * Examples:
 *
 *  folly::StringPiece name, key, value;
 *  if (folly::trySplitTo(line, '\t',  name, key, value))
 *    ...
 *
 *  folly::StringPiece name;
 *  double value;
 *  int id;
 *  if (folly::trySplitTo(line, '\t', name, value, id))
 *    ...
 *
 * \param input The string to split.
 * \param delimiter The delimiter to split on.
 * \param outputs The output fields to populate, in reverse order.
 * \returns Unit on success, or the first conversion error on failure.
 */
template <class Delim, class... OutputTypes>
  requires StrictConjunction<IsConvertible<OutputTypes>...>::value
Expected<Unit, SubstringConversionCode> trySplitTo(
    StringPiece input, const Delim& delimiter, OutputTypes&... outputs);

/**
 * Join list of tokens.
 *
 * Stores a string representation of tokens in the same order with
 * delimiter between each element.
 *
 * \param delimiter The delimiter placed between elements.
 * \param begin Iterator to the first token.
 * \param end Iterator past the last token.
 * \param output The string that the joined result is stored in.
 */
template <class Delim, class Iterator, class String>
void join(const Delim& delimiter, Iterator begin, Iterator end, String& output);

/// Join the elements of a container into a string.
/// \param delimiter The delimiter placed between elements.
/// \param container The container of tokens to join.
/// \param output The string that the joined result is stored in.
template <class Delim, class Container, class String>
void join(const Delim& delimiter, const Container& container, String& output) {
  join(delimiter, container.begin(), container.end(), output);
}

/// Join the elements of an initializer list into a string.
/// \param delimiter The delimiter placed between elements.
/// \param values The values to join.
/// \param output The string that the joined result is stored in.
template <class Delim, class Value, class String>
void join(
    const Delim& delimiter,
    const std::initializer_list<Value>& values,
    String& output) {
  join(delimiter, values.begin(), values.end(), output);
}

/// Join the elements of a container, returning the result.
/// \param delimiter The delimiter placed between elements.
/// \param container The container of tokens to join.
/// \returns The joined string.
template <class Delim, class Container>
std::string join(const Delim& delimiter, const Container& container) {
  std::string output;
  join(delimiter, container.begin(), container.end(), output);
  return output;
}

/// Join the elements of an initializer list, returning the result.
/// \param delimiter The delimiter placed between elements.
/// \param values The values to join.
/// \returns The joined string.
template <class Delim, class Value>
std::string join(
    const Delim& delimiter, const std::initializer_list<Value>& values) {
  std::string output;
  join(delimiter, values.begin(), values.end(), output);
  return output;
}

/// Join a range of tokens, returning the result.
/// \param delimiter The delimiter placed between elements.
/// \param begin Iterator to the first token.
/// \param end Iterator past the last token.
/// \returns The joined string.
template <
    class Delim,
    class Iterator,
    typename std::enable_if<std::is_base_of<
        std::forward_iterator_tag,
        typename std::iterator_traits<Iterator>::iterator_category>::value>::
        type* = nullptr>
std::string join(const Delim& delimiter, Iterator begin, Iterator end) {
  std::string output;
  join(delimiter, begin, end, output);
  return output;
}

/**
 * Remove leading whitespace.
 *
 * Returns a subpiece with all whitespace removed from the front of @sp.
 * Whitespace means any of [' ', '\n', '\r', '\t'].
 *
 * \param sp The input piece to trim.
 * \returns A subpiece with leading whitespace removed.
 */
StringPiece ltrimWhitespace(StringPiece sp);

/**
 * Remove trailing whitespace.
 *
 * Returns a subpiece with all whitespace removed from the back of @sp.
 * Whitespace means any of [' ', '\n', '\r', '\t'].
 *
 * \param sp The input piece to trim.
 * \returns A subpiece with trailing whitespace removed.
 */
StringPiece rtrimWhitespace(StringPiece sp);

/**
 * Remove leading and trailing whitespace.
 *
 * Returns a subpiece with all whitespace removed from the back and front of
 * @sp. Whitespace means any of [' ', '\n', '\r', '\t'].
 *
 * \param sp The input piece to trim.
 * \returns A subpiece with leading and trailing whitespace removed.
 */
inline StringPiece trimWhitespace(StringPiece sp) {
  return ltrimWhitespace(rtrimWhitespace(sp));
}

/**
 * DEPRECATED: Use ltrimWhitespace instead
 *
 * Returns a subpiece with all whitespace removed from the front of @sp.
 * Whitespace means any of [' ', '\n', '\r', '\t'].
 *
 * \param sp The input piece to trim.
 * \returns A subpiece with leading whitespace removed.
 */
inline StringPiece skipWhitespace(StringPiece sp) {
  return ltrimWhitespace(sp);
}

/**
 * Specify characters to ltrim.
 *
 * Returns a subpiece with all characters the provided @toTrim returns true
 * for removed from the front of @sp.
 *
 * \param sp The input piece to trim.
 * \param toTrim Predicate selecting characters to remove.
 * \returns A subpiece with the matching leading characters removed.
 */
template <typename ToTrim>
StringPiece ltrim(StringPiece sp, ToTrim toTrim) {
  while (!sp.empty() && toTrim(sp.front())) {
    sp.pop_front();
  }

  return sp;
}

/**
 * Specify characters to rtrim.
 *
 * Returns a subpiece with all characters the provided @toTrim returns true
 * for removed from the back of @sp.
 *
 * \param sp The input piece to trim.
 * \param toTrim Predicate selecting characters to remove.
 * \returns A subpiece with the matching trailing characters removed.
 */
template <typename ToTrim>
StringPiece rtrim(StringPiece sp, ToTrim toTrim) {
  while (!sp.empty() && toTrim(sp.back())) {
    sp.pop_back();
  }

  return sp;
}

/**
 * Specify characters to trim.
 *
 * Returns a subpiece with all characters the provided @toTrim returns true
 * for removed from the back and front of @sp.
 *
 * \param sp The input piece to trim.
 * \param toTrim Predicate selecting characters to remove.
 * \returns A subpiece with the matching leading and trailing characters
 *   removed.
 */
template <typename ToTrim>
StringPiece trim(StringPiece sp, ToTrim toTrim) {
  return ltrim(rtrim(sp, std::ref(toTrim)), std::ref(toTrim));
}

/**
 * De-indent a string.
 *
 * Strips the leading and the trailing whitespace-only lines. Then looks for
 * the least indented non-whitespace-only line and removes its amount of
 * leading whitespace from every line. Assumes leading whitespace is either all
 * spaces or all tabs.
 *
 * Purpose: including a multiline string literal in source code, indented to
 * the level expected from context.
 *
 * \param s The string to de-indent.
 * \returns The de-indented string.
 */
std::string stripLeftMargin(std::string s);

/**
 * Convert ascii to lowercase, in-place.
 *
 * Leaves all other characters unchanged, including those with the 0x80
 * bit set.
 * @param str String to convert
 * @param length Length of str, in bytes
 */
void toLowerAscii(char* str, size_t length);

/// Convert ascii to lowercase in place over a mutable string piece.
/// \param str The piece to convert.
inline void toLowerAscii(MutableStringPiece str) {
  toLowerAscii(str.begin(), str.size());
}

/// Convert ascii to lowercase in place over a string.
/// \param str The string to convert.
inline void toLowerAscii(std::string& str) {
  // str[0] is legal also if the string is empty.
  toLowerAscii(&str[0], str.size());
}

/**
 * Convert ascii to uppercase, in-place.
 *
 * Leaves all other characters unchanged, including those with the 0x80
 * bit set.
 * @param str String to convert
 * @param length Length of str, in bytes
 */
void toUpperAscii(char* str, size_t length);

/// Convert ascii to uppercase in place over a mutable string piece.
/// \param str The piece to convert.
inline void toUpperAscii(MutableStringPiece str) {
  toUpperAscii(str.begin(), str.size());
}

/// Convert ascii to uppercase in place over a string.
/// \param str The string to convert.
inline void toUpperAscii(std::string& str) {
  // str[0] is legal also if the string is empty.
  toUpperAscii(&str[0], str.size());
}

/**
 * Returns if string contains std::isspace or std::iscntrl characters.
 *
 * \param s The string to inspect.
 * \returns True if any whitespace or control character is present.
 **/
inline bool hasSpaceOrCntrlSymbols(folly::StringPiece s) {
  return detail::simdHasSpaceOrCntrlSymbols(s);
}

/// Callable that invokes a function for each named argument in a format string.
struct format_string_for_each_named_arg_fn {
  /// Options controlling how named arguments are enumerated.
  struct options {
    bool numeric_args_as_named = false; ///< Treat numeric args as named.

    /// Sets whether numeric arguments are treated as named.
    /// \param value Whether numeric arguments should be reported.
    /// \returns A reference to this options object for chaining.
    options& set_numeric_args_as_named(bool value) noexcept {
      numeric_args_as_named = value;
      return *this;
    }
  };

  /// Invokes fn for each named argument in str using default options.
  /// \param str The format string to scan.
  /// \param fn The callable invoked with each named argument.
  template <typename C, typename CT, typename Fn>
  constexpr void operator()(std::basic_string_view<C, CT> str, Fn fn) const
      noexcept(noexcept(fn(str))) {
    return operator()(options{}, str, std::ref(fn));
  }

  /// Invokes fn for each named argument in str using the given options.
  /// \param opts Options controlling enumeration.
  /// \param str The format string to scan.
  /// \param fn The callable invoked with each named argument.
  template <typename C, typename CT, typename Fn>
  constexpr void operator()(
      options const& opts, std::basic_string_view<C, CT> str, Fn fn) const
      noexcept(noexcept(fn(str))) {
    using view = std::basic_string_view<C, CT>;
    while (true) {
      auto const pos = str.find('{');
      auto const beg = pos == view::npos ? str.size() : pos + 1;
      if (beg == str.size()) {
        return; // completed
      }
      if (str[beg] == '{') {
        str = str.substr(beg + 1);
        continue; // escaped
      }
      auto const end = std::min(str.find('}', pos), str.find(':', pos));
      if (end == view::npos) {
        return; // malformed
      }
      auto const arg = str.substr(beg, end - beg);
      auto const c = arg.empty() ? 0 : arg[0];
      if (c && (opts.numeric_args_as_named || !(c >= '0' && c <= '9'))) {
        fn(arg);
      }
      str = str.substr(end);
    }
  }
};

/// Callable object enumerating the named arguments of a format string.
inline constexpr format_string_for_each_named_arg_fn
    format_string_for_each_named_arg{};

/// Options type for format_string_for_each_named_arg.
using format_string_for_each_named_arg_options =
    format_string_for_each_named_arg_fn::options;

} // namespace folly

#include <folly/String-inl.h>
