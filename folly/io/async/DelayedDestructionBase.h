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

#include <assert.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <folly/Portability.h>

/// Root namespace for the Folly library.
namespace folly {

/**
 * DelayedDestructionBase is a helper class to ensure objects are not deleted
 * while they still have functions executing in a higher stack frame.
 *
 * This is useful for objects that invoke callback functions, to ensure that a
 * callback does not destroy the calling object.
 *
 * Classes needing this functionality should:
 * - derive from DelayedDestructionBase directly
 * - implement onDelayedDestroy which'll be called before the object is
 *   going to be destructed
 * - create a DestructorGuard object on the stack in each public method that
 *   may invoke a callback
 *
 * DelayedDestructionBase does not perform any locking.  It is intended to be
 * used only from a single thread.
 */
class DelayedDestructionBase {
 public:
  /// Deleted copy constructor.
  DelayedDestructionBase(const DelayedDestructionBase& other) = delete;
  /// Deleted copy assignment operator.
  DelayedDestructionBase& operator=(const DelayedDestructionBase& other) =
      delete;

  /// Destroys the object.
  virtual ~DelayedDestructionBase() = default;

  /**
   * Classes should create a DestructorGuard object on the stack in any
   * function that may invoke callback functions.
   *
   * The DestructorGuard prevents the guarded class from being destroyed while
   * it exists.  Without this, the callback function could delete the guarded
   * object, causing problems when the callback function returns and the
   * guarded object's method resumes execution.
   */
  class [[nodiscard]] DestructorGuard {
   public:
    /// Constructs a guard protecting the given object.
    ///
    /// \param dd The object to protect from destruction.
    explicit DestructorGuard(DelayedDestructionBase* dd) : dd_(dd) {
      if (dd_ != nullptr) {
        ++dd_->guardCount_;
        assert(dd_->guardCount_ > 0); // check for wrapping
      }
    }

    /// Copy constructor that protects the same object.
    ///
    /// \param dg The guard to copy.
    DestructorGuard(const DestructorGuard& dg) : DestructorGuard(dg.dd_) {}

    /// Move constructor that takes over the guarded object.
    ///
    /// \param dg The guard to move from.
    DestructorGuard(DestructorGuard&& dg) noexcept
        : dd_(std::exchange(dg.dd_, nullptr)) {}

    /// Assigns from another guard.
    ///
    /// \param dg The guard to assign from.
    /// \returns A reference to this guard.
    DestructorGuard& operator=(DestructorGuard dg) noexcept {
      std::swap(dd_, dg.dd_);
      return *this;
    }

    /// Assigns a new object to protect.
    ///
    /// \param dd The object to protect from destruction.
    /// \returns A reference to this guard.
    DestructorGuard& operator=(DelayedDestructionBase* dd) {
      *this = DestructorGuard(dd);
      return *this;
    }

    /// Releases the guard, allowing a pending destruction to proceed.
    ~DestructorGuard() {
      if (dd_ != nullptr) {
        assert(dd_->guardCount_ > 0);
        --dd_->guardCount_;
        if (dd_->guardCount_ == 0) {
          dd_->onDelayedDestroy(true);
        }
      }
    }

    /// Returns the guarded object.
    ///
    /// \returns The protected object, or `nullptr` if none.
    DelayedDestructionBase* get() const { return dd_; }

    /// Returns whether the guard protects an object.
    ///
    /// \returns `true` if the guard protects a non-null object.
    explicit operator bool() const { return dd_ != nullptr; }

   private:
    DelayedDestructionBase* dd_;
  };

  /**
   * This smart pointer is a convenient way to manage a concrete
   * DelayedDestructorBase child. It can replace the equivalent raw pointer and
   * provide automatic memory management.
   */
  template <typename AliasType>
  class IntrusivePtr : private DestructorGuard {
    template <typename CopyAliasType>
    friend class IntrusivePtr;

   public:
    /// Creates a new managed object.
    ///
    /// \tparam Args The constructor argument types.
    /// \param args The arguments forwarded to the object's constructor.
    /// \returns A pointer owning the newly created object.
    template <typename... Args>
    static IntrusivePtr<AliasType> make(Args&&... args) {
      return {new AliasType(std::forward<Args>(args)...)};
    }

    /// Constructs an empty pointer.
    IntrusivePtr() = default;
    /// Copy constructor.
    ///
    /// \param other The pointer to copy.
    IntrusivePtr(const IntrusivePtr& other) = default;
    /// Move constructor.
    ///
    /// \param other The pointer to move from.
    IntrusivePtr(IntrusivePtr&& other) noexcept = default;

    /// Constructs from a pointer to a convertible type.
    ///
    /// \tparam CopyAliasType The source alias type, convertible to AliasType.
    /// \param copy The pointer to copy from.
    template <
        typename CopyAliasType,
        typename = typename std::enable_if<
            std::is_convertible<CopyAliasType*, AliasType*>::value>::type>
    IntrusivePtr(const IntrusivePtr<CopyAliasType>& copy)
        : DestructorGuard(copy) {}

