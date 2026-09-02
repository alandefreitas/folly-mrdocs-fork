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

// This is version 2 of SpookyHash, incompatible with version 1.
//
// SpookyHash: a 128-bit noncryptographic hash function
// By Bob Jenkins, public domain
//   Oct 31 2010: alpha, framework + SpookyHash::Mix appears right
//   Oct 31 2011: alpha again, Mix only good to 2^^69 but rest appears right
//   Dec 31 2011: beta, improved Mix, tested it for 2-bit deltas
//   Feb  2 2012: production, same bits as beta
//   Feb  5 2012: adjusted definitions of uint* to be more portable
//   Mar 30 2012: 3 bytes/cycle, not 4.  Alpha was 4 but wasn't thorough enough.
//   August 5 2012: SpookyV2 (different results)
//
// Up to 3 bytes/cycle for long messages.  Reasonably fast for short messages.
// All 1 or 2 bit deltas achieve avalanche within 1% bias per output bit.
//
// This was developed for and tested on 64-bit x86-compatible processors.
// It assumes the processor is little-endian.  There is a macro
// controlling whether unaligned reads are allowed (by default they are).
// This should be an equally good hash on big-endian machines, but it will
// compute different results on them than on little-endian machines.
//
// Google's CityHash has similar specs to SpookyHash, and CityHash is faster
// on new Intel boxes.  MD4 and MD5 also have similar specs, but they are orders
// of magnitude slower.  CRCs are two or more times slower, but unlike
// SpookyHash, they have nice math for combining the CRCs of pieces to form
// the CRCs of wholes.  There are also cryptographic hashes, but those are even
// slower than MD5.
//

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <folly/CPortability.h>
#include <folly/Portability.h>
#include <folly/lang/CString.h>

namespace folly {
namespace hash {

// clang-format off

/// SpookyHash V2: a 128-bit noncryptographic hash function by Bob Jenkins.
///
/// Version 2 is incompatible with version 1.
class SpookyHashV2
{
public:
    /**
     * Hash a single message in one call, producing 128-bit output.
     *
     * @param message Message to hash
     * @param length Length of the message in bytes
     * @param hash1 In/out: in seed 1, out the first 64 bits of the hash
     * @param hash2 In/out: in seed 2, out the second 64 bits of the hash
     */
    static void Hash128(
        const void *message,  // message to hash
        size_t length,        // length of message in bytes
        uint64_t *hash1,        // in/out: in seed 1, out hash value 1
        uint64_t *hash2);       // in/out: in seed 2, out hash value 2

    /**
     * Hash a single message in one call, returning 64-bit output.
     *
     * @param message Message to hash
     * @param length Length of the message in bytes
     * @param seed Seed value
     * @return The 64-bit hash of the message
     */
    static uint64_t Hash64(
        const void *message,  // message to hash
        size_t length,        // length of message in bytes
        uint64_t seed)          // seed
    {
        uint64_t hash1 = seed;
        Hash128(message, length, &hash1, &seed);
        return hash1;
    }

    /**
     * Hash a single message in one call, producing 32-bit output.
     *
     * @param message Message to hash
     * @param length Length of the message in bytes
     * @param seed Seed value
     * @return The 32-bit hash of the message
     */
    static uint32_t Hash32(
        const void *message,  // message to hash
        size_t length,        // length of message in bytes
        uint32_t seed)          // seed
    {
        uint64_t hash1 = seed, hash2 = seed;
        Hash128(message, length, &hash1, &hash2);
        return (uint32_t)hash1;
    }

    /**
     * Initialize the state of a SpookyHash computation.
     *
     * @param seed1 First 64-bit seed; any value will do, including 0
     * @param seed2 Second 64-bit seed; different seeds produce independent hashes
     */
    void Init(
        uint64_t seed1,       // any 64-bit value will do, including 0
        uint64_t seed2);      // different seeds produce independent hashes

    /**
     * Add a fragment of a message to the SpookyHash state.
     *
     * @param message Message fragment to add
     * @param length Length of the message fragment in bytes
     */
    void Update(
        const void *message,  // message fragment
        size_t length);       // length of message fragment in bytes


