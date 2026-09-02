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

#include <chrono>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <glog/logging.h>

#include <folly/ConstexprMath.h>
#include <folly/String.h>
#include <folly/stats/BucketedTimeSeries.h>

/// The Folly library.
namespace folly {

/**
 * This class represents a timeseries which keeps several levels of data
 * granularity (similar in principle to the loads reported by the UNIX
 * 'uptime' command).  It uses several instances (one per level) of
 * BucketedTimeSeries as the underlying storage.
 *
 * This can easily be used to track sums (and thus rates or averages) over
 * several predetermined time periods, as well as all-time sums.  For example,
 * you would use to it to track query rate or response speed over the last
 * 5, 15, 30, and 60 minutes.
 *
 * The MultiLevelTimeSeries takes a list of level durations as an input; the
 * durations must be strictly increasing.  Furthermore a special level can be
 * provided with a duration of '0' -- this will be an "all-time" level.  If
 * an all-time level is provided, it MUST be the last level present.
 *
 * The class assumes that time advances forward --  you can't retroactively add
 * values for events in the past -- the 'now' argument is provided for better
 * efficiency and ease of unittesting.
 *
 * The class is not thread-safe -- use your own synchronization!
 */
template <typename VT, typename CT = LegacyStatsClock<std::chrono::seconds>>
class MultiLevelTimeSeries {
 public:
  /// The type of the values stored in the time series.
  using ValueType = VT;
  /// The clock type used for timestamps.
  using Clock = CT;
  /// The duration type of the clock.
  using Duration = typename Clock::duration;
  /// The time point type of the clock.
  using TimePoint = typename Clock::time_point;
  /// The per-level BucketedTimeSeries type used for storage.
  using Level = folly::BucketedTimeSeries<ValueType, Clock>;

  /**
   * Create a new MultiLevelTimeSeries.
   *
   * This creates a new MultiLevelTimeSeries that tracks time series data at the
   * specified time durations (level). The time series data tracked at each
   * level is then further divided by numBuckets for memory efficiency.
   *
   * The durations must be strictly increasing. Furthermore a special level can
   * be provided with a duration of '0' -- this will be an "all-time" level. If
   * an all-time level is provided, it MUST be the last level present.
   *
   * \param numBuckets The number of buckets used to track data at each level.
   * \param numLevels The number of levels to track.
   * \param levelDurations The array of durations, one per level.
   */
  explicit MultiLevelTimeSeries(
      size_t numBuckets, size_t numLevels, const Duration levelDurations[])
      : MultiLevelTimeSeries(
            numBuckets,
            folly::Range<const Duration*>(levelDurations, numLevels)) {}

  /// Create a new MultiLevelTimeSeries from a list of level durations.
  ///
  /// \param numBuckets The number of buckets used to track data at each level.
  /// \param durations The list of durations, one per level.
  explicit MultiLevelTimeSeries(
      size_t numBuckets, std::initializer_list<Duration> durations)
      : MultiLevelTimeSeries(numBuckets, folly::range(durations)) {}

  /// Create a new MultiLevelTimeSeries from a range of level durations.
  ///
  /// \param numBuckets The number of buckets used to track data at each level.
  /// \param durations The range of durations, one per level.
  explicit MultiLevelTimeSeries(
      size_t numBuckets, folly::Range<const Duration*> durations);

  /**
   * Return the number of buckets used to track time series at each level.
   *
   * \returns The number of buckets used at each level.
   */
  size_t numBuckets() const {
    // The constructor ensures that levels_ has at least one item
    return levels_[0].numBuckets();
  }

  /**
   * Return the number of levels tracked by MultiLevelTimeSeries.
   *
   * \returns The number of levels tracked.
   */
  size_t numLevels() const { return levels_.size(); }

  /**
   * Get the BucketedTimeSeries backing the specified level.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param level The level index to look up.
   * \returns The BucketedTimeSeries backing the specified level.
   */
  const Level& getLevel(size_t level) const {
    DCHECK_LT(level, levels_.size());
    return levels_[level];
  }

