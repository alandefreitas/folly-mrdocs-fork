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

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <tuple>
#include <type_traits>

#include <folly/lang/Exception.h>

/// The Folly library.
namespace folly {

/// Robust and efficient online computation of statistics,
/// using Welford's method for variance.
/// https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Welford's_online_algorithm
template <typename SampleDataType, typename StatsType = double>
class StreamingStats final {
  // Caclulated statistic result has to be floating point type
  static_assert(std::is_floating_point_v<StatsType>);

 public:
  /// Snapshot of the accumulated statistics state.
  struct StreamingState {
    /// The number of samples accumulated.
    size_t count = 0;
    /// The mean of the samples.
    StatsType mean = 0;
    /// The sum of squared differences from the mean.
    StatsType m2 = 0;
    /// The minimum sample seen.
    SampleDataType min = std::numeric_limits<SampleDataType>::max();
    /// The maximum sample seen.
    SampleDataType max = std::numeric_limits<SampleDataType>::lowest();
  };

  /// Constructs the stats from a range of samples.
  ///
  /// \param first Iterator to the first sample.
  /// \param last Iterator past the last sample.
  template <class Iterator>
  StreamingStats(Iterator first, Iterator last) noexcept {
    add(first, last);
  }

  /// Constructs the stats from a previously captured state.
  ///
  /// \param state The state to restore.
  explicit StreamingStats(StreamingState state)
      : count_(state.count),
        mean_(state.mean),
        m2_(state.m2),
        min_(state.min),
        max_(state.max) {}

  /// Constructs an empty stats object.
  StreamingStats() = default;

  /// Destroys the stats object.
  ~StreamingStats() = default;

  /// Add sample data via iteratation
  ///
  /// \param first Iterator to the first sample.
  /// \param last Iterator past the last sample.
  template <class Iterator>
  void add(Iterator first, Iterator last) noexcept {
    for (auto it = first; it != last; ++it) {
      add(*it);
    }
  }

  /// Add a single sample
  ///
  /// \param value The sample value to add.
  void add(SampleDataType value) noexcept {
    max_ = std::max(max_, value);
    min_ = std::min(min_, value);
    ++count_;
    StatsType const delta = value - mean_;
    mean_ += delta / count_;
    StatsType const delta2 = value - mean_;
    m2_ += delta * delta2;
  }

  /// Merge with an existing StreamingStats object
  ///
  /// \param other The stats object to merge into this one.
  void merge(StreamingStats const& other) {
    if (other.count_ == 0) {
      return;
    }
    max_ = std::max(max_, other.max_);
    min_ = std::min(min_, other.min_);
    size_t const new_size = count_ + other.count_;
    StatsType const new_mean =
        (mean_ * count_ + other.mean_ * other.count_) / new_size;
    // Each cumulant must be corrected.
    //   * from: sum((x_i - mean_)²)
    //   * to:   sum((x_i - new_mean)²)
    auto delta = [&](auto const& stats) {
      return stats.count_ *
          (new_mean * (new_mean - 2 * stats.mean_) + stats.mean_ * stats.mean_);
    };
    m2_ = m2_ + delta(*this) + other.m2_ + delta(other);
    mean_ = new_mean;
    count_ = new_size;
  }

  /// Returns the number of samples accumulated.
  ///
  /// \returns The sample count.
  size_t count() const noexcept { return count_; }

  /// Returns the minimum sample seen.
  ///
  /// \returns The smallest sample value.
  SampleDataType minimum() const {
    checkMinimumDataSize(1);
    return min_;
  }

  /// Returns the maximum sample seen.
  ///
  /// \returns The largest sample value.
  SampleDataType maximum() const {
    checkMinimumDataSize(1);
    return max_;
  }

  /// Returns the mean of the samples.
  ///
  /// \returns The arithmetic mean.
  StatsType mean() const {
    checkMinimumDataSize(1);
    return mean_;
  }

  /// Returns the sum of squared differences from the mean.
  ///
  /// \returns The second central moment accumulator.
  StatsType m2() const {
    checkMinimumDataSize(1);
    return m2_;
  }

  /// Returns the population variance of the samples.
  ///
  /// \returns The population variance.
  StatsType populationVariance() const {
    checkMinimumDataSize(2);
    return var_(0);
  }

  /// Returns the sample variance of the samples.
  ///
  /// \returns The sample variance.
  StatsType sampleVariance() const {
    checkMinimumDataSize(2);
    return var_(1);
  }

  /// Returns the population standard deviation of the samples.
  ///
  /// \returns The population standard deviation.
  StatsType populationStandardDeviation() const {
    checkMinimumDataSize(2);
    return std_(0);
  }

  /// Returns the sample standard deviation of the samples.
  ///
  /// \returns The sample standard deviation.
  StatsType sampleStandardDeviation() const {
    checkMinimumDataSize(2);
    return std_(1);
  }

  /// Returns a snapshot of the accumulated state.
  ///
  /// \returns The current streaming state.
  StreamingState state() const {
    StreamingState state;
    state.count = count_;
    state.m2 = m2_;
    state.max = max_;
    state.mean = mean_;
    state.min = min_;
    return state;
  }

 private:
  void checkMinimumDataSize(size_t const minElements) const {
    if (count_ < minElements) {
      throw_exception<std::logic_error>("stats: unavailable with no samples");
    }
  }

  StatsType var_(size_t bias) const noexcept { return m2_ / (count_ - bias); }

  StatsType std_(size_t bias) const noexcept { return std::sqrt(var_(bias)); }

  size_t count_ = 0;
  StatsType mean_ = 0;
  StatsType m2_ = 0;

  SampleDataType min_ = std::numeric_limits<SampleDataType>::max();
  SampleDataType max_ = std::numeric_limits<SampleDataType>::lowest();
};

} // namespace folly