    /// Moves from a pointer to a convertible type.
    ///
    /// \tparam CopyAliasType The source alias type, convertible to AliasType.
    /// \param copy The pointer to move from.
    template <
        typename CopyAliasType,
        typename = typename std::enable_if<
            std::is_convertible<CopyAliasType*, AliasType*>::value>::type>
    IntrusivePtr(IntrusivePtr<CopyAliasType>&& copy)
        : DestructorGuard(std::move(copy)) {}

    /// Constructs from a raw object pointer.
    ///
    /// \param dd The object to manage.
    explicit IntrusivePtr(AliasType* dd) : DestructorGuard(dd) {}

    // Copying from a unique_ptr is safe because if the upcast to
    // DelayedDestructionBase works, then the instance is already using
    // intrusive ref-counting.
    /// Constructs from a unique_ptr to a convertible type.
    ///
    /// \tparam CopyAliasType The source alias type, convertible to AliasType.
    /// \tparam Deleter The unique_ptr deleter type.
    /// \param copy The unique_ptr to copy the managed object from.
    template <
        typename CopyAliasType,
        typename Deleter,
        typename = typename std::enable_if<
            std::is_convertible<CopyAliasType*, AliasType*>::value>::type>
    explicit IntrusivePtr(const std::unique_ptr<CopyAliasType, Deleter>& copy)
        : DestructorGuard(copy.get()) {}

    /// Copy assignment operator.
    ///
    /// \param other The pointer to copy.
    /// \returns A reference to this pointer.
    IntrusivePtr& operator=(const IntrusivePtr& other) = default;
    /// Move assignment operator.
    ///
    /// \param other The pointer to move from.
    /// \returns A reference to this pointer.
    IntrusivePtr& operator=(IntrusivePtr&& other) noexcept = default;

    /// Assigns from a pointer to a convertible type.
    ///
    /// \tparam CopyAliasType The source alias type, convertible to AliasType.
    /// \param copy The pointer to assign from.
    /// \returns A reference to this pointer.
    template <
        typename CopyAliasType,
        typename = typename std::enable_if<
            std::is_convertible<CopyAliasType*, AliasType*>::value>::type>
    IntrusivePtr& operator=(IntrusivePtr<CopyAliasType> copy) noexcept {
      DestructorGuard::operator=(copy);
      return *this;
    }

    /// Assigns a new object to manage.
    ///
    /// \param dd The object to manage.
    /// \returns A reference to this pointer.
    IntrusivePtr& operator=(AliasType* dd) {
      DestructorGuard::operator=(dd);
      return *this;
    }

    /// Replaces the managed object.
    ///
    /// \param dd The new object to manage, or `nullptr` to release.
    void reset(AliasType* dd = nullptr) { *this = dd; }

    /// Returns the managed object.
    ///
    /// \returns The managed object, or `nullptr` if none.
    AliasType* get() const {
      return static_cast<AliasType*>(DestructorGuard::get());
    }

    /// Dereferences the managed object.
    ///
    /// \returns A reference to the managed object.
    AliasType& operator*() const { return *get(); }

    /// Accesses members of the managed object.
    ///
    /// \returns A pointer to the managed object.
    AliasType* operator->() const { return get(); }

    /// Returns whether a managed object is held.
    ///
    /// \returns `true` if a non-null object is managed.
    explicit operator bool() const { return DestructorGuard::operator bool(); }
  };

 protected:
  /// Constructs the object with no active guards.
  DelayedDestructionBase() : guardCount_(0) {}

  /**
   * Get the number of DestructorGuards currently protecting this object.
   *
   * This is primarily intended for debugging purposes, such as asserting
   * that an object has at least 1 guard.
   *
   * \returns The number of active DestructorGuards.
   */
  uint32_t getDestructorGuardCount() const { return guardCount_; }

  /**
   * Implement onDelayedDestroy in subclasses.
   * onDelayedDestroy() is invoked when the object is potentially being
   * destroyed.
   *
   * @param delayed  This parameter is true if destruction was delayed because
   *                 of a DestructorGuard object, or false if onDelayedDestroy()
   *                 is being called directly from the destructor.
   */
  virtual void onDelayedDestroy(bool delayed) = 0;

