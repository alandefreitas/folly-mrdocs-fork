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

#include <cstdint>

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/SoftRealTimeExecutor.h>
#include <folly/synchronization/ThrottledLifoSem.h>

namespace folly {

/**
 * An approximate implementation of an Earliest Deadline First executor.
 *
 * Instead of having a global priority queue, we maintain one independent queue
 * for each LLC cache, to avoid expensive cross-LLC traffic. This implies that
 * the EDF policy is only honored among tasks submitted from CPUs sharing the
 * same LLC. In practice, each LLC should have enough CPUs to make the
 * approximation good enough for most use cases.
 *
 * Within a single stripe, deadline ties are broken by submission order: of two
 * tasks with the same deadline submitted to the same stripe, the one submitted
 * first is dequeued first. There is no equivalent guarantee across stripes,
 * because tracking a global submission order would require synchronization
 * between stripes, defeating the purpose of striping.
 */
class StripedEDFThreadPoolExecutor
    : public SoftRealTimeExecutor,
      public CPUThreadPoolExecutor {
 public:
  /// Configuration options for the executor.
  struct Options {
    /// Options for the underlying throttled LIFO semaphore.
    ThrottledLifoSem::Options tlsOptions;

    /// If true, force a single stripe regardless of the CPU topology. This
    /// gives up the per-LLC scalability benefit, but in exchange the executor
    /// honors a strict global EDF policy with submission-order tie-breaking,
    /// making it equivalent to folly::EDFThreadPoolExecutor.
    bool strictOrdering = false;
  };

  /// Deadline value representing the earliest (highest priority) deadline.
  static constexpr uint64_t kEarliestDeadline = 0;

  /// Deadline value representing the latest (lowest priority) deadline.
  static constexpr uint64_t kLatestDeadline =
      std::numeric_limits<uint64_t>::max();

  /// Constructs the executor with a fixed number of threads.
  ///
  /// \param numThreads The number of worker threads.
  /// \param threadFactory The factory used to create worker threads.
  /// \param options The executor options.
  explicit StripedEDFThreadPoolExecutor(
      size_t numThreads,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("StripedEDFTP"),
      const Options& options = defaultOptions())
      : StripedEDFThreadPoolExecutor(
            {numThreads, numThreads}, std::move(threadFactory), options) {}

  /// Constructs the executor with minimum and maximum thread counts.
  ///
  /// \param numThreads The minimum and maximum number of worker threads.
  /// \param threadFactory The factory used to create worker threads.
  /// \param options The executor options.
  explicit StripedEDFThreadPoolExecutor(
      std::pair<size_t, size_t> numThreads,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("StripedEDFTP"),
      const Options& options = defaultOptions());

  /// Inherits the base class add() overloads.
  using CPUThreadPoolExecutor::add;

  /// Schedules a function to run by the given deadline.
  ///
  /// \param f The function to execute.
  /// \param deadline The deadline used to order the task.
  void add(Func f, uint64_t deadline) override;

  /// Schedules a batch of functions to run by the given deadline.
  ///
  /// \param fs The functions to execute.
  /// \param deadline The deadline used to order the tasks.
  void add(std::vector<Func> fs, uint64_t deadline) override;

  /// Rejects a priority-based submission, which is not supported.
  ///
  /// Priorities are not supported: a priority is easily confused with a
  /// deadline, so it is rejected rather than silently ignored. Use
  /// add(Func, deadline) to schedule by deadline.
  ///
  /// \param func The function that would be executed.
  /// \param priority The requested priority.
  [[noreturn]] void addWithPriority(Func func, int8_t priority) override;

 private:
  static Options defaultOptions() { return {}; }
};

} // namespace folly
