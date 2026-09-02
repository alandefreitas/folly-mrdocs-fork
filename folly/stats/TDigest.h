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
#include <limits>
#include <vector>

#include <folly/Range.h>
#include <folly/Utility.h>

/// The Folly library.
namespace folly {

/**
 * TDigests are a biased quantile estimator designed to estimate the values of
 * the quantiles of streaming data with high accuracy and low memory,
 * particularly for quantiles at the tails (p0.1, p1, p99, p99.9). See
 * https://github.com/tdunning/t-digest/blob/master/docs/t-digest-paper/histo.pdf
 * for an explanation of what the purpose of TDigests is, and how they work.
 *
 * There is a notable difference between the implementation here and the
 * implementation in the paper. In the paper, the recommended scaling function
 * for bucketing centroids is an arcsin function. The arcsin function provides
 * high accuracy for low memory, but comes at a relatively high compute cost.
 * A good choice algorithm has the following properties:
 *   - The value of the function k(0, delta) = 0, and k(1, delta) = delta.
 *     This is a requirement for any t-digest function.
 *   - The limit of the derivative of the function dk/dq at 0 is inf, and at
 *     1 is inf. This provides bias to improve accuracy at the tails.
 *   - For any q <= 0.5, dk/dq(q) = dk/dq(1-q). This ensures that the accuracy
 *     of upper and lower quantiles are equivalent.
 * As such, TDigest uses a sqrt function with these properties, which is faster
 * than arcsin. There is a small, but relatively negligible impact to accuracy
 * at the tail. In empirical tests, accuracy of the sqrt approach has been
 * adequate.
 */
class TDigest {
 public:
  /// Default maximum number of centroids retained after a merge.
  static constexpr size_t kDefaultMaxSize = 100;
  /// Recommended number of values to buffer before each merge.
  static constexpr size_t kDefaultBufferSize = 1000;

  /// A single centroid holding a mean value and its weight.
  class Centroid {
   public:
    /// Constructs a centroid with the given mean and weight.
    ///
    /// \param mean The mean value of the centroid.
    /// \param weight The weight of the centroid.
    explicit Centroid(double mean = 0.0, double weight = 1.0)
        : mean_(mean), weight_(weight) {
      assert(weight > 0);
    }

    /// Returns the mean value of this centroid.
    ///
    /// \returns The centroid mean.
    inline double mean() const { return mean_; }

    /// Returns the weight of this centroid.
    ///
    /// \returns The centroid weight.
    inline double weight() const { return weight_; }

    /**
     * Adds the sum/weight to this centroid, and returns the new sum.
     *
     * \param sum The weighted value to add.
     * \param weight The weight to add.
     * \returns The updated sum after the addition.
     */
    inline double add(double sum, double weight);

    /// Compares two centroids by their mean.
    ///
    /// \param other The centroid to compare against.
    /// \returns `true` if this centroid's mean is smaller.
    inline bool operator<(const Centroid& other) const {
      return mean() < other.mean();
    }

   private:
    double mean_;
    double weight_;
  };

  /// Reusable scratch storage for in-place merges.
  class MergeWorkingBuffer {
    friend TDigest;
    std::vector<Centroid> buf;
  };

  /// Constructs an empty digest with the given maximum centroid count.
  ///
  /// \param maxSize The maximum number of centroids to retain.
  explicit TDigest(size_t maxSize = kDefaultMaxSize) : maxSize_(maxSize) {}

  /// Constructs a digest from precomputed centroids and summary statistics.
  ///
  /// \param centroids The centroids that make up the digest.
  /// \param sum The sum of all values represented by the centroids.
  /// \param count The total weight of all values.
  /// \param max_val The maximum value represented.
  /// \param min_val The minimum value represented.
  /// \param maxSize The maximum number of centroids to retain.
  explicit TDigest(
      std::vector<Centroid> centroids,
      double sum,
      double count,
      double max_val,
      double min_val,
      size_t maxSize = kDefaultMaxSize);