  /**
   * Get the highest granularity level that is still large enough to contain
   * data going back to the specified start time.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param start The start time the level must be able to contain.
   * \returns The highest granularity level covering the given start time.
   */
  const Level& getLevel(TimePoint start) const {
    for (const auto& level : levels_) {
      if (level.isAllTime()) {
        return level;
      }
      // Note that we use duration() here rather than elapsed().
      // If duration is large enough to contain the start time then this level
      // is good enough, even if elapsed() indicates that no data was recorded
      // before the specified start time.
      if (level.getLatestTime() - level.duration() <= start) {
        return level;
      }
    }
    // We should always have an all-time level, so this is never reached.
    LOG(FATAL) << "No level of timeseries covers internval" << " from "
               << start.time_since_epoch().count() << " to now";
    return levels_.back();
  }

  /**
   * Get the BucketedTimeSeries backing the specified level.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param duration The level duration to look up.
   * \returns The BucketedTimeSeries backing the level with the given duration.
   */
  const Level& getLevelByDuration(Duration duration) const {
    // since the number of levels is expected to be small (less than 5 in most
    // cases), a simple linear scan would be efficient and is intentionally
    // chosen here over other alternatives for lookup.
    for (const auto& level : levels_) {
      if (level.duration() == duration) {
        return level;
      }
    }
    throw std::out_of_range(
        folly::to<std::string>(
            "No level of duration ", duration.count(), " found"));
  }

  /**
   * Return the sum of all the data points currently tracked at this level.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param level The level index to query.
   * \returns The sum of all data points tracked at the given level.
   */
  ValueType sum(size_t level) const { return getLevel(level).sum(); }

  /**
   * Return the sum of all the data points currently tracked by this
   * level, as if `update(now)` is called first. The user
   * does NOT need to call `update(now)` separately.
   *
   * This is for providing a way for reading the timeseries without mutation.
   *
   * \param level The level index to query.
   * \param now The time to evaluate the sum at.
   * \returns The sum of all data points tracked at the given level.
   */
  ValueType sumBy(size_t level, TimePoint now) const {
    auto [total, includeCache] = totalBy(level, now);
    return constexpr_add_overflow_clamped(
        total.sum, includeCache ? cachedSum_ : 0);
  }

  /**
   * Return the average (sum / count) of all the data points currently tracked
   * at this level.
   *
   * The return type may be specified to control whether floating-point or
   * integer division should be performed.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param level The level index to query.
   * \returns The average value at the given level.
   */
  template <typename ReturnType = double>
  ReturnType avg(size_t level) const {
    return getLevel(level).template avg<ReturnType>();
  }

  /**
   * Return the average (sum/count) of all the data points currently tracked by
   * this level, as if `update(now)` is called first. The user does NOT need to
   * call `update(now)` separately.
   *
   * This is for providing a way for reading the timeseries without mutation.
   *
   * \param level The level index to query.
   * \param now The time to evaluate the average at.
   * \returns The average value at the given level.
   */
  template <typename ReturnType = double>
  ReturnType avgBy(size_t level, TimePoint now) const {
    auto [total, includeCache] = totalBy(level, now);
    return folly::detail::avgHelper<ReturnType>(
        constexpr_add_overflow_clamped(
            total.sum, includeCache ? cachedSum_ : 0),
        constexpr_add_overflow_clamped(
            total.count, includeCache ? cachedCount_ : 0));
  }

  /**
   * Return the rate (sum divided by elaspsed time) of the all data points
   * currently tracked at this level.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param level The level index to query.
   * \returns The rate at the given level.
   */
  template <typename ReturnType = double, typename Interval = Duration>
  ReturnType rate(size_t level) const {
    return getLevel(level).template rate<ReturnType, Interval>();
  }

