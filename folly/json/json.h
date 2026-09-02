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
// Docs: https://fburl.com/fbcref_json
//

/**
 * Serialize and deserialize folly::dynamic values as JSON.
 *
 * Basic JSON type system:
 *
 *     Value  : String | Bool | Null | Object | Array | Number
 *     String : UTF-8 sequence
 *     Object : (String, Value) pairs, with unique String keys
 *     Array  : ordered list of Values
 *     Null   : null
 *     Bool   : true | false
 *     Number : (representation unspecified)
 *
 * For more information see http://json.org or look up RFC 4627.
 *
 * If your dynamic has anything illegal with regard to this type
 * system, the serializer will throw.
 *
 * @file json.h
 */

#pragma once

#include <iosfwd>
#include <string>

#include <folly/Function.h>
#include <folly/Range.h>
#include <folly/json/dynamic.h>

/// The Folly library namespace.
namespace folly {

//////////////////////////////////////////////////////////////////////

/// JSON parsing and serialization utilities.
namespace json {

/// Specifies how to format floating-point values in serialized JSON output.
///
/// Each enum value maps to a fixed fmt format string at the printer dispatch
/// site, so there is no runtime fmt-spec parsing on the serialization hot path.
enum class FloatFormat {
  /// Shortest decimal that round-trips back to the exact same IEEE-754 double
  /// (equivalent to `fmt::format("{}", x)`).
  SHORTEST,
  /// Like SHORTEST, but always emits a decimal point and at least one
  /// fractional digit (e.g. `123` becomes `123.0`, `3e-06` becomes `3.0e-06`).
  SHORTEST_TRAILING_DOT_ZERO,
  /// Like SHORTEST, but uses single-precision rounding: the shortest decimal
  /// that round-trips to the same IEEE-754 float
  /// (equivalent to `fmt::format("{}", static_cast<float>(x))`).
  SHORTEST_SINGLE,
  /// Like SHORTEST_SINGLE, but always emits a decimal point and at least one
  /// fractional digit (e.g. `123` becomes `123.0`, `3e-06` becomes `3.0e-06`).
  SHORTEST_SINGLE_TRAILING_DOT_ZERO,
  /// Fixed-point notation with `double_num_digits` digits after the decimal
  /// point (equivalent to `fmt::format("{:.{}f}", x, double_num_digits)`).
  FIXED,
  /// General-precision notation (`%g` semantics) with `double_num_digits`
  /// significant digits, stripping trailing zeros.
  GENERAL,
};

/// Options controlling how a dynamic is serialized to and parsed from JSON.
struct serialization_opts {
  /// If true, keys in an object can be non-strings (in strict JSON, object
  /// keys must be strings); used by dynamic's operator<<.
  bool allow_non_string_keys{false};

  /// If true, integer keys are allowed irrespective of 'allow_non_string_keys'
  /// in parsing, and are converted to strings in serialization; implies
  /// validate_keys.
  bool convert_int_keys{false};

  /// If true, refuse to serialize 64-bit numbers that cannot be precisely
  /// represented by a double, throwing an exception instead.
  bool javascript_safe{false};

  /// If true, the serialized json will contain spaces and newlines to try to
  /// be minimally "pretty".
  bool pretty_formatting{false};

  /// The number of spaces to indent by when pretty_formatting is enabled.
  unsigned int pretty_formatting_indent_width{2};

  /// If true, non-ASCII utf8 characters are encoded as \uXXXX escapes.
  bool encode_non_ascii{false};

  /// Check that strings are valid utf8.
  bool validate_utf8{false};

  /// Check that keys are distinct.
  bool validate_keys{false};

  /// Allow trailing comma in lists of values / items.
  bool allow_trailing_comma{false};

  /// Sort keys of all objects before printing out, using dynamic::operator<
  /// (potentially slow; has no effect if sort_keys_by is set).
  bool sort_keys{false};

  /// Sort keys of all objects before printing out using the provided less
  /// functor (potentially slow).
  Function<bool(dynamic const&, dynamic const&) const> sort_keys_by{};

  /// Replace invalid utf8 characters with U+FFFD and continue.
  bool skip_invalid_utf8{false};

  /// True to allow NaN or INF values.
  bool allow_nan_inf{false};

  /// Floating-point formatting mode for serialization.
  FloatFormat float_format{FloatFormat::SHORTEST};

  /// Number of digits used by the FIXED and GENERAL float formats.
  unsigned int double_num_digits{0};

  /// Fall back to double when an integer-looking value is too big to fit in an
  /// int64_t (can lose precision).
  bool double_fallback{false};

  /// Do not parse numbers; store them as strings and leave conversion to the
  /// user.
  bool parse_numbers_as_strings{false};

  /// Recursion limit when parsing.
  unsigned int recursion_limit{100};

  /// Bitmap of ASCII characters to escape with unicode representations; the
  /// least significant bit of the first word is ASCII value 0 and the most
  /// significant bit of the second word is ASCII value 127.
  std::array<uint64_t, 2> extra_ascii_to_escape_bitmap{};

