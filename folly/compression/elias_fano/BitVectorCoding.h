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

#include <cstdlib>
#include <limits>
#include <type_traits>

#include <glog/logging.h>

#include <folly/Likely.h>
#include <folly/Portability.h>
#include <folly/Range.h>
#include <folly/compression/Instructions.h>
#include <folly/compression/Select64.h>
#include <folly/compression/elias_fano/CodingDetail.h>
#include <folly/lang/Bits.h>
#include <folly/lang/BitsClass.h>

namespace folly {
namespace compression {

static_assert(kIsLittleEndian, "BitVectorCoding.h requires little endianness");

/// Non-owning view over a bit-vector-encoded list and its sub-sections.
template <class Pointer>
struct BitVectorCompressedListBase {
  /// Constructs an empty compressed list.
  BitVectorCompressedListBase() = default;

  /// Constructs a compressed list from one with a compatible pointer type.
  ///
  /// \param other The compressed list to copy pointers and sizes from.
  template <class OtherPointer>
  BitVectorCompressedListBase(
      const BitVectorCompressedListBase<OtherPointer>& other)
      : size(other.size),
        upperBound(other.upperBound),
        data(other.data),
        bits(reinterpret_cast<Pointer>(other.bits)),
        skipPointers(reinterpret_cast<Pointer>(other.skipPointers)),
        forwardPointers(reinterpret_cast<Pointer>(other.forwardPointers)) {}

  /// Frees the data buffer that was allocated for this list.
  ///
  /// \returns The result of the underlying ::free call.
  template <class T = Pointer>
  auto free() -> decltype(::free(T(nullptr))) {
    return ::free(data.data());
  }

  size_t size = 0; ///< Number of elements in the list.
  size_t upperBound = 0; ///< The largest value stored in the list.

  folly::Range<Pointer> data; ///< The backing bytes of the list.

  Pointer bits = nullptr; ///< Pointer to the bit vector section.
  Pointer skipPointers = nullptr; ///< Pointer to the skip pointers section.
  Pointer forwardPointers = nullptr; ///< Pointer to the forward pointers section.
};

/// Read-only bit-vector compressed list.
using BitVectorCompressedList = BitVectorCompressedListBase<const uint8_t*>;
/// Mutable bit-vector compressed list.
using MutableBitVectorCompressedList = BitVectorCompressedListBase<uint8_t*>;

/// Encodes a strictly increasing sequence of unsigned integers as a bit vector.
template <
    class Value,
    class SkipValue,
    size_t kSkipQuantum = 0,
    size_t kForwardQuantum = 0>
struct BitVectorEncoder {
  static_assert(
      std::is_integral<Value>::value && std::is_unsigned<Value>::value,
      "Value should be unsigned integral");

  /// Read-only view type over an encoded list.
  using CompressedList = BitVectorCompressedList;
  /// Mutable view type over an encoded list.
  using MutableCompressedList = MutableBitVectorCompressedList;

  /// The element value type.
  using ValueType = Value;
  /// The type used to store skip and forward pointers.
  using SkipValueType = SkipValue;
  struct Layout;

  /// Distance (in values) between skip pointers; 0 disables them.
  static constexpr size_t skipQuantum = kSkipQuantum;
  /// Distance (in elements) between forward pointers; 0 disables them.
  static constexpr size_t forwardQuantum = kForwardQuantum;

  /// Encodes the strictly increasing range [begin, end) into a new list.
  ///
  /// \param begin Iterator to the first element to encode.
  /// \param end Iterator past the last element to encode.
  /// \returns The compressed list holding the encoded range.
  template <class RandomAccessIterator>
  static MutableCompressedList encode(
      RandomAccessIterator begin, RandomAccessIterator end) {
    if (begin == end) {
      return MutableCompressedList();
    }
    BitVectorEncoder encoder(size_t(end - begin), *(end - 1));
    for (; begin != end; ++begin) {
      encoder.add(*begin);
    }
    return encoder.finish();
  }

  /// Constructs an encoder that writes into a caller-provided list.
  ///
  /// \param result The pre-allocated list to populate.
  explicit BitVectorEncoder(const MutableCompressedList& result)
      : bits_(result.bits),
        skipPointers_(result.skipPointers),
        forwardPointers_(result.forwardPointers),
        result_(result) {
    memset(result.data.data(), 0, result.data.size());
  }