    /**
     * Compute the hash for the current SpookyHash state.
     *
     * This does not modify the state, so updating can continue afterward. The
     * result matches hashing all the fragments concatenated into one message.
     *
     * @param hash1 Out: first 64 bits of the hash value
     * @param hash2 Out: second 64 bits of the hash value
     */
    void Final(
        uint64_t *hash1,          // out only: first 64 bits of hash value.
        uint64_t *hash2) const;   // out only: second 64 bits of hash value.

    /**
     * Left-rotate a 64-bit value by k bits.
     *
     * @param x Value to rotate
     * @param k Number of bit positions to rotate left
     * @return The rotated value
     */
    static inline uint64_t Rot64(uint64_t x, int k)
    {
        return (x << k) | (x >> (64 - k));
    }

    /**
     * Mix the next 96-byte block into the twelve-word internal state.
     *
     * Used when the input is 96 bytes or longer; the internal state is fully
     * overwritten every 96 bytes.
     *
     * @param data Pointer to the next twelve 64-bit message words to mix in
     * @param s0 Internal state word 0 (updated in place)
     * @param s1 Internal state word 1 (updated in place)
     * @param s2 Internal state word 2 (updated in place)
     * @param s3 Internal state word 3 (updated in place)
     * @param s4 Internal state word 4 (updated in place)
     * @param s5 Internal state word 5 (updated in place)
     * @param s6 Internal state word 6 (updated in place)
     * @param s7 Internal state word 7 (updated in place)
     * @param s8 Internal state word 8 (updated in place)
     * @param s9 Internal state word 9 (updated in place)
     * @param s10 Internal state word 10 (updated in place)
     * @param s11 Internal state word 11 (updated in place)
     */
    static inline void Mix(
        const uint64_t *data,
        uint64_t &s0, uint64_t &s1, uint64_t &s2, uint64_t &s3,
        uint64_t &s4, uint64_t &s5, uint64_t &s6, uint64_t &s7,
        uint64_t &s8, uint64_t &s9, uint64_t &s10,uint64_t &s11)
    {
      auto read = [&](auto off) { return Read8(data, off); };
      s0 += read(0);   s2 ^= s10; s11 ^= s0;  s0 = Rot64(s0,11);   s11 += s1;
      s1 += read(1);   s3 ^= s11; s0 ^= s1;   s1 = Rot64(s1,32);   s0 += s2;
      s2 += read(2);   s4 ^= s0;  s1 ^= s2;   s2 = Rot64(s2,43);   s1 += s3;
      s3 += read(3);   s5 ^= s1;  s2 ^= s3;   s3 = Rot64(s3,31);   s2 += s4;
      s4 += read(4);   s6 ^= s2;  s3 ^= s4;   s4 = Rot64(s4,17);   s3 += s5;
      s5 += read(5);   s7 ^= s3;  s4 ^= s5;   s5 = Rot64(s5,28);   s4 += s6;
      s6 += read(6);   s8 ^= s4;  s5 ^= s6;   s6 = Rot64(s6,39);   s5 += s7;
      s7 += read(7);   s9 ^= s5;  s6 ^= s7;   s7 = Rot64(s7,57);   s6 += s8;
      s8 += read(8);   s10 ^= s6; s7 ^= s8;   s8 = Rot64(s8,55);   s7 += s9;
      s9 += read(9);   s11 ^= s7; s8 ^= s9;   s9 = Rot64(s9,54);   s8 += s10;
      s10 += read(10); s0 ^= s8;  s9 ^= s10;  s10 = Rot64(s10,22); s9 += s11;
      s11 += read(11); s1 ^= s9;  s10 ^= s11; s11 = Rot64(s11,46); s10 += s0;
    }