 private:
  /**
   * guardCount_ is incremented by DestructorGuard, to indicate that one of
   * the DelayedDestructionBase object's methods is currently running.
   *
   * If the destructor is called while guardCount_ is non-zero, destruction
   * will be delayed until guardCount_ drops to 0.  This allows
   * DelayedDestructionBase objects to invoke callbacks without having to worry
   * about being deleted before the callback returns.
   */
  uint32_t guardCount_;
};

/// Compares two guards for equal guarded objects.
///
/// \param left The left-hand guard.
/// \param right The right-hand guard.
/// \returns `true` if both guards protect the same object.
inline bool operator==(
    const DelayedDestructionBase::DestructorGuard& left,
    const DelayedDestructionBase::DestructorGuard& right) {
  return left.get() == right.get();
}
/// Compares two guards for different guarded objects.
///
/// \param left The left-hand guard.
/// \param right The right-hand guard.
/// \returns `true` if the guards protect different objects.
inline bool operator!=(
    const DelayedDestructionBase::DestructorGuard& left,
    const DelayedDestructionBase::DestructorGuard& right) {
  return left.get() != right.get();
}
/// Tests whether a guard protects no object.
///
/// \param left The guard to test.
/// \param null The null pointer sentinel to compare against.
/// \returns `true` if the guard protects no object.
inline bool operator==(
    const DelayedDestructionBase::DestructorGuard& left, std::nullptr_t null) {
  return left.get() == nullptr;
}
/// Tests whether a guard protects no object.
///
/// \param null The null pointer sentinel to compare against.
/// \param right The guard to test.
/// \returns `true` if the guard protects no object.
inline bool operator==(
    std::nullptr_t null, const DelayedDestructionBase::DestructorGuard& right) {
  return nullptr == right.get();
}
/// Tests whether a guard protects an object.
///
/// \param left The guard to test.
/// \param null The null pointer sentinel to compare against.
/// \returns `true` if the guard protects a non-null object.
inline bool operator!=(
    const DelayedDestructionBase::DestructorGuard& left, std::nullptr_t null) {
  return left.get() != nullptr;
}
/// Tests whether a guard protects an object.
///
/// \param null The null pointer sentinel to compare against.
/// \param right The guard to test.
/// \returns `true` if the guard protects a non-null object.
inline bool operator!=(
    std::nullptr_t null, const DelayedDestructionBase::DestructorGuard& right) {
  return nullptr != right.get();
}

/// Compares two pointers for equal managed objects.
///
/// \tparam LeftAliasType The left pointer's managed type.
/// \tparam RightAliasType The right pointer's managed type.
/// \param left The left-hand pointer.
/// \param right The right-hand pointer.
/// \returns `true` if both pointers manage the same object.
template <typename LeftAliasType, typename RightAliasType>
inline bool operator==(
    const DelayedDestructionBase::IntrusivePtr<LeftAliasType>& left,
    const DelayedDestructionBase::IntrusivePtr<RightAliasType>& right) {
  return left.get() == right.get();
}
/// Compares two pointers for different managed objects.
///
/// \tparam LeftAliasType The left pointer's managed type.
/// \tparam RightAliasType The right pointer's managed type.
/// \param left The left-hand pointer.
/// \param right The right-hand pointer.
/// \returns `true` if the pointers manage different objects.
template <typename LeftAliasType, typename RightAliasType>
inline bool operator!=(
    const DelayedDestructionBase::IntrusivePtr<LeftAliasType>& left,
    const DelayedDestructionBase::IntrusivePtr<RightAliasType>& right) {
  return left.get() != right.get();
}
/// Tests whether a pointer manages no object.
///
/// \tparam LeftAliasType The pointer's managed type.
/// \param left The pointer to test.
/// \param null The null pointer sentinel to compare against.
/// \returns `true` if the pointer manages no object.
template <typename LeftAliasType>
inline bool operator==(
    const DelayedDestructionBase::IntrusivePtr<LeftAliasType>& left,
    std::nullptr_t null) {
  return left.get() == nullptr;
}
/// Tests whether a pointer manages no object.
///
/// \tparam RightAliasType The pointer's managed type.
/// \param null The null pointer sentinel to compare against.
/// \param right The pointer to test.
/// \returns `true` if the pointer manages no object.
template <typename RightAliasType>
inline bool operator==(
    std::nullptr_t null,
    const DelayedDestructionBase::IntrusivePtr<RightAliasType>& right) {
  return nullptr == right.get();
}
/// Tests whether a pointer manages an object.
///
/// \tparam LeftAliasType The pointer's managed type.
/// \param left The pointer to test.
/// \param null The null pointer sentinel to compare against.
/// \returns `true` if the pointer manages a non-null object.
template <typename LeftAliasType>
inline bool operator!=(
    const DelayedDestructionBase::IntrusivePtr<LeftAliasType>& left,
    std::nullptr_t null) {
  return left.get() != nullptr;
}
/// Tests whether a pointer manages an object.
///
/// \tparam RightAliasType The pointer's managed type.
/// \param null The null pointer sentinel to compare against.
/// \param right The pointer to test.
/// \returns `true` if the pointer manages a non-null object.
template <typename RightAliasType>
inline bool operator!=(
    std::nullptr_t null,
    const DelayedDestructionBase::IntrusivePtr<RightAliasType>& right) {
  return nullptr != right.get();
}
} // namespace folly