  /// Constructs an encoder that allocates its own list for the given size.
  ///
  /// \param size The number of elements that will be added.
  /// \param upperBound The largest value that will be added.
  BitVectorEncoder(size_t size, ValueType upperBound)
      : BitVectorEncoder(
            Layout::fromUpperBoundAndSize(upperBound, size).allocList()) {}

  /// Appends the next value to the list. Values must be strictly increasing.
  ///
  /// \param value The value to append; must be > the previously added value.
  void add(ValueType value) {
    CHECK_LT(value, std::numeric_limits<ValueType>::max());
    // Also works when lastValue_ == -1.
    CHECK_GT(value + 1, lastValue_ + 1)
        << "BitVectorCoding only supports stricly monotone lists";

    auto block = bits_ + (value / 64) * sizeof(uint64_t);
    size_t inner = value % 64;
    folly::Bits<folly::Unaligned<uint64_t>>::set(
        reinterpret_cast<folly::Unaligned<uint64_t>*>(block), inner);

    if constexpr (skipQuantum != 0) {
      size_t nextSkipPointerSize = value / skipQuantum;
      while (skipPointersSize_ < nextSkipPointerSize) {
        auto pos = skipPointersSize_++;
        folly::storeUnaligned<SkipValueType>(
            skipPointers_ + pos * sizeof(SkipValueType),
            static_cast<SkipValueType>(size_));
      }
    }

    if constexpr (forwardQuantum != 0) {
      if (size_ != 0 && (size_ % forwardQuantum == 0)) {
        const auto pos = size_ / forwardQuantum - 1;
        folly::storeUnaligned<SkipValueType>(
            forwardPointers_ + pos * sizeof(SkipValueType),
            static_cast<SkipValueType>(value));
      }
    }

    lastValue_ = value;
    ++size_;
  }

  /// Finalizes encoding and returns the completed compressed list.
  ///
  /// \returns The completed compressed list.
  const MutableCompressedList& finish() const {
    CHECK_EQ(size_, result_.size);
    // TODO(ott): Relax this assumption.
    CHECK_EQ(result_.upperBound, lastValue_);
    return result_;
  }

 private:
  uint8_t* const bits_ = nullptr;
  uint8_t* const skipPointers_ = nullptr;
  uint8_t* const forwardPointers_ = nullptr;

  ValueType lastValue_ = -1;
  size_t size_ = 0;
  size_t skipPointersSize_ = 0;