    /**
     * Mix all twelve state words so that the first two form a hash of them all.
     *
     * One pass of the final mixing; End() runs three iterations.
     *
     * @param h0 Hash state word 0 (updated in place)
     * @param h1 Hash state word 1 (updated in place)
     * @param h2 Hash state word 2 (updated in place)
     * @param h3 Hash state word 3 (updated in place)
     * @param h4 Hash state word 4 (updated in place)
     * @param h5 Hash state word 5 (updated in place)
     * @param h6 Hash state word 6 (updated in place)
     * @param h7 Hash state word 7 (updated in place)
     * @param h8 Hash state word 8 (updated in place)
     * @param h9 Hash state word 9 (updated in place)
     * @param h10 Hash state word 10 (updated in place)
     * @param h11 Hash state word 11 (updated in place)
     */
    static inline void EndPartial(
        uint64_t &h0, uint64_t &h1, uint64_t &h2, uint64_t &h3,
        uint64_t &h4, uint64_t &h5, uint64_t &h6, uint64_t &h7,
        uint64_t &h8, uint64_t &h9, uint64_t &h10,uint64_t &h11)
    {
        h11+= h1;    h2 ^= h11;   h1 = Rot64(h1,44);
        h0 += h2;    h3 ^= h0;    h2 = Rot64(h2,15);
        h1 += h3;    h4 ^= h1;    h3 = Rot64(h3,34);
        h2 += h4;    h5 ^= h2;    h4 = Rot64(h4,21);
        h3 += h5;    h6 ^= h3;    h5 = Rot64(h5,38);
        h4 += h6;    h7 ^= h4;    h6 = Rot64(h6,33);
        h5 += h7;    h8 ^= h5;    h7 = Rot64(h7,10);
        h6 += h8;    h9 ^= h6;    h8 = Rot64(h8,13);
        h7 += h9;    h10^= h7;    h9 = Rot64(h9,38);
        h8 += h10;   h11^= h8;    h10= Rot64(h10,53);
        h9 += h11;   h0 ^= h9;    h11= Rot64(h11,42);
        h10+= h0;    h1 ^= h10;   h0 = Rot64(h0,54);
    }

