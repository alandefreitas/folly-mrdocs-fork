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
#include <type_traits>

#include <folly/Conv.h>
#include <folly/GLog.h>
#include <folly/Range.h>

/// The Folly library.
namespace folly {

/// Predefined sets of quantiles for use with QuantileHistogram.
class PredefinedQuantiles {
 public:
  /// The default set of quantiles.
  class Default {
   public:
    /// The tracked quantiles.
    static constexpr std::array<double, 11> kQuantiles{
        0.0, 0.001, 0.01, 0.1, 0.25, 0.5, 0.75, 0.9, 0.99, 0.999, 1.0};
  };

  /// Tracks only the minimum and maximum.
  class MinAndMax {
   public:
    /// The tracked quantiles.
    static constexpr std::array<double, 2> kQuantiles{0.0, 1.0};
  };

  /// Tracks the minimum, median, and maximum.
  class Median {
   public:
    /// The tracked quantiles.
    static constexpr std::array<double, 3> kQuantiles{0.0, 0.5, 1.0};
  };

  /// Tracks the minimum, 1st percentile, and maximum.
  class P01 {
   public:
    /// The tracked quantiles.
    static constexpr std::array<double, 3> kQuantiles{0.0, 0.01, 1.0};
  };

  /// Tracks the minimum, 99th percentile, and maximum.
  class P99 {
   public:
    /// The tracked quantiles.
    static constexpr std::array<double, 3> kQuantiles{0.0, 0.99, 1.0};
  };

  /// Tracks the median and higher percentiles.
  class MedianAndHigh {
   public:
    /// The tracked quantiles.
    static constexpr std::array<double, 6> kQuantiles{
        0.0, 0.5, 0.9, 0.99, 0.999, 1.0};
  };

  /// Tracks the four quartiles.
  class Quartiles {
   public:
    /// The tracked quantiles.
    static constexpr std::array<double, 5> kQuantiles{
        0.0, 0.25, 0.5, 0.75, 1.0};
  };

  /// Tracks the ten deciles.
  class Deciles {
   public:
    /// The tracked quantiles.
    static constexpr std::array<double, 11> kQuantiles{
        0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0};
  };

  /// Tracks the twenty ventiles.
  class Ventiles {
   public:
    /// The tracked quantiles.
    static constexpr std::array<double, 21> kQuantiles{
        0.0,  0.05, 0.1,  0.15, 0.2,  0.25, 0.3,  0.35, 0.4,  0.45, 0.5,
        0.55, 0.6,  0.65, 0.7,  0.75, 0.8,  0.85, 0.9,  0.95, 1.0};
  };
};

/// A histogram that tracks the locations of a fixed set of quantiles.
template <class Q = PredefinedQuantiles::Default>
class [[deprecated("Use TDigest")]] QuantileHistogram {
 public:
  /// Construct an empty histogram.
  QuantileHistogram() = default;
  /// Construct an empty histogram, ignoring the requested capacity.
  ///
  /// \param capacity The requested capacity, which is ignored.
  explicit QuantileHistogram(size_t capacity) : QuantileHistogram() {}

  /// Return the array of quantiles tracked by this histogram.
  ///
  /// \returns The tracked quantiles.
  static constexpr decltype(Q::kQuantiles) quantiles() { return Q::kQuantiles; }

  /**
   * Combines the given histograms into a new histogram where the locations for
   * each tracked quantile is the weighted average of the corresponding quantile
   * locations. The min and max for the new histogram will be the smallest min
   * or the largest max of all of the given histograms.
   *
   * \param qhists The histograms to combine.
   * \returns A new histogram combining all of the given histograms.
   */
  static QuantileHistogram<Q> merge(
      Range<const ::folly::QuantileHistogram<Q>*> qhists);

  /// Merge the given unsorted values into a copy of this histogram.
  ///
  /// \param unsortedValues The values to merge into the histogram.
  /// \returns A new histogram containing this histogram's data and the given values.
  QuantileHistogram<Q> merge(Range<const double*> unsortedValues) const;

  /// Add a single value to the histogram.
  ///
  /// \param value The value to add.
  void addValue(double value);

  /**
   * Estimates the value of the given quantile.
   *
   * \param q The quantile to estimate, in the range [0, 1].
   * \returns The estimated value at the given quantile.
   */
  double estimateQuantile(double q) const;

  /// Return the number of values added to the histogram.
  ///
  /// \returns The number of values added.
  uint64_t count() const { return count_; }

  /// Return true if no values have been added to the histogram.
  ///
  /// \returns true if the histogram is empty.
  bool empty() const { return count_ == 0; }

  /// Return the smallest value tracked by the histogram.
  ///
  /// \returns The minimum tracked value.
  double min() const { return locations_.front(); }

  /// Return the largest value tracked by the histogram.
  ///
  /// \returns The maximum tracked value.
  double max() const { return locations_.back(); }

  /// Return a human-readable representation of the histogram for debugging.
  ///
  /// \returns A string describing the histogram state.
  std::string debugString() const;

 private:
  static_assert(quantiles().size() >= 2, "Quantiles 0.0 and 1.0 are required.");
  static_assert(quantiles().front() == 0.0, "Quantile 0.0 is required.");
  static_assert(quantiles().back() == 1.0, "Quantile 1.0 is required.");

  // locations_ tracks min and max at the two ends.
  typename std::remove_const<decltype(Q::kQuantiles)>::type locations_{};
  uint64_t count_{0};

  inline size_t addValueImpl(
      double value, const decltype(Q::kQuantiles)& oldLocations);

  void dcheckSane() const;
};

} // namespace folly

#include <folly/stats/QuantileHistogram-inl.h>