  /**
   * Return the rate (sum divided by elapsed time) of all the data points
   * currently tracked by this level, as if `update(now)` is called first. The
   * user does NOT need to call `update(now)` separately.
   *
   * This is for providing a way for reading the timeseries without mutation.
   *
   * \param level The level index to query.
   * \param now The time to evaluate the rate at.
   * \returns The rate at the given level.
   */
  template <typename ReturnType = double, typename Interval = Duration>
  ReturnType rateBy(size_t level, TimePoint now) const {
    const auto& timeseries = getLevel(level);
    return timeseries.template rateHelper<ReturnType, Interval>(
        sumBy(level, now), elapsedBy(level, now));
  }

  /**
   * Return the number of data points currently tracked at this level.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param level The level index to query.
   * \returns The number of data points tracked at the given level.
   */
  uint64_t count(size_t level) const { return getLevel(level).count(); }

  /**
   * Return the count of all the data points currently tracked by this
   * level, as if `update(now)` is called first. The user
   * does NOT need to call `update(now)` separately.
   *
   * This is for providing a way for reading the timeseries without mutation.
   *
   * \param level The level index to query.
   * \param now The time to evaluate the count at.
   * \returns The number of data points tracked at the given level.
   */
  uint64_t countBy(size_t level, TimePoint now) const {
    auto [total, includeCache] = totalBy(level, now);
    return constexpr_add_overflow_clamped(
        total.count, includeCache ? cachedCount_ : 0);
  }

  /**
   * Return the count divided by the elapsed time tracked at this level.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param level The level index to query.
   * \returns The count divided by the elapsed time at the given level.
   */
  template <typename ReturnType = double, typename Interval = Duration>
  ReturnType countRate(size_t level) const {
    return getLevel(level).template countRate<ReturnType, Interval>();
  }

  /**
   * Return the count divided by elapsed time of all the data points currently
   * tracked by this level, as if `update(now)` is called first. The user does
   * NOT need to call `update(now)` separately.
   *
   * This is for providing a way for reading the timeseries without mutation.
   *
   * \param level The level index to query.
   * \param now The time to evaluate the count rate at.
   * \returns The count divided by the elapsed time at the given level.
   */
  template <typename ReturnType = double, typename Interval = Duration>
  ReturnType countRateBy(size_t level, TimePoint now) const {
    const auto& timeseries = getLevel(level);
    return timeseries.template rateHelper<ReturnType, Interval>(
        countBy(level, now), elapsedBy(level, now));
  }

  /**
   * Return the sum of all the data points currently tracked at this level.
   *
   * This method is identical to sum(size_t level) above but takes in the
   * duration that the user is interested in querying as the parameter.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param duration The level duration to query.
   * \returns The sum of all data points tracked at the queried level.
   */
  ValueType sum(Duration duration) const {
    return getLevelByDuration(duration).sum();
  }

  /**
   * Return the average (sum / count) of all the data points currently tracked
   * at this level.
   *
   * This method is identical to avg(size_t level) above but takes in the
   * duration that the user is interested in querying as the parameter.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param duration The level duration to query.
   * \returns The average value at the queried level.
   */
  template <typename ReturnType = double>
  ReturnType avg(Duration duration) const {
    return getLevelByDuration(duration).template avg<ReturnType>();
  }

  /**
   * Return the rate (sum divided by elaspsed time) of the all data points
   * currently tracked at this level.
   *
   * This method is identical to rate(size_t level) above but takes in the
   * duration that the user is interested in querying as the parameter.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param duration The level duration to query.
   * \returns The rate at the queried level.
   */
  template <typename ReturnType = double, typename Interval = Duration>
  ReturnType rate(Duration duration) const {
    return getLevelByDuration(duration).template rate<ReturnType, Interval>();
  }

  /**
   * Return the number of data points currently tracked at this level.
   *
   * This method is identical to count(size_t level) above but takes in the
   * duration that the user is interested in querying as the parameter.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param duration The level duration to query.
   * \returns The number of data points tracked at the queried level.
   */
  uint64_t count(Duration duration) const {
    return getLevelByDuration(duration).count();
  }