  /**
   * Returns a new TDigest constructed with values merged from the current
   * digest and the given sortedValues.
   *
   * \param sortedTag Tag indicating the values are already sorted.
   * \param sortedValues The values to merge, in ascending order.
   * \returns A new digest with the values merged in.
   */
  TDigest merge(
      sorted_equivalent_t sortedTag, Range<const double*> sortedValues) const;
  /**
   * Returns a new TDigest constructed with values merged from the current
   * digest and the given unsortedValues.
   *
   * \param unsortedValues The values to merge, in any order.
   * \returns A new digest with the values merged in.
   */
  TDigest merge(Range<const double*> unsortedValues) const;

  /**
   * Returns a new TDigest constructed with values merged from the given
   * digests.
   *
   * \param digests The digests to merge.
   * \returns A new digest combining all the given digests.
   */
  static TDigest merge(Range<const TDigest*> digests);
  /// Returns a new TDigest constructed with values merged from the given
  /// digests.
  ///
  /// \param digests The digests to merge, given by pointer.
  /// \returns A new digest combining all the given digests.
  static TDigest merge(Range<const TDigest**> digests);
  /// Returns a new TDigest constructed with values merged from the two
  /// given digests.
  ///
  /// \param d1 The first digest to merge.
  /// \param d2 The second digest to merge.
  /// \returns A new digest combining `d1` and `d2`.
  static TDigest merge(const TDigest& d1, const TDigest& d2);

  /**
   * Merge in place, using the provided storage.
   *
   * \param sortedTag Tag indicating the values are already sorted.
   * \param sortedValues The values to merge, in ascending order.
   * \param workingBuffer Scratch storage reused across merges.
   */
  void merge(
      sorted_equivalent_t sortedTag,
      Range<const double*> sortedValues,
      MergeWorkingBuffer& workingBuffer);

  /**
   * Estimates the value of the given quantile.
   *
   * \param q The quantile to estimate, in the range [0, 1].
   * \returns The estimated value at quantile `q`.
   */
  double estimateQuantile(double q) const;

  /**
   * Returns the estimate of the CDF at the given input.
   * Raise an invalid_argument exception if the input is NaN or infinite.
   * Returns NaN if the centroids are emtpy.
   *
   * \param x The value at which to evaluate the cumulative distribution.
   * \returns The estimated CDF at `x`, or NaN if the digest is empty.
   */
  double estimateCdf(double x) const;

  /// Returns the mean of all values added to the digest.
  ///
  /// \returns The arithmetic mean, or zero when the digest is empty.
  double mean() const { return count_ > 0 ? sum_ / count_ : 0; }

  /// Returns the sum of all values added to the digest.
  ///
  /// \returns The accumulated sum.
  double sum() const { return sum_; }

  /// Returns the total weight of all values added to the digest.
  ///
  /// \returns The accumulated count.
  double count() const { return count_; }

  /// Returns the minimum value added to the digest.
  ///
  /// \returns The smallest value seen.
  double min() const { return min_; }

  /// Returns the maximum value added to the digest.
  ///
  /// \returns The largest value seen.
  double max() const { return max_; }

  /// Returns whether the digest holds no centroids.
  ///
  /// \returns `true` if the digest is empty, `false` otherwise.
  bool empty() const { return centroids_.empty(); }

  /// Returns the centroids that make up the digest.
  ///
  /// \returns A reference to the vector of centroids.
  const std::vector<Centroid>& getCentroids() const { return centroids_; }

  /// Returns the maximum number of centroids retained after a merge.
  ///
  /// \returns The configured maximum centroid count.
  size_t maxSize() const { return maxSize_; }

 private:
  class CentroidMerger;

  void mergeValues(
      TDigest& dst,
      Range<const double*> sortedValues,
      std::vector<Centroid>& workingBuffer) const;

  template <class T>
  static TDigest mergeImpl(Range<T> ds);

  static TDigest merge2Impl(const TDigest& d1, const TDigest& d2);

  std::vector<Centroid> centroids_;
  size_t maxSize_;
  double sum_ = 0.0;
  double count_ = 0.0;
  double max_ = std::numeric_limits<double>::quiet_NaN();
  double min_ = std::numeric_limits<double>::quiet_NaN();
};

} // namespace folly