  /// Allow json5 string, which implies allowing trailing comma, nan, inf, etc.
  bool allow_json5_experimental{false};
};

/**
 * Create bitmap for serialization_opts.extra_ascii_to_escape_bitmap.
 *
 * Generates a bitmap with bits set for each of the ASCII characters provided
 * for use in the serialization_opts extra_ascii_to_escape_bitmap option. If any
 * characters are not valid ASCII, they are ignored.
 */
/**
 * Create bitmap for serialization_opts.extra_ascii_to_escape_bitmap.
 *
 * Generates a bitmap with bits set for each of the ASCII characters provided
 * for use in the serialization_opts extra_ascii_to_escape_bitmap option. If any
 * characters are not valid ASCII, they are ignored.
 *
 * \param chars The ASCII characters to mark for escaping.
 * \returns The bitmap encoding which ASCII characters to escape.
 */
std::array<uint64_t, 2> buildExtraAsciiToEscapeBitmap(StringPiece chars);

/**
 * Serialize dynamic to json-string, with options.
 *
 * Main JSON serialization routine taking folly::dynamic parameters.
 * For the most common use cases there are simpler functions in the
 * main folly namespace.
 *
 * \param value The dynamic value to serialize.
 * \param opts The serialization options to apply.
 * \returns The serialized JSON string.
 */
std::string serialize(dynamic const& value, serialization_opts const& opts);

/**
 * Escape a string so that it is legal to print it in JSON text.
 *
 * Append the result to out.
 *
 * \param input The string to escape.
 * \param out The string that the escaped result is appended to.
 * \param opts The serialization options to apply.
 */
void escapeString(
    StringPiece input, std::string& out, const serialization_opts& opts);

/**
 * Strip all C99-like comments (i.e. // and / * ... * /)
 *
 * \param jsonC The JSON text possibly containing comments.
 * \returns The JSON text with comments removed.
 */
std::string stripComments(StringPiece jsonC);

/// Error thrown when deserializing json (i.e. converting a string into json).
class FOLLY_EXPORT parse_error : public std::runtime_error {
 public:
  /// Inherit the std::runtime_error constructors.
  using std::runtime_error::runtime_error;
};

/// Error thrown when serializing json (i.e. converting json into a string).
class FOLLY_EXPORT print_error : public std::runtime_error {
 public:
  /// Inherit the std::runtime_error constructors.
  using std::runtime_error::runtime_error;
};

/// Source location of a parsed value (may be extended in the future to include
/// offset, column, etc.).
struct parse_location {
  /// The 0-indexed line number.
  uint32_t line{};
};

/// Source range of a parsed value (may be extended in the future to include an
/// end location).
struct parse_range {
  /// The location where the value begins.
  parse_location begin;
};

/// Parse metadata describing where a value's key and value appear in the
/// source text.
struct parse_metadata {
  /// The source range of the key.
  parse_range key_range;
  /// The source range of the value.
  parse_range value_range;
};

/// Maps each parsed dynamic to its parse metadata.
using metadata_map = std::unordered_map<dynamic const*, parse_metadata>;

} // namespace json

//////////////////////////////////////////////////////////////////////

/**
 * Parse a json blob out of a range and produce a dynamic representing it.
 *
 * \param range The JSON text to parse.
 * \param opts The serialization options controlling parsing.
 * \returns The parsed dynamic value.
 */
dynamic parseJson(StringPiece range, json::serialization_opts const& opts);

/// Parse a json blob out of a range and produce a dynamic representing it.
///
/// \param range The JSON text to parse.
/// \returns The parsed dynamic value.
dynamic parseJson(StringPiece range);

/// Parse an experimental json5 blob and produce a dynamic representing it.
///
/// \param range The json5 text to parse.
/// \returns The parsed dynamic value.
[[deprecated("This is an experimental feature. Do not use in production.")]]
dynamic parseJson5(StringPiece range);

/// Parse a json blob, recording parse metadata for each value.
///
/// \param range The JSON text to parse.
/// \param map Receives parse metadata for the parsed values.
/// \returns The parsed dynamic value.
dynamic parseJsonWithMetadata(StringPiece range, json::metadata_map* map);

/// Parse a json blob with options, recording parse metadata for each value.
///
/// \param range The JSON text to parse.
/// \param opts The serialization options controlling parsing.
/// \param map Receives parse metadata for the parsed values.
/// \returns The parsed dynamic value.
dynamic parseJsonWithMetadata(
    StringPiece range,
    json::serialization_opts const& opts,
    json::metadata_map* map);

/**
 * Serialize a dynamic into a json string.
 *
 * \param value The dynamic value to serialize.
 * \returns The serialized JSON string.
 */
std::string toJson(dynamic const& value);

/**
 * Serialize a dynamic into a json string with indentation.
 * Note that the keys of all objects will be sorted.
 *
 * \param value The dynamic value to serialize.
 * \returns The pretty-printed JSON string.
 */
std::string toPrettyJson(dynamic const& value);

/**
 * Printer for GTest.
 *
 * Uppercase name to fill GTest's API, which calls this method through ADL.
 *
 * \param value The dynamic value to print.
 * \param os The stream that the printed value is written to.
 */
void PrintTo(const dynamic& value, std::ostream* os);
//////////////////////////////////////////////////////////////////////

} // namespace folly