    /**
     * Add the last 96-byte block and finalize the twelve-word state.
     *
     * Adds the final data words, then runs EndPartial three times.
     *
     * @param data Pointer to the final twelve 64-bit message words
     * @param h0 Hash state word 0 (updated in place)
     * @param h1 Hash state word 1 (updated in place)
     * @param h2 Hash state word 2 (updated in place)
     * @param h3 Hash state word 3 (updated in place)
     * @param h4 Hash state word 4 (updated in place)
     * @param h5 Hash state word 5 (updated in place)
     * @param h6 Hash state word 6 (updated in place)
     * @param h7 Hash state word 7 (updated in place)
     * @param h8 Hash state word 8 (updated in place)
     * @param h9 Hash state word 9 (updated in place)
     * @param h10 Hash state word 10 (updated in place)
     * @param h11 Hash state word 11 (updated in place)
     */
    static inline void End(
        const uint64_t *data,
        uint64_t &h0, uint64_t &h1, uint64_t &h2, uint64_t &h3,
        uint64_t &h4, uint64_t &h5, uint64_t &h6, uint64_t &h7,
        uint64_t &h8, uint64_t &h9, uint64_t &h10,uint64_t &h11)
    {
        h0 += data[0];   h1 += data[1];   h2 += data[2];   h3 += data[3];
        h4 += data[4];   h5 += data[5];   h6 += data[6];   h7 += data[7];
        h8 += data[8];   h9 += data[9];   h10 += data[10]; h11 += data[11];
        EndPartial(h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
        EndPartial(h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
        EndPartial(h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
    }

    /**
     * Mixing step used by the short-message path over four state words.
     *
     * @param h0 Hash state word 0 (updated in place)
     * @param h1 Hash state word 1 (updated in place)
     * @param h2 Hash state word 2 (updated in place)
     * @param h3 Hash state word 3 (updated in place)
     */
    static inline void ShortMix(uint64_t &h0, uint64_t &h1,
                                uint64_t &h2, uint64_t &h3)
    {
        h2 = Rot64(h2,50);  h2 += h3;  h0 ^= h2;
        h3 = Rot64(h3,52);  h3 += h0;  h1 ^= h3;
        h0 = Rot64(h0,30);  h0 += h1;  h2 ^= h0;
        h1 = Rot64(h1,41);  h1 += h2;  h3 ^= h1;
        h2 = Rot64(h2,54);  h2 += h3;  h0 ^= h2;
        h3 = Rot64(h3,48);  h3 += h0;  h1 ^= h3;
        h0 = Rot64(h0,38);  h0 += h1;  h2 ^= h0;
        h1 = Rot64(h1,37);  h1 += h2;  h3 ^= h1;
        h2 = Rot64(h2,62);  h2 += h3;  h0 ^= h2;
        h3 = Rot64(h3,34);  h3 += h0;  h1 ^= h3;
        h0 = Rot64(h0,5);   h0 += h1;  h2 ^= h0;
        h1 = Rot64(h1,36);  h1 += h2;  h3 ^= h1;
    }

    /**
     * Final mixing of the four short-message state words into a hash.
     *
     * @param h0 Hash state word 0 (updated in place)
     * @param h1 Hash state word 1 (updated in place)
     * @param h2 Hash state word 2 (updated in place)
     * @param h3 Hash state word 3 (updated in place)
     */
    static inline void ShortEnd(uint64_t &h0, uint64_t &h1,
                                uint64_t &h2, uint64_t &h3)
    {
        h3 ^= h2;  h2 = Rot64(h2,15);  h3 += h2;
        h0 ^= h3;  h3 = Rot64(h3,52);  h0 += h3;
        h1 ^= h0;  h0 = Rot64(h0,26);  h1 += h0;
        h2 ^= h1;  h1 = Rot64(h1,51);  h2 += h1;
        h3 ^= h2;  h2 = Rot64(h2,28);  h3 += h2;
        h0 ^= h3;  h3 = Rot64(h3,9);   h0 += h3;
        h1 ^= h0;  h0 = Rot64(h0,47);  h1 += h0;
        h2 ^= h1;  h1 = Rot64(h1,54);  h2 += h1;
        h3 ^= h2;  h2 = Rot64(h2,32);  h3 += h2;
        h0 ^= h3;  h3 = Rot64(h3,25);  h0 += h3;
        h1 ^= h0;  h0 = Rot64(h0,63);  h1 += h0;
    }

private:

    //
    // Short is used for messages under 192 bytes in length
    // Short has a low startup cost, the normal mode is good for long
    // keys, the cost crossover is at about 192 bytes.  The two modes were
    // held to the same quality bar.
    //
    static void Short(
        const void *message,  // message (byte array, not necessarily aligned)
        size_t length,        // length of message (in bytes)
        uint64_t *hash1,        // in/out: in the seed, out the hash value
        uint64_t *hash2);       // in/out: in the seed, out the hash value

    //
    // Helper to read 8 consecutive bytes from a buffer
    // If the platform has unaligned access, may be called with unaligned buf
    // Otherwise, must be called only with aligned buf
    //
    FOLLY_ALWAYS_INLINE static uint64_t Read8(const uint64_t* buf, size_t off) {
      if constexpr (kHasUnalignedAccess) {
        uint64_t out;
        FOLLY_BUILTIN_MEMCPY(&out, buf + off, sizeof(out));
        return out;
      } else {
        assert(0 == reinterpret_cast<uintptr_t>(buf) % sizeof(*buf));
        return buf[off];
      }
    }

    // number of uint64_t's in internal state
    static constexpr size_t sc_numVars = 12;

    // size of the internal state
    static constexpr size_t sc_blockSize = sc_numVars*8;

    // size of buffer of unhashed data, in bytes
    static constexpr size_t sc_bufSize = 2*sc_blockSize;

    //
    // sc_const: a constant which:
    //  * is not zero
    //  * is odd
    //  * is a not-very-regular mix of 1's and 0's
    //  * does not need any other special mathematical properties
    //
    static constexpr uint64_t sc_const = 0xdeadbeefdeadbeefULL;

    uint64_t m_data[2*sc_numVars];   // unhashed data, for partial messages
    uint64_t m_state[sc_numVars];  // internal state of the hash
    size_t m_length;             // total length of the input so far
    uint8_t  m_remainder;          // length of unhashed data stashed in m_data
};

// clang-format on

} // namespace hash
} // namespace folly