  /**
   * Return the count divided by the elapsed time tracked at this level.
   *
   * This method is identical to countRate(size_t level) above but takes in the
   * duration that the user is interested in querying as the parameter.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param duration The level duration to query.
   * \returns The count divided by the elapsed time at the queried level.
   */
  template <typename ReturnType = double, typename Interval = Duration>
  ReturnType countRate(Duration duration) const {
    return getLevelByDuration(duration)
        .template countRate<ReturnType, Interval>();
  }

  /**
   * Estimate the sum of the data points that occurred in the specified time
   * period at this level.
   *
   * The range queried is [start, end).
   * That is, start is inclusive, and end is exclusive.
   *
   * Note that data outside of the timeseries duration will no longer be
   * available for use in the estimation.  Specifying a start time earlier than
   * getEarliestTime() will not have much effect, since only data points after
   * that point in time will be counted.
   *
   * Note that the value returned is an estimate, and may not be precise.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param start The inclusive start of the time period.
   * \param end The exclusive end of the time period.
   * \returns The estimated sum of data points during the period.
   */
  ValueType sum(TimePoint start, TimePoint end) const {
    return getLevel(start).sum(start, end);
  }

  /**
   * Estimate the average value during the specified time period.
   *
   * The same caveats documented in the sum(TimePoint start, TimePoint end)
   * comments apply here as well.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param start The inclusive start of the time period.
   * \param end The exclusive end of the time period.
   * \returns The estimated average value during the period.
   */
  template <typename ReturnType = double>
  ReturnType avg(TimePoint start, TimePoint end) const {
    return getLevel(start).template avg<ReturnType>(start, end);
  }

  /**
   * Estimate the rate during the specified time period.
   *
   * The same caveats documented in the sum(TimePoint start, TimePoint end)
   * comments apply here as well.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param start The inclusive start of the time period.
   * \param end The exclusive end of the time period.
   * \returns The estimated rate during the period.
   */
  template <typename ReturnType = double, typename Interval = Duration>
  ReturnType rate(TimePoint start, TimePoint end) const {
    return getLevel(start).template rate<ReturnType, Interval>(start, end);
  }

  /**
   * Estimate the count during the specified time period.
   *
   * The same caveats documented in the sum(TimePoint start, TimePoint end)
   * comments apply here as well.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   *
   * \param start The inclusive start of the time period.
   * \param end The exclusive end of the time period.
   * \returns The estimated number of data points during the period.
   */
  uint64_t count(TimePoint start, TimePoint end) const {
    return getLevel(start).count(start, end);
  }

  /**
   * Adds the value 'val' at time 'now' to all levels.
   *
   * Data points added at the same time point is cached internally here and not
   * propagated to the underlying levels until either flush() is called or when
   * update from a different time comes.
   *
   * This function expects time to always move forwards: it cannot be used to
   * add historical data points that have occurred in the past.  If now is
   * older than the another timestamp that has already been passed to
   * addValue() or update(), now will be ignored and the latest timestamp will
   * be used.
   *
   * \param now The time to add the value at.
   * \param val The value to add.
   */
  void addValue(TimePoint now, const ValueType& val);

  /**
   * Adds the value 'val' at time 'now' to all levels.
   *
   * \param now The time to add the value at.
   * \param val The value to add.
   * \param times The number of times to add the value.
   */
  void addValue(TimePoint now, const ValueType& val, uint64_t times);

  /**
   * Adds the value 'total' at time 'now' to all levels as the sum of
   * 'nsamples' samples.
   *
   * \param now The time to add the total at.
   * \param total The sum of the samples to add.
   * \param nsamples The number of samples the total represents.
   */
  void addValueAggregated(
      TimePoint now, const ValueType& total, uint64_t nsamples);

