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

#include <folly/stats/TDigest.h>
#include <folly/stats/detail/BufferedStat.h>

/// The Folly library.
namespace folly {

/// Summary statistics and quantile estimates produced by a QuantileEstimator.
struct QuantileEstimates {
  /// The sum of all values.
  double sum;
  /// The number of values.
  double count;

  /// The estimated quantiles as a vector of {quantile, value} pairs.
  std::vector<std::pair<double, double>> quantiles;
};

/**
 * A QuantileEstimator that buffers writes for 1 second.
 */
template <typename ClockT = std::chrono::steady_clock>
class SimpleQuantileEstimator {
 public:
  /// The time point type of the clock.
  using TimePoint = typename ClockT::time_point;

  /// Construct an empty estimator.
  SimpleQuantileEstimator();

  /// Estimate the given quantiles over the buffered data.
  ///
  /// \param quantiles The quantiles to estimate, each in the range [0, 1].
  /// \param now The time at which to estimate the quantiles.
  /// \returns The estimated quantiles and summary statistics.
  QuantileEstimates estimateQuantiles(
      Range<const double*> quantiles, TimePoint now = ClockT::now());

  /// Add a value at the given time.
  ///
  /// \param value The value to add.
  /// \param now The time at which the value is added.
  void addValue(double value, TimePoint now = ClockT::now());

  /// Flush buffered values
  void flush() { bufferedDigest_.flush(); }

  /// Get point-in-time TDigest
  ///
  /// \param now The time at which to sample the digest.
  /// \returns The digest at the given time.
  TDigest getDigest(TimePoint now = ClockT::now()) {
    return bufferedDigest_.get(now);
  }

 private:
  detail::BufferedDigest<TDigest, ClockT> bufferedDigest_;
};

/**
 * A QuantileEstimator that keeps values for nWindows * windowDuration (see
 * constructor). Values are buffered for windowDuration.
 */
template <typename ClockT = std::chrono::steady_clock>
class SlidingWindowQuantileEstimator {
 public:
  /// The time point type of the clock.
  using TimePoint = typename ClockT::time_point;
  /// The duration type of the clock.
  using Duration = typename ClockT::duration;

  /// Construct an estimator keeping nWindows windows of the given duration.
  ///
  /// \param windowDuration The duration of each window.
  /// \param nWindows The number of windows to keep.
  SlidingWindowQuantileEstimator(Duration windowDuration, size_t nWindows = 60);

  /// Estimate the given quantiles over the sliding window.
  ///
  /// \param quantiles The quantiles to estimate, each in the range [0, 1].
  /// \param now The time at which to estimate the quantiles.
  /// \returns The estimated quantiles and summary statistics.
  QuantileEstimates estimateQuantiles(
      Range<const double*> quantiles, TimePoint now = ClockT::now());

  /// Add a value at the given time.
  ///
  /// \param value The value to add.
  /// \param now The time at which the value is added.
  void addValue(double value, TimePoint now = ClockT::now());

  /// Flush buffered values
  void flush() { bufferedSlidingWindow_.flush(); }

  /// Get point-in-time TDigest
  ///
  /// \param now The time at which to sample the digest.
  /// \returns The merged digest across all windows at the given time.
  TDigest getDigest(TimePoint now = ClockT::now()) {
    return TDigest::merge(bufferedSlidingWindow_.get(now));
  }

 private:
  detail::BufferedSlidingWindow<TDigest, ClockT> bufferedSlidingWindow_;
};

/**
 * Equivalent to (but more efficient than) a SimpleQuantileEstimator plus one
 * SlidingWindowQuantileEstimator for each requested window.
 */
template <typename ClockT = std::chrono::steady_clock>
class MultiSlidingWindowQuantileEstimator {
 public:
  /// The time point type of the clock.
  using TimePoint = typename ClockT::time_point;
  /// The duration type of the clock.
  using Duration = typename ClockT::duration;

  /// A window definition: its duration and the number of windows to keep.
  ///
  /// Minimum granularity is in seconds, so we can buffer at least one second.
  using WindowDef = std::pair<std::chrono::seconds, size_t>;

  /// Point-in-time TDigests for the all-time data and each tracked window.
  struct Digests {
    /// Construct from an all-time digest and per-window digests.
    ///
    /// \param at The all-time digest.
    /// \param ws The per-window digests.
    Digests(TDigest at, std::vector<TDigest> ws)
        : allTime(std::move(at)), windows(std::move(ws)) {}

    /// The all-time digest.
    TDigest allTime;
    /// The per-window digests.
    std::vector<TDigest> windows;
  };

  /// Quantile estimates for the all-time data and each tracked window.
  struct MultiQuantileEstimates {
    /// The all-time quantile estimates.
    QuantileEstimates allTime;
    /// The per-window quantile estimates.
    std::vector<QuantileEstimates> windows;
  };

  /// Construct an estimator tracking the given window definitions.
  ///
  /// \param defs The window definitions, each a duration and number of windows.
  explicit MultiSlidingWindowQuantileEstimator(Range<const WindowDef*> defs);

  /// Estimate the given quantiles for the all-time and each window.
  ///
  /// \param quantiles The quantiles to estimate, each in the range [0, 1].
  /// \param now The time at which to estimate the quantiles.
  /// \returns The all-time and per-window quantile estimates.
  MultiQuantileEstimates estimateQuantiles(
      Range<const double*> quantiles, TimePoint now = ClockT::now());

  /// Add a value at the given time.
  ///
  /// \param value The value to add.
  /// \param now The time at which the value is added.
  void addValue(double value, TimePoint now = ClockT::now());

  /// Flush buffered values
  void flush() { bufferedMultiSlidingWindow_.flush(); }

  /// Get point-in-time TDigests
  ///
  /// \param now The time at which to sample the digests.
  /// \returns The all-time and per-window digests at the given time.
  Digests getDigests(TimePoint now = ClockT::now());

 private:
  detail::BufferedMultiSlidingWindow<TDigest, ClockT>
      bufferedMultiSlidingWindow_;
};

} // namespace folly

#include <folly/stats/QuantileEstimator-inl.h>
