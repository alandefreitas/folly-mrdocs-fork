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

#include <folly/Synchronized.h>

/* `SynchronizedPtr` is a variation on the `Synchronized` idea that's useful for
 * some cases where you want to protect a pointed-to object (or an object within
 * some pointer-like wrapper). If you would otherwise need to use
 * `Synchronized<smart_ptr<Synchronized<T>>>` consider using
 * `SynchronizedPtr<smart_ptr<T>>`as it is a bit easier to use and it works when
 * you want the `T` object at runtime to actually a subclass of `T`.
 *
 * You can access the contained `T` with `.rlock()`, and `.wlock()`, and the
 * pointer or pointer-like wrapper with `.wlockPointer()`. The corresponding
 * `with...` methods take a callback, invoke it with a `T const&`, `T&` or
 * `smart_ptr<T>&` respectively, and return the callback's result.
 */
namespace folly {
/// Holds a lock together with access to the element it protects.
template <typename LockHolder, typename Element>
struct SynchronizedPtrLockedElement {
  /// Construct from a lock holder, taking ownership of the lock.
  ///
  /// @param holder The lock holder to take ownership of.
  explicit SynchronizedPtrLockedElement(LockHolder&& holder)
      : holder_(std::move(holder)) {}

  /// Return a reference to the locked element.
  ///
  /// @return A reference to the element protected by the held lock.
  Element& operator*() const { return **holder_; }

  /// Provide member access to the locked element.
  ///
  /// @return A pointer to the element protected by the held lock.
  Element* operator->() const { return &**holder_; }

  /// Test whether the underlying pointer is non-null.
  ///
  /// @return `true` if the underlying pointer is non-null.
  explicit operator bool() const { return static_cast<bool>(*holder_); }

 private:
  LockHolder holder_;
};

/// A `Synchronized` variant that protects a pointed-to object.
template <typename PointerType, typename MutexType = SharedMutex>
class SynchronizedPtr {
  using inner_type = Synchronized<PointerType, MutexType>;
  inner_type inner_;

 public:
  /// The pointer or pointer-like type being protected.
  using pointer_type = PointerType;
  /// The type of the object the pointer refers to.
  using element_type = typename std::pointer_traits<pointer_type>::element_type;
  /// The const-qualified element type.
  using const_element_type = typename std::add_const<element_type>::type;
  /// Read-locked accessor for the const element.
  using read_locked_element = SynchronizedPtrLockedElement<
      typename inner_type::ConstLockedPtr,
      const_element_type>;
  /// Write-locked accessor for the element.
  using write_locked_element = SynchronizedPtrLockedElement<
      typename inner_type::LockedPtr,
      element_type>;
  /// Write-locked accessor for the pointer itself.
  using write_locked_pointer = typename inner_type::LockedPtr;

  /// Construct the protected pointer by forwarding arguments to it.
  ///
  /// @param args The arguments used to construct the pointer.
  template <typename... Args>
  explicit SynchronizedPtr(Args... args)
      : inner_(std::forward<Args>(args)...) {}

  /// Default-construct the protected pointer.
  SynchronizedPtr() = default;
  /// Copy-construct from another `SynchronizedPtr`.
  ///
  /// \param other The object to copy from.
  SynchronizedPtr(SynchronizedPtr const& other) = default;
  /// Move-construct from another `SynchronizedPtr`.
  ///
  /// \param other The object to move from.
  SynchronizedPtr(SynchronizedPtr&& other) = default;
  /// Copy-assign from another `SynchronizedPtr`.
  ///
  /// \param other The object to copy from.
  /// \returns A reference to this object.
  SynchronizedPtr& operator=(SynchronizedPtr const& other) = default;
  /// Move-assign from another `SynchronizedPtr`.
  ///
  /// \param other The object to move from.
  /// \returns A reference to this object.
  SynchronizedPtr& operator=(SynchronizedPtr&& other) = default;

  // Methods to provide appropriately locked and const-qualified access to the
  // element.

  /// Acquire a read lock and return const access to the element.
  ///
  /// @return A read-locked accessor to the const element.
  read_locked_element rlock() const {
    return read_locked_element(inner_.rlock());
  }

  /// Invoke a callback with a read lock held on the element.
  ///
  /// @param function The callback invoked with a `T const&`.
  /// @return The result of invoking the callback.
  template <class Function>
  auto withRLock(Function&& function) const {
    return function(*rlock());
  }

  /// Acquire a write lock and return access to the element.
  ///
  /// @return A write-locked accessor to the element.
  write_locked_element wlock() { return write_locked_element(inner_.wlock()); }

  /// Invoke a callback with a write lock held on the element.
  ///
  /// @param function The callback invoked with a `T&`.
  /// @return The result of invoking the callback.
  template <class Function>
  auto withWLock(Function&& function) {
    return function(*wlock());
  }

  // Methods to provide write-locked access to the pointer. We deliberately make
  // it difficult to get a read-locked pointer because that provides read-locked
  // non-const access to the element, and the purpose of this class is to
  // discourage that.
  /// Acquire a write lock and return access to the pointer itself.
  ///
  /// @return A write-locked accessor to the pointer.
  write_locked_pointer wlockPointer() { return inner_.wlock(); }

  /// Invoke a callback with a write lock held on the pointer.
  ///
  /// @param function The callback invoked with a `smart_ptr<T>&`.
  /// @return The result of invoking the callback.
  template <class Function>
  auto withWLockPointer(Function&& function) {
    return function(*wlockPointer());
  }
};
} // namespace folly