  MutableCompressedList result_;
};

/// Describes the byte layout of an encoded list and builds its sub-ranges.
template <
    class Value,
    class SkipValue,
    size_t kSkipQuantum,
    size_t kForwardQuantum>
struct BitVectorEncoder<Value, SkipValue, kSkipQuantum, kForwardQuantum>::
    Layout {
  /// Computes the layout for a list with the given upper bound and size.
  ///
  /// \param upperBound The largest value that will be stored in the list.
  /// \param size Number of elements in the list.
  /// \returns The computed layout.
  static Layout fromUpperBoundAndSize(size_t upperBound, size_t size) {
    Layout layout;
    layout.size = size;
    layout.upperBound = upperBound;

    size_t bitVectorSizeInBytes = (upperBound / 8) + 1;
    layout.bits = bitVectorSizeInBytes;

    if constexpr (skipQuantum != 0) {
      size_t numSkipPointers = upperBound / skipQuantum;
      layout.skipPointers = numSkipPointers * sizeof(SkipValueType);
    }
    if constexpr (forwardQuantum != 0) {
      size_t numForwardPointers = size / forwardQuantum;
      layout.forwardPointers = numForwardPointers * sizeof(SkipValueType);
    }

    CHECK_LT(size, std::numeric_limits<SkipValueType>::max());

    return layout;
  }

  /// Returns the total size in bytes of the compressed list.
  ///
  /// \returns The combined size of the bits, skip and forward sections.
  size_t bytes() const { return bits + skipPointers + forwardPointers; }

  /// Lays out the list over the given buffer, setting each sub-range pointer.
  ///
  /// \param buf The buffer to lay the list out over; advanced as it is consumed.
  /// \returns A compressed list viewing the sub-ranges within buf.
  template <class Range>
  BitVectorCompressedListBase<typename Range::iterator> openList(
      Range& buf) const {
    BitVectorCompressedListBase<typename Range::iterator> result;
    result.size = size;
    result.upperBound = upperBound;
    result.data = buf.subpiece(0, bytes());
    auto advance = [&](size_t n) {
      auto begin = buf.data();
      buf.advance(n);
      return begin;
    };

    result.bits = advance(bits);
    result.skipPointers = advance(skipPointers);
    result.forwardPointers = advance(forwardPointers);
    CHECK_EQ(buf.data() - result.data.data(), bytes());

    return result;
  }

  /// Allocates a buffer for the list and returns a view over it.
  ///
  /// \returns A mutable compressed list backed by a newly allocated buffer.
  MutableCompressedList allocList() const {
    uint8_t* buf = nullptr;
    if (size > 0) {
      buf = static_cast<uint8_t*>(malloc(bytes() + 7));
    }
    folly::MutableByteRange bufRange(buf, bytes());
    return openList(bufRange);
  }

  size_t size = 0; ///< Number of elements in the list.
  size_t upperBound = 0; ///< The largest value stored in the list.

  // Sizes in bytes.
  size_t bits = 0; ///< Size in bytes of the bit vector.
  size_t skipPointers = 0; ///< Size in bytes of the skip pointers.
  size_t forwardPointers = 0; ///< Size in bytes of the forward pointers.
};

/// Forward/random-access reader over a bit-vector-encoded integer sequence.
template <
    class Encoder,
    class Instructions = instructions::Default,
    bool kUnchecked = false>
class BitVectorReader
    : detail::ForwardPointers<Encoder::forwardQuantum>,
      detail::SkipPointers<Encoder::skipQuantum> {
 public:
  /// The encoder type whose output this reader consumes.
  using EncoderType = Encoder;
  /// The element value type.
  using ValueType = typename Encoder::ValueType;
  /// The type used for positions and sizes.
  ///
  /// A bitvector can only be as large as its largest value.
  using SizeType = typename Encoder::ValueType;
  /// The type used to store skip and forward pointers.
  using SkipValueType = typename Encoder::SkipValueType;

  /// Constructs a reader over the given compressed list.
  ///
  /// \param list The compressed list to read from.
  explicit BitVectorReader(const typename Encoder::CompressedList& list)
      : detail::ForwardPointers<Encoder::forwardQuantum>(list.forwardPointers),
        detail::SkipPointers<Encoder::skipQuantum>(list.skipPointers),
        bits_(list.bits),
        size_(list.size),
        upperBound_(kUnchecked || list.size == 0 ? 0 : list.upperBound) {
    reset();
  }

  /// Repositions the reader before the first element.
  void reset() {
    // Pretend the bitvector is prefixed by a block of zeroes.
    block_ = 0;
    position_ = static_cast<SizeType>(-1);
    outer_ = static_cast<SizeType>(-sizeof(uint64_t));
    value_ = kInvalidValue;
  }

  /// Advances to the next element.
  ///
  /// \returns True if the reader advanced to a valid element.
  bool next() {
    if (!kUnchecked && FOLLY_UNLIKELY(position() + 1 >= size_)) {
      return setDone();
    }

    while (block_ == 0) {
      outer_ += sizeof(uint64_t);
      block_ = folly::loadUnaligned<uint64_t>(bits_ + outer_);
    }

    ++position_;
    auto inner = Instructions::ctz(block_);
    block_ = Instructions::blsr(block_);

    return setValue(inner);
  }

  /// Advances by n elements.
  ///
  /// \param n The number of elements to advance by; 0 has no effect.
  /// \returns True if the reader is positioned on a valid element afterwards.
  bool skip(SizeType n) {
    if (n == 0) {
      return valid();
    }

    if (!kUnchecked && position() + n >= size_) {
      return setDone();
    }
    // Small skip optimization.
    if (FOLLY_LIKELY(n < kLinearScanThreshold)) {
      for (size_t i = 0; i < n; ++i) {
        next();
      }
      return true;
    }

    position_ += n;

    // Use forward pointer.
    if constexpr (Encoder::forwardQuantum > 0) {
      if (n > Encoder::forwardQuantum) {
        const size_t steps = position_ / Encoder::forwardQuantum;
        const size_t dest = folly::loadUnaligned<SkipValueType>(
            this->forwardPointers_ + (steps - 1) * sizeof(SkipValueType));

        reposition(dest);
        n = position_ + 1 - steps * Encoder::forwardQuantum;
      }
    }

    size_t cnt;
    // Find necessary block.
    while ((cnt = Instructions::popcount(block_)) < n) {
      n -= cnt;
      outer_ += sizeof(uint64_t);
      block_ = folly::loadUnaligned<uint64_t>(bits_ + outer_);
    }

    // Skip to the n-th one in the block.
    DCHECK_GT(n, 0);
    auto inner = select64<Instructions>(block_, n - 1);
    block_ &= (uint64_t(-1) << inner) << 1;

    return setValue(inner);
  }

  /// Skips forward to the first element >= v.
  ///
  /// \param v The value to skip to.
  /// \returns True if an element >= v was found.
  template <bool kCanBeAtValue = true>
  bool skipTo(ValueType v) {
    // Also works when value_ == kInvalidValue.
    if (v != kInvalidValue) {
      DCHECK_GE(v + 1, value_ + 1);
    }

    if (!kUnchecked && v > upperBound_) {
      return setDone();
    } else if (kCanBeAtValue && v == value_) {
      return true;
    }

    // Small skip optimization.
    if (v - value_ < kLinearScanThreshold) {
      do {
        next();
      } while (value() < v);

      return true;
    }

    if constexpr (Encoder::skipQuantum > 0) {
      if (v - value_ > Encoder::skipQuantum) {
        size_t q = v / Encoder::skipQuantum;
        auto skipPointer = folly::loadUnaligned<SkipValueType>(
            this->skipPointers_ + (q - 1) * sizeof(SkipValueType));
        position_ = static_cast<SizeType>(skipPointer) - 1;

        reposition(q * Encoder::skipQuantum);
      }
    }

    // Find the value.
    size_t outer = v / 64 * sizeof(uint64_t);

    while (outer_ != outer) {
      position_ += Instructions::popcount(block_);
      outer_ += sizeof(uint64_t);
      block_ = folly::loadUnaligned<uint64_t>(bits_ + outer_);
      DCHECK_LE(outer_, outer);
    }

    uint64_t mask = ~((uint64_t(1) << (v % 64)) - 1);
    position_ += Instructions::popcount(block_ & ~mask) + 1;
    block_ &= mask;

    while (block_ == 0) {
      outer_ += sizeof(uint64_t);
      block_ = folly::loadUnaligned<uint64_t>(bits_ + outer_);
    }

    auto inner = Instructions::ctz(block_);
    block_ = Instructions::blsr(block_);

    setValue(inner);
    return true;
  }

  /// Returns the number of elements in the list.
  ///
  /// \returns The number of elements in the list.
  SizeType size() const { return size_; }

  /// Whether the reader is positioned on a valid element.
  ///
  /// \returns True if the reader is positioned on a valid element.
  bool valid() const {
    return position() < size(); // Also checks that position() != -1.
  }

  /// Returns the zero-based index of the current element.
  ///
  /// \returns The current position.
  SizeType position() const { return position_; }
  /// Returns the value of the current element. Requires valid().
  ///
  /// \returns The current element's value.
  ValueType value() const {
    DCHECK(valid());
    return value_;
  }

  /// Jumps to the element at position n from any reader state.
  ///
  /// \param n The absolute position to jump to.
  /// \returns True if the element at position n exists.
  bool jump(SizeType n) {
    reset();
    return skip(n + 1);
  }

  /// Jumps to the first element >= v from any reader state.
  ///
  /// \param v The value to jump to.
  /// \returns True if an element >= v was found.
  bool jumpTo(ValueType v) {
    reset();
    return skipTo(v);
  }

  /// Marks the reader as positioned past the last element.
  ///
  /// \returns Always false, for use as a return value of failed advances.
  bool setDone() {
    value_ = kInvalidValue;
    position_ = size_;
    return false;
  }

 private:
  // Must hold kInvalidValue + 1 == 0.
  constexpr static ValueType kInvalidValue = -1;

  bool setValue(size_t inner) {
    value_ = static_cast<ValueType>(8 * outer_ + inner);
    return true;
  }

  void reposition(size_t dest) {
    outer_ = dest / 64 * 8;
    // We maintain the invariant that outer_ is divisible by 8.
    block_ = folly::loadUnaligned<uint64_t>(bits_ + outer_);
    block_ &= ~((uint64_t(1) << (dest % 64)) - 1);
  }

  constexpr static size_t kLinearScanThreshold = 4;

  const uint8_t* const bits_;
  uint64_t block_;
  SizeType outer_;
  SizeType position_;
  ValueType value_;

  const SizeType size_;
  const ValueType upperBound_;
};

} // namespace compression
} // namespace folly