  /**
   * Update all the levels to the specified time, doing all the necessary
   * work to rotate the buckets and remove any stale data points.
   *
   * When reading data from the timeseries, you should make sure to manually
   * call update() before accessing the data. Otherwise you may be reading
   * stale data if update() has not been called recently.
   *
   * \param now The time to update all the levels to.
   */
  void update(TimePoint now);

  /**
   * Reset all the timeseries to an empty state as if no data points have ever
   * been added to it.
   */
  void clear();

  /**
   * Flush all cached updates.
   */
  void flush();

  /**
   * Legacy APIs that accept a Duration parameters rather than TimePoint.
   *
   * These treat the Duration as relative to the clock epoch.
   * Prefer using the correct TimePoint-based APIs instead.  These APIs will
   * eventually be deprecated and removed.
   *
   * \param now The time, relative to the clock epoch, to update to.
   */
  void update(Duration now) { update(TimePoint(now)); }
  /// Add the value at the given time, relative to the clock epoch, to all levels.
  ///
  /// \param now The time, relative to the clock epoch, to add the value at.
  /// \param value The value to add.
  void addValue(Duration now, const ValueType& value) {
    addValue(TimePoint(now), value);
  }
  /// Add the value the given number of times at the given time to all levels.
  ///
  /// \param now The time, relative to the clock epoch, to add the value at.
  /// \param value The value to add.
  /// \param times The number of times to add the value.
  void addValue(Duration now, const ValueType& value, uint64_t times) {
    addValue(TimePoint(now), value, times);
  }
  /// Add an aggregated total representing the given number of samples to all levels.
  ///
  /// \param now The time, relative to the clock epoch, to add the total at.
  /// \param total The sum of the samples to add.
  /// \param nsamples The number of samples the total represents.
  void addValueAggregated(
      Duration now, const ValueType& total, uint64_t nsamples) {
    addValueAggregated(TimePoint(now), total, nsamples);
  }

 private:
  Duration computeMaxCacheDuration();

  Duration elapsedBy(size_t level, TimePoint now) const {
    const auto& timeseries = getLevel(level);
    TimePoint firstTime = now;
    if (cachedCount_ != 0) {
      firstTime = std::min(firstTime, cachedTime_);
    }
    if (!timeseries.empty()) {
      firstTime = std::min(firstTime, timeseries.firstTime());
    }
    auto latestTime = std::max({now, cachedTime_, timeseries.getLatestTime()});
    if (timeseries.isAllTime()) {
      return latestTime - firstTime + Duration(1);
    } else {
      return latestTime -
          std::max(
                 firstTime, timeseries.getEarliestTrackableTimeBy(latestTime)) +
          Duration(1);
    }
  }

  std::pair<typename Level::Bucket, bool> totalBy(
      size_t level, TimePoint now) const {
    const auto& timeseries = getLevel(level);
    if (cachedCount_ == 0) {
      return {
          timeseries.totalBy(std::max(now, timeseries.getLatestTime())), false};
    }

    // Regardless if cache value is still captured or not, we still need to
    // rotate the buckets to the max timestamps.
    auto latestTime = std::max({timeseries.getLatestTime(), now, cachedTime_});
    if (timeseries.isAllTime()) {
      return {timeseries.totalBy(latestTime), true};
    }

    // In order to decide if we should include cached value in the result or
    // not, all we need to know is if cachedTime_ is still within the trackable
    // window.
    auto earliestTime = timeseries.getEarliestTrackableTimeBy(latestTime);
    if (cachedTime_ < earliestTime) {
      // Cached value is too old, and not tracked by the timeseries any more.
      return {timeseries.totalBy(latestTime), false};
    } else {
      return {timeseries.totalBy(latestTime), true};
    }
  }

  std::vector<Level> levels_;

  // Updates within the same time interval are cached
  // They are flushed out when updates from a different time comes,
  // or flush() is called.
  TimePoint cachedTime_;
  ValueType cachedSum_;
  uint64_t cachedCount_;
  Duration cacheDuration_;
};

} // namespace folly

#include <folly/stats/MultiLevelTimeSeries-inl.h>
