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

#include <unordered_map>

#include <folly/CPortability.h>
#include <folly/Optional.h>
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>
#include <folly/json/dynamic.h>

/* This is an implementation of the BSER binary serialization scheme.
 * BSER was created as a binary, local-system-only representation of
 * JSON values.  It is more space efficient in its output text than JSON,
 * and cheaper to decode.
 * It has no requirement that string values be UTF-8.
 * BSER was created for use with Watchman.
 * https://facebook.github.io/watchman/docs/bser.html
 */

/// Root namespace for the Folly library.
namespace folly {
/// BSER binary serialization for dynamic values.
namespace bser {

/// Exception thrown when BSER-encoded input cannot be decoded.
class FOLLY_EXPORT BserDecodeError : public std::runtime_error {
 public:
  /// Inherit the constructors of std::runtime_error.
  using std::runtime_error::runtime_error;
};

/// Type tags used in the BSER wire format.
enum class BserType : int8_t {
  Array = 0, ///< An array of values.
  Object, ///< An object mapping string keys to values.
  String, ///< A string value.
  Int8, ///< A signed 8-bit integer.
  Int16, ///< A signed 16-bit integer.
  Int32, ///< A signed 32-bit integer.
  Int64, ///< A signed 64-bit integer.
  Real, ///< A floating-point value.
  True, ///< The boolean value true.
  False, ///< The boolean value false.
  Null, ///< A null value.
  Template, ///< A templated array of objects sharing a common key set.
  Skip, ///< A placeholder for a value omitted from a templated object.
};
/// The two-byte magic prefix that identifies a BSER stream.
extern const uint8_t kMagic[2];

/// Options controlling how a dynamic value is serialized to BSER.
struct serialization_opts {
  /// Construct serialization options with default values.
  serialization_opts();

  /// Whether to sort keys of object values before serializing them.
  ///
  /// Note that this is potentially slow and that it does not apply
  /// to templated arrays defined via defineTemplate; its keys are always
  /// emitted in the order defined by the template.
  bool sort_keys;

  /// Incremental growth size for the underlying Appender when allocating
  /// storage for the encoded output.
  size_t growth_increment;

  /// Maps a dynamic array to the object template used to encode its elements.
  ///
  /// BSER allows generating a more space efficient representation of a list of
  /// object values.  These are stored as an "object template" listing the keys
  /// of the objects ahead of the objects themselves.  The objects are then
  /// serialized without repeating the key string for each element.
  using TemplateMap = std::unordered_map<const folly::dynamic*, folly::dynamic>;
  /// Optional templates associating dynamic arrays with object templates.
  ///
  /// You should construct this map after all mutations have been
  /// performed on the dynamic instance that you intend to serialize as bser,
  /// as it captures the address of the dynamic to match at encoding time.
  folly::Optional<TemplateMap> templates;
};

/// Options controlling how a BSER stream is deserialized.
struct bser_deserialization_options {
  /// Maximum nesting depth allowed while decoding.
  size_t max_depth = 256;
};

/// Parse a BSER value from a string.
/// \param p The complete BSER-encoded data.
/// \returns The decoded dynamic value.
folly::dynamic parseBser(folly::StringPiece p);
/// Parse a BSER value from a byte range.
/// \param p The complete BSER-encoded data.
/// \returns The decoded dynamic value.
folly::dynamic parseBser(folly::ByteRange p);
/// Parse a BSER value from an IOBuf.
/// \param p The complete BSER-encoded data.
/// \returns The decoded dynamic value.
folly::dynamic parseBser(const folly::IOBuf* p);
/// Parse a BSER value from a string with deserialization options.
/// \param p The complete BSER-encoded data.
/// \param opts Options controlling deserialization.
/// \returns The decoded dynamic value.
folly::dynamic parseBser(folly::StringPiece p, bser_deserialization_options opts);
/// Parse a BSER value from a byte range with deserialization options.
/// \param p The complete BSER-encoded data.
/// \param opts Options controlling deserialization.
/// \returns The decoded dynamic value.
folly::dynamic parseBser(folly::ByteRange p, bser_deserialization_options opts);
/// Parse a BSER value from an IOBuf with deserialization options.
/// \param p The complete BSER-encoded data.
/// \param opts Options controlling deserialization.
/// \returns The decoded dynamic value.
folly::dynamic parseBser(const folly::IOBuf* p, bser_deserialization_options opts);

/// Determine how much data is needed to fully decode a BSER pdu.
///
/// When reading incrementally, it is useful to know how much data to
/// read to fully decode a BSER pdu.
/// \param buf The buffer holding the start of a BSER pdu.
/// \returns The total length in bytes of the encoded pdu.
/// \throws std::out_of_range if more data needs to be read to decode
/// the header, or a runtime_error if the header is invalid.
size_t decodePduLength(const folly::IOBuf* buf);

/// Serialize a dynamic value to a BSER-encoded string.
/// \param v The value to serialize.
/// \param opts Options controlling serialization.
/// \returns The BSER-encoded output.
folly::fbstring toBser(folly::dynamic const& v, const serialization_opts& opts);
/// Serialize a dynamic value to a BSER-encoded IOBuf.
/// \param v The value to serialize.
/// \param opts Options controlling serialization.
/// \returns The BSER-encoded output.
std::unique_ptr<folly::IOBuf> toBserIOBuf(
    folly::dynamic const& v, const serialization_opts& opts);
} // namespace bser
} // namespace folly

/* vim:ts=2:sw=2:et:
 */
