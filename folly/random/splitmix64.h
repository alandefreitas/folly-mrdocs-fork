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
#include <cstdint>
#include <istream>
#include <limits>
#include <ostream>
#include <type_traits>

#include <folly/container/span.h>
#include <folly/functional/Invoke.h>
#include <folly/random/seed_seq.h>

namespace folly {

/// splitmix64
///
/// The raw SplitMix64 mixing step: advances state by the SplitMix64 increment
/// and returns the resulting mixed 64-bit value.
///
/// This is the primitive used to expand a single seed value into a larger
/// amount of uniformly-distributed state, e.g. when seeding a wider generator.
///
/// \param state The state to advance; updated in place.
/// \returns The mixed 64-bit value.
constexpr uint64_t splitmix64(uint64_t& state) noexcept {
  uint64_t z = (state += UINT64_C(0x9e3779b97f4a7c15));
  z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
  return z ^ (z >> 31);
}

/// splitmix64_engine
///
/// A standard-conforming RandomNumberEngine wrapping the SplitMix64 mixing
/// step. Its 64-bit internal state advances by a fixed increment per draw, so
/// discard() is O(1) and the full state is recoverable via operator>>.
class splitmix64_engine {
 private:
  template <typename SeedSeq>
  static constexpr bool is_seed_seq = std::
      is_invocable_v<seed_seq_generate_fn, std::span<uint64_t, 1>, SeedSeq&>;

 public:
  /// The unsigned integer type produced by the engine.
  using result_type = uint64_t;
  /// Default seed value used when none is provided.
  static constexpr result_type default_seed = 1u;

  /// Constructs the engine seeded from a scalar value.
  ///
  /// \param value The seed value.
  explicit splitmix64_engine(result_type value = default_seed) noexcept {
    seed(value);
  }

  /// Constructs the engine seeded from a seed sequence.
  ///
  /// \param seq The seed sequence.
  template <typename SeedSeq>
    requires is_seed_seq<SeedSeq>
  explicit splitmix64_engine(SeedSeq& seq) {
    seed(seq);
  }

  /// Generates the next random value.
  ///
  /// \returns The next random value.
  result_type operator()() noexcept { return splitmix64(state_); }

  /// Returns the smallest value the engine can produce.
  ///
  /// \returns The minimum result value.
  static constexpr result_type min() noexcept {
    return std::numeric_limits<result_type>::min();
  }
  /// Returns the largest value the engine can produce.
  ///
  /// \returns The maximum result value.
  static constexpr result_type max() noexcept {
    return std::numeric_limits<result_type>::max();
  }

  /// seed
  ///
  /// Resets the internal state to the given value.
  ///
  /// \param value The seed value.
  void seed(result_type value = default_seed) noexcept { state_ = value; }

  /// seed
  ///
  /// Resets the internal state with uniformly-distributed data generated from
  /// seq.
  ///
  /// \param seq The seed sequence to generate state from.
  template <typename SeedSeq>
    requires is_seed_seq<SeedSeq>
  void seed(SeedSeq& seq) {
    std::array<result_type, 1> arr;
    seed_seq_generate(std::span(arr), seq);
    state_ = arr[0];
  }

  /// discard
  ///
  /// Advances the state as if operator() had been called z times.
  ///
  /// \param z The number of draws to skip.
  void discard(unsigned long long z) noexcept {
    state_ += z * UINT64_C(0x9e3779b97f4a7c15);
  }

  /// Compares two engines for equality of state.
  ///
  /// \param a The first engine.
  /// \param b The second engine.
  /// \returns True if both engines have the same state.
  friend bool operator==(
      const splitmix64_engine& a, const splitmix64_engine& b) noexcept {
    return a.state_ == b.state_;
  }

  /// Writes the engine state to an output stream.
  ///
  /// \param os The output stream.
  /// \param e The engine to write.
  /// \returns The output stream.
  template <typename CharT, typename Traits>
  friend std::basic_ostream<CharT, Traits>& operator<<(
      std::basic_ostream<CharT, Traits>& os, const splitmix64_engine& e) {
    return os << e.state_;
  }

  /// Reads the engine state from an input stream.
  ///
  /// \param is The input stream.
  /// \param e The engine to read into.
  /// \returns The input stream.
  template <typename CharT, typename Traits>
  friend std::basic_istream<CharT, Traits>& operator>>(
      std::basic_istream<CharT, Traits>& is, splitmix64_engine& e) {
    return is >> e.state_;
  }

 private:
  result_type state_;
};

} // namespace folly
