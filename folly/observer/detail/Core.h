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

#include <folly/Function.h>
#include <folly/Synchronized.h>
#include <folly/futures/Future.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <vector>

/// The Folly library.
namespace folly {
/// Implementation details.
namespace observer_detail {

#define DEFINE_HAS_MEMBER_FUNC(Member)                                         \
  template <typename T, typename = std::void_t<>>                              \
  struct Has_##Member##T : std::false_type {};                                 \
  template <typename T>                                                        \
  struct Has_##Member##T<T, std::void_t<decltype(std::declval<T>().Member())>> \
      : std::true_type {};                                                     \
  template <typename T>                                                        \
  constexpr bool Has_##Member##T_v = Has_##Member##T<T>::value;

/// Detects whether a type has a getName() member function.
///
/// \implementationdefined
DEFINE_HAS_MEMBER_FUNC(getName)

class ObserverManager;

/**
 * Core stores the current version of the object held by Observer. It also keeps
 * all dependencies and dependents of the Observer.
 */
class Core : public std::enable_shared_from_this<Core> {
 public:
  /// Shared pointer to a Core.
  using Ptr = std::shared_ptr<Core>;
  /// Weak pointer to a Core.
  using WeakPtr = std::weak_ptr<Core>;

  /// Describes the creator functor and its result type.
  struct CreatorContext {
    /// type info for the creator function
    const std::type_info* typeInfo;
    /// type info for the return type of the creator function
    const std::type_info* invokeResultTypeInfo;
    /// The name of the creator functor, if available.
    std::string name;

    /// Builds a CreatorContext for the given creator functor.
    ///
    /// \param creator The creator functor to describe.
    /// \returns A context describing the creator.
    template <typename F>
    static CreatorContext create(const F& creator) {
      CreatorContext context;
      context.typeInfo = &typeid(F);
      context.invokeResultTypeInfo = &typeid(decltype(FOLLY_DECLVAL(F&&)()));
      if constexpr (Has_getNameT_v<F>) {
        context.name = creator.getName();
      }
      return context;
    }
  };
  /**
   * Blocks until creator is successfully run by ObserverManager
   *
   * \param creator Functor computing the observed object.
   * \param creatorContext Context describing the creator functor.
   * \returns A shared pointer to the newly created Core.
   */
  static Ptr create(
      folly::Function<std::shared_ptr<const void>()> creator,
      CreatorContext creatorContext);

  /**
   * View of the observed object as well as its version and created time
   */
  struct VersionedData {
    /// Clock time point type used for creation timestamps.
    using TimePoint = std::chrono::system_clock::time_point;

    /// Constructs an empty versioned view.
    VersionedData() {}

    /// Constructs a versioned view of the observed object.
    ///
    /// \param dat The observed object.
    /// \param ver The version of the observed object.
    /// \param timeC The time at which the object was created.
    VersionedData(std::shared_ptr<const void> dat, size_t ver, TimePoint timeC)
        : data(std::move(dat)), version(ver), timeCreated(timeC) {}

    /// The observed object.
    std::shared_ptr<const void> data;
    /// The version of the observed object.
    size_t version{0};
    /// The time at which the observed object was created.
    TimePoint timeCreated;
  };

  /**
   * Gets current view of the observed object.
   * This is safe to call from any thread. If this is called from other Observer
   * functor then that Observer is marked as dependent on current Observer.
   *
   * \returns The current versioned view of the observed object.
   */
  VersionedData getData();

  /**
   * Gets the version of the observed object.
   *
   * \returns The current version of the observed object.
   */
  size_t getVersion() const { return version_; }

  /**
   * Get the last version at which the observed object was actually changed.
   *
   * \returns The version of the last actual change.
   */
  size_t getVersionLastChange() { return versionLastChange_; }

  /**
   * Check if the observed object needs to be re-computed. Returns the version
   * of last change.
   *
   * This should be only called from ObserverManager thread.
   *
   * \param version The current version to check against.
   * \returns The version of the last change.
   */
  size_t refresh(size_t version);

  /**
   * Force the next call to refresh to unconditionally re-compute the observed
   * object, even if dependencies didn't change.
   */
  void setForceRefresh();

  /// Returns the context describing the creator functor.
  ///
  /// \returns A reference to the creator context.
  const CreatorContext& getCreatorContext() const { return creatorContext_; }

  /// Destroys the Core.
  ~Core();

 private:
  Core(
      folly::Function<std::shared_ptr<const void>()> creator,
      CreatorContext creatorContext);

  void addDependent(Core::WeakPtr dependent);
  void maybeRemoveStaleDependents();

  struct Dependents {
    size_t numPotentiallyExpiredDependents{0};
    std::vector<WeakPtr> deps;
  };
  using Dependencies = std::unordered_set<Ptr>;

  folly::Synchronized<Dependents> dependents_;
  folly::Synchronized<Dependencies> dependencies_;

  std::atomic<size_t> version_{0};
  std::atomic<size_t> versionLastChange_{0};

  folly::Synchronized<VersionedData> data_;

  folly::Function<std::shared_ptr<const void>()> creator_;

  CreatorContext creatorContext_;

  mutable SharedMutex refreshMutex_;

  bool forceRefresh_{false};

 public:
  /// Returns a copy of the current set of dependencies.
  ///
  /// \returns A snapshot of the dependencies.
  Dependencies getSnapshotOfDependencies() const {
    return dependencies_.copy();
  }
};
} // namespace observer_detail
} // namespace folly
