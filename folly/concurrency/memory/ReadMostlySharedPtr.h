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

#include <atomic>

#include <folly/Function.h>
#include <folly/concurrency/memory/TLRefCount.h>

namespace folly {

template <typename T, typename RefCount>
class ReadMostlyMainPtr;
template <typename T, typename RefCount>
class ReadMostlyWeakPtr;
template <typename T, typename RefCount>
class ReadMostlySharedPtr;
template <typename RefCount>
class ReadMostlyMainPtrDeleter;

/// Default reference-count type used by the read-mostly smart pointers.
using DefaultRefCount = TLRefCount;

namespace detail {

template <typename RefCount = DefaultRefCount>
class ReadMostlySharedPtrCore {
 public:
  std::shared_ptr<const void> getShared() { return ptr_; }

  bool incref() { return ++count_ > 0; }

  void decref() {
    if (--count_ == 0) {
      ptr_.reset();

      decrefWeak();
    }
  }

  void increfWeak() {
    auto value = ++weakCount_;
    DCHECK_GT(value, 0);
  }

  void decrefWeak() {
    if (--weakCount_ == 0) {
      delete this;
    }
  }

  size_t useCount() const { return *count_; }

  ~ReadMostlySharedPtrCore() noexcept {
    assert(*count_ == 0);
    assert(*weakCount_ == 0);
  }

 private:
  template <typename T, typename RefCount2>
  friend class folly::ReadMostlyMainPtr;
  friend class ReadMostlyMainPtrDeleter<RefCount>;

  explicit ReadMostlySharedPtrCore(std::shared_ptr<const void> ptr)
      : ptr_(std::move(ptr)) {}

  RefCount count_;
  RefCount weakCount_;
  std::shared_ptr<const void> ptr_;
};

template <typename From, typename To>
concept ptr_convertible = std::is_convertible_v<From*, To*>;

} // namespace detail

/// Owning pointer that gives cheap, lock-free reads to shared objects.
///
/// Holds the single main reference to an object while allowing many
/// readers to obtain a ReadMostlySharedPtr without contending on a shared
/// atomic counter.
///
/// \tparam T The pointed-to object type.
/// \tparam RefCount The reference-count implementation to use.
template <typename T, typename RefCount = DefaultRefCount>
class ReadMostlyMainPtr {
 public:
  /// Constructs an empty main pointer.
  ReadMostlyMainPtr() {}

  /// Constructs a main pointer that takes ownership of an existing object.
  ///
  /// \param ptr The shared object to manage.
  explicit ReadMostlyMainPtr(std::shared_ptr<T> ptr) { reset(std::move(ptr)); }

  /// Deleted copy constructor; main pointers are not copyable.
  ///
  /// \param other The main pointer to copy from.
  ReadMostlyMainPtr(const ReadMostlyMainPtr& other) = delete;
  /// Deleted copy assignment; main pointers are not copyable.
  ///
  /// \param other The main pointer to copy from.
  /// \returns A reference to this pointer.
  ReadMostlyMainPtr& operator=(const ReadMostlyMainPtr& other) = delete;

  /// Move-constructs from another main pointer, leaving it empty.
  ///
  /// \param other The main pointer to move from.
  ReadMostlyMainPtr(ReadMostlyMainPtr&& other) noexcept {
    *this = std::move(other);
  }

  /// Move-assigns from another main pointer by swapping state.
  ///
  /// \param other The main pointer to move from.
  /// \returns A reference to this main pointer.
  ReadMostlyMainPtr& operator=(ReadMostlyMainPtr&& other) noexcept {
    std::swap(impl_, other.impl_);
    std::swap(ptrRaw_, other.ptrRaw_);
    return *this;
  }

  /// Compares against another main pointer for pointer equality.
  ///
  /// \param other The main pointer to compare with.
  /// \returns `true` if both refer to the same object.
  bool operator==(const ReadMostlyMainPtr<T, RefCount>& other) const {
    return get() == other.get();
  }

  /// Compares against a raw pointer for equality.
  ///
  /// \param other The raw pointer to compare with.
  /// \returns `true` if this pointer refers to `other`.
  bool operator==(T* other) const { return get() == other; }

  /// Compares against a shared pointer for pointer equality.
  ///
  /// \param other The shared pointer to compare with.
  /// \returns `true` if both refer to the same object.
  bool operator==(const ReadMostlySharedPtr<T, RefCount>& other) const {
    return get() == other.get();
  }

  /// Destroys the main pointer and releases its object.
  ~ReadMostlyMainPtr() noexcept { reset(); }

  /// Releases the managed object, making this pointer empty.
  void reset() noexcept {
    if (impl_) {
      ptrRaw_ = nullptr;
      impl_->count_.useGlobal();
      impl_->weakCount_.useGlobal();
      impl_->decref();
      impl_ = nullptr;
    }
  }

  /// Replaces the managed object with a new one.
  ///
  /// \param ptr The shared object to take ownership of.
  void reset(std::shared_ptr<T> ptr) {
    reset();
    if (ptr) {
      ptrRaw_ = ptr.get();
      impl_ = new detail::ReadMostlySharedPtrCore<RefCount>(std::move(ptr));
    }
  }

  /// Returns the raw pointer to the managed object.
  ///
  /// \returns The raw pointer, or `nullptr` if empty.
  T* get() const { return ptrRaw_; }

  /// Returns a standard shared pointer aliasing the managed object.
  ///
  /// \returns A `std::shared_ptr` to the object, or empty if this is empty.
  std::shared_ptr<T> getStdShared() const {
    if (impl_) {
      return {impl_->getShared(), ptrRaw_};
    } else {
      return {};
    }
  }

  /// Dereferences the managed object.
  ///
  /// \returns A reference to the managed object.
  T& operator*() const { return *get(); }

  /// Accesses members of the managed object.
  ///
  /// \returns The raw pointer to the managed object.
  T* operator->() const { return get(); }

  /// Obtains a read-mostly shared pointer to the managed object.
  ///
  /// \returns A ReadMostlySharedPtr referring to the same object.
  ReadMostlySharedPtr<T, RefCount> getShared() const {
    return ReadMostlySharedPtr<T, RefCount>(*this);
  }

  /// Tests whether the pointer manages an object.
  ///
  /// \returns `true` if a object is managed.
  explicit operator bool() const { return impl_ != nullptr; }

 private:
  template <typename U, typename RefCount2>
  friend class ReadMostlyWeakPtr;
  template <typename U, typename RefCount2>
  friend class ReadMostlySharedPtr;
  friend class ReadMostlyMainPtrDeleter<RefCount>;

  detail::ReadMostlySharedPtrCore<RefCount>* impl_{nullptr};
  T* ptrRaw_{nullptr};
};

/// Non-owning weak reference to a read-mostly managed object.
///
/// Keeps the control block alive without keeping the object alive, and can
/// be promoted to a ReadMostlySharedPtr via lock() while the object exists.
///
/// \tparam T The pointed-to object type.
/// \tparam RefCount The reference-count implementation to use.
template <typename T, typename RefCount = DefaultRefCount>
class ReadMostlyWeakPtr {
 public:
  /// Constructs an empty weak pointer.
  ReadMostlyWeakPtr() {}

  /// Copy-constructs a weak reference to the same object.
  ///
  /// \param other The weak pointer to copy.
  ReadMostlyWeakPtr(const ReadMostlyWeakPtr& other) { *this = other; }

  /// Move-constructs from another weak pointer, leaving it empty.
  ///
  /// \param other The weak pointer to move from.
  ReadMostlyWeakPtr(ReadMostlyWeakPtr&& other) noexcept {
    *this = std::move(other);
  }

  /// Copy-constructs from a weak pointer to a convertible type.
  ///
  /// \tparam T2 The source pointee type, convertible to `T`.
  /// \param other The weak pointer to copy.
  template <typename T2>
    requires detail::ptr_convertible<T2, T>
  ReadMostlyWeakPtr(const ReadMostlyWeakPtr<T2, RefCount>& other) {
    *this = other;
  }

  /// Move-constructs from a weak pointer to a convertible type.
  ///
  /// \tparam T2 The source pointee type, convertible to `T`.
  /// \param other The weak pointer to move from.
  template <typename T2>
    requires detail::ptr_convertible<T2, T>
  ReadMostlyWeakPtr(ReadMostlyWeakPtr<T2, RefCount>&& other) noexcept {
    *this = std::move(other);
  }

  /// Constructs a weak reference from a main pointer of a convertible type.
  ///
  /// \tparam T2 The source pointee type, convertible to `T`.
  /// \param other The main pointer to reference.
  template <typename T2>
    requires detail::ptr_convertible<T2, T>
  explicit ReadMostlyWeakPtr(const ReadMostlyMainPtr<T2, RefCount>& other) {
    *this = other;
  }

  /// Constructs a weak reference from a shared pointer of a convertible type.
  ///
  /// \tparam T2 The source pointee type, convertible to `T`.
  /// \param other The shared pointer to reference.
  template <typename T2>
    requires detail::ptr_convertible<T2, T>
  explicit ReadMostlyWeakPtr(const ReadMostlySharedPtr<T2, RefCount>& other) {
    *this = other;
  }

  /// Copy-assigns a weak reference to the same object.
  ///
  /// \param other The weak pointer to copy.
  /// \returns A reference to this weak pointer.
  ReadMostlyWeakPtr& operator=(const ReadMostlyWeakPtr& other) {
    reset(other.impl_, other.ptrRaw_);
    return *this;
  }

  /// Move-assigns from another weak pointer by swapping state.
  ///
  /// \param other The weak pointer to move from.
  /// \returns A reference to this weak pointer.
  ReadMostlyWeakPtr& operator=(ReadMostlyWeakPtr&& other) noexcept {
    std::swap(impl_, other.impl_);
    std::swap(ptrRaw_, other.ptrRaw_);
    return *this;
  }

  /// Copy-assigns from a weak pointer to a convertible type.
  ///
  /// \tparam T2 The source pointee type, convertible to `T`.
  /// \param other The weak pointer to copy.
  /// \returns A reference to this weak pointer.
  template <typename T2>
    requires detail::ptr_convertible<T2, T>
  ReadMostlyWeakPtr& operator=(const ReadMostlyWeakPtr<T2, RefCount>& other) {
    reset(other.impl_, other.ptrRaw_);
    return *this;
  }

  /// Move-assigns from a weak pointer to a convertible type.
  ///
  /// \tparam T2 The source pointee type, convertible to `T`.
  /// \param other The weak pointer to move from.
  /// \returns A reference to this weak pointer.
  template <typename T2>
    requires detail::ptr_convertible<T2, T>
  ReadMostlyWeakPtr& operator=(
      ReadMostlyWeakPtr<T2, RefCount>&& other) noexcept {
    reset();
    impl_ = std::exchange(other.impl_, nullptr);
    ptrRaw_ = std::exchange(other.ptrRaw_, nullptr);
    return *this;
  }

  /// Assigns a weak reference from a main pointer of a convertible type.
  ///
  /// \tparam T2 The source pointee type, convertible to `T`.
  /// \param mainPtr The main pointer to reference.
  /// \returns A reference to this weak pointer.
  template <typename T2>
    requires detail::ptr_convertible<T2, T>
  ReadMostlyWeakPtr& operator=(const ReadMostlyMainPtr<T2, RefCount>& mainPtr) {
    reset(mainPtr.impl_, mainPtr.ptrRaw_);
    return *this;
  }

  /// Assigns a weak reference from a shared pointer of a convertible type.
  ///
  /// \tparam T2 The source pointee type, convertible to `T`.
  /// \param mainPtr The shared pointer to reference.
  /// \returns A reference to this weak pointer.
  template <typename T2>
    requires detail::ptr_convertible<T2, T>
  ReadMostlyWeakPtr& operator=(
      const ReadMostlySharedPtr<T2, RefCount>& mainPtr) {
    reset(mainPtr.impl_, mainPtr.ptrRaw_);
    return *this;
  }

  /// Destroys the weak pointer and releases its weak reference.
  ~ReadMostlyWeakPtr() noexcept { reset(nullptr, nullptr); }

  /// Promotes the weak reference to a shared pointer.
  ///
  /// \returns A ReadMostlySharedPtr to the object, or empty if it expired.
  ReadMostlySharedPtr<T, RefCount> lock() {
    return ReadMostlySharedPtr<T, RefCount>(*this);
  }

 private:
  template <typename U, typename RefCount2>
  friend class ReadMostlyWeakPtr;
  template <typename U, typename RefCount2>
  friend class ReadMostlySharedPtr;

  void reset(detail::ReadMostlySharedPtrCore<RefCount>* impl, T* ptrRaw) {
    if (impl_ == impl) {
      return;
    }

    if (impl_) {
      impl_->decrefWeak();
    }
    impl_ = impl;
    ptrRaw_ = ptrRaw;
    if (impl_) {
      impl_->increfWeak();
    }
  }

  detail::ReadMostlySharedPtrCore<RefCount>* impl_{nullptr};
  T* ptrRaw_{nullptr};
};

/// Shared reader handle to a read-mostly managed object.
///
/// A cheaply copyable pointer that keeps the object alive while it is held.
/// It is typically obtained from a ReadMostlyMainPtr or ReadMostlyWeakPtr and
/// is meant for the read-mostly access path.
///
/// \tparam T The pointed-to object type.
/// \tparam RefCount The reference-count implementation to use.
template <typename T, typename RefCount = DefaultRefCount>
class ReadMostlySharedPtr {
 public:
  /// Constructs an empty shared pointer.
  ReadMostlySharedPtr() {}

  /// Copy-constructs a shared reference to the same object.
  ///
  /// \param other The shared pointer to copy.
  ReadMostlySharedPtr(const ReadMostlySharedPtr& other) { *this = other; }

  /// Move-constructs from another shared pointer, leaving it empty.
  ///
  /// \param other The shared pointer to move from.
  ReadMostlySharedPtr(ReadMostlySharedPtr&& other) noexcept {
    *this = std::move(other);
  }

  /// Copy-constructs from a shared pointer to a convertible type.
  ///
  /// \tparam T2 The source pointee type, convertible to `T`.
  /// \param other The shared pointer to copy.
  template <typename T2>
    requires detail::ptr_convertible<T2, T>
  ReadMostlySharedPtr(const ReadMostlySharedPtr<T2, RefCount>& other) {
    *this = other;
  }

  /// Move-constructs from a shared pointer to a convertible type.
  ///
  /// \tparam T2 The source pointee type, convertible to `T`.
  /// \param other The shared pointer to move from.
  template <typename T2>
    requires detail::ptr_convertible<T2, T>
  ReadMostlySharedPtr(ReadMostlySharedPtr<T2, RefCount>&& other) noexcept {
    *this = std::move(other);
  }

  /// Constructs a shared pointer by locking a weak pointer.
  ///
  /// \tparam T2 The source pointee type, convertible to `T`.
  /// \param other The weak pointer to promote.
  template <typename T2>
    requires detail::ptr_convertible<T2, T>
  explicit ReadMostlySharedPtr(const ReadMostlyWeakPtr<T2, RefCount>& other) {
    *this = other;
  }

  /// Constructs a shared pointer from a main pointer.
  ///
  /// Generally, this shouldn't be used.
  ///
  /// \tparam T2 The source pointee type, convertible to `T`.
  /// \param other The main pointer to reference.
  template <typename T2>
    requires detail::ptr_convertible<T2, T>
  explicit ReadMostlySharedPtr(const ReadMostlyMainPtr<T2, RefCount>& other) {
    *this = other;
  }

  /// Copy-assigns a shared reference to the same object.
  ///
  /// \param other The shared pointer to copy.
  /// \returns A reference to this shared pointer.
  ReadMostlySharedPtr& operator=(const ReadMostlySharedPtr& other) {
    reset(other.impl_, other.ptrRaw_);
    return *this;
  }

  /// Move-assigns from another shared pointer by swapping state.
  ///
  /// \param other The shared pointer to move from.
  /// \returns A reference to this shared pointer.
  ReadMostlySharedPtr& operator=(ReadMostlySharedPtr&& other) noexcept {
    std::swap(impl_, other.impl_);
    std::swap(ptrRaw_, other.ptrRaw_);
    return *this;
  }

  /// Copy-assigns from a shared pointer to a convertible type.
  ///
  /// \tparam T2 The source pointee type, convertible to `T`.
  /// \param other The shared pointer to copy.
  /// \returns A reference to this shared pointer.
  template <typename T2>
    requires detail::ptr_convertible<T2, T>
  ReadMostlySharedPtr& operator=(
      const ReadMostlySharedPtr<T2, RefCount>& other) {
    reset(other.impl_, other.ptrRaw_);
    return *this;
  }

  /// Move-assigns from a shared pointer to a convertible type.
  ///
  /// \tparam T2 The source pointee type, convertible to `T`.
  /// \param other The shared pointer to move from.
  /// \returns A reference to this shared pointer.
  template <typename T2>
    requires detail::ptr_convertible<T2, T>
  ReadMostlySharedPtr& operator=(
      ReadMostlySharedPtr<T2, RefCount>&& other) noexcept {
    reset();
    impl_ = std::exchange(other.impl_, nullptr);
    ptrRaw_ = std::exchange(other.ptrRaw_, nullptr);
    return *this;
  }

  /// Assigns a shared reference by locking a weak pointer.
  ///
  /// \tparam T2 The source pointee type, convertible to `T`.
  /// \param other The weak pointer to promote.
  /// \returns A reference to this shared pointer.
  template <typename T2>
    requires detail::ptr_convertible<T2, T>
  ReadMostlySharedPtr& operator=(const ReadMostlyWeakPtr<T2, RefCount>& other) {
    reset(other.impl_, other.ptrRaw_);
    return *this;
  }

  /// Assigns a shared reference from a main pointer.
  ///
  /// \tparam T2 The source pointee type, convertible to `T`.
  /// \param other The main pointer to reference.
  /// \returns A reference to this shared pointer.
  template <typename T2>
    requires detail::ptr_convertible<T2, T>
  ReadMostlySharedPtr& operator=(const ReadMostlyMainPtr<T2, RefCount>& other) {
    reset(other.impl_, other.ptrRaw_);
    return *this;
  }

  /// Destroys the shared pointer and releases its reference.
  ~ReadMostlySharedPtr() noexcept { reset(nullptr, nullptr); }

  /// Compares against a main pointer for pointer equality.
  ///
  /// \param other The main pointer to compare with.
  /// \returns `true` if both refer to the same object.
  bool operator==(const ReadMostlyMainPtr<T, RefCount>& other) const {
    return get() == other.get();
  }

  /// Compares against a raw pointer for equality.
  ///
  /// \param other The raw pointer to compare with.
  /// \returns `true` if this pointer refers to `other`.
  bool operator==(T* other) const { return get() == other; }

  /// Compares against another shared pointer for pointer equality.
  ///
  /// \param other The shared pointer to compare with.
  /// \returns `true` if both refer to the same object.
  bool operator==(const ReadMostlySharedPtr<T, RefCount>& other) const {
    return get() == other.get();
  }

  /// Releases the reference, making this pointer empty.
  void reset() { reset(nullptr, nullptr); }

  /// Returns the raw pointer to the referenced object.
  ///
  /// \returns The raw pointer, or `nullptr` if empty.
  T* get() const { return ptrRaw_; }

  /// Returns a standard shared pointer aliasing the referenced object.
  ///
  /// \returns A `std::shared_ptr` to the object, or empty if this is empty.
  std::shared_ptr<T> getStdShared() const {
    if (impl_) {
      return {impl_->getShared(), ptrRaw_};
    } else {
      return {};
    }
  }

  /// Dereferences the referenced object.
  ///
  /// \returns A reference to the referenced object.
  T& operator*() const { return *get(); }

  /// Accesses members of the referenced object.
  ///
  /// \returns The raw pointer to the referenced object.
  T* operator->() const { return get(); }

  /// Returns the current reference count of the object.
  ///
  /// \returns The number of shared references.
  size_t use_count() const { return impl_->useCount(); }

  /// Tests whether this is the only reference to the object.
  ///
  /// \returns `true` if the reference count is one.
  bool unique() const { return use_count() == 1; }

  /// Tests whether the pointer references an object.
  ///
  /// \returns `true` if an object is referenced.
  explicit operator bool() const { return impl_ != nullptr; }

 private:
  template <typename U, typename RefCount2>
  friend class ReadMostlyWeakPtr;
  template <typename U, typename RefCount2>
  friend class ReadMostlySharedPtr;

  void reset(detail::ReadMostlySharedPtrCore<RefCount>* impl, T* ptrRaw) {
    if (impl_ == impl) {
      return;
    }

    if (impl_) {
      impl_->decref();
      impl_ = nullptr;
      ptrRaw_ = nullptr;
    }

    if (impl && impl->incref()) {
      impl_ = impl;
      ptrRaw_ = ptrRaw;
    }
  }

  T* ptrRaw_{nullptr};
  detail::ReadMostlySharedPtrCore<RefCount>* impl_{nullptr};
};

/**
 * This can be used to destroy multiple ReadMostlyMainPtrs at once.
 */
template <typename RefCount = DefaultRefCount>
class ReadMostlyMainPtrDeleter {
 public:
  /// Destroys all added main pointers together.
  ~ReadMostlyMainPtrDeleter() noexcept {
    RefCount::useGlobal(refCounts_);
    for (auto& decref : decrefs_) {
      decref();
    }
  }

  /// Registers a main pointer to be destroyed by this deleter.
  ///
  /// Takes over the pointer's control block, leaving the argument empty.
  ///
  /// \tparam T The pointee type of the main pointer.
  /// \param ptr The main pointer to hand over.
  template <typename T>
  void add(ReadMostlyMainPtr<T, RefCount> ptr) noexcept {
    if (!ptr.impl_) {
      return;
    }

    refCounts_.push_back(&ptr.impl_->count_);
    refCounts_.push_back(&ptr.impl_->weakCount_);
    decrefs_.push_back([impl = ptr.impl_] { impl->decref(); });
    ptr.impl_ = nullptr;
    ptr.ptrRaw_ = nullptr;
  }

 private:
  std::vector<RefCount*> refCounts_;
  std::vector<folly::Function<void()>> decrefs_;
};

/// Tests whether a main pointer is empty.
///
/// \tparam T The pointee type.
/// \tparam RefCount The reference-count implementation.
/// \param ptr The main pointer to test.
/// \param null The null pointer constant to compare against.
/// \returns `true` if the pointer is empty.
template <typename T, typename RefCount>
inline bool operator==(
    const ReadMostlyMainPtr<T, RefCount>& ptr, std::nullptr_t null) {
  return ptr.get() == nullptr;
}

/// Tests whether a main pointer is empty.
///
/// \tparam T The pointee type.
/// \tparam RefCount The reference-count implementation.
/// \param null The null pointer constant to compare against.
/// \param ptr The main pointer to test.
/// \returns `true` if the pointer is empty.
template <typename T, typename RefCount>
inline bool operator==(
    std::nullptr_t null, const ReadMostlyMainPtr<T, RefCount>& ptr) {
  return ptr.get() == nullptr;
}

/// Tests whether a shared pointer is empty.
///
/// \tparam T The pointee type.
/// \tparam RefCount The reference-count implementation.
/// \param ptr The shared pointer to test.
/// \param null The null pointer constant to compare against.
/// \returns `true` if the pointer is empty.
template <typename T, typename RefCount>
inline bool operator==(
    const ReadMostlySharedPtr<T, RefCount>& ptr, std::nullptr_t null) {
  return ptr.get() == nullptr;
}

/// Tests whether a shared pointer is empty.
///
/// \tparam T The pointee type.
/// \tparam RefCount The reference-count implementation.
/// \param null The null pointer constant to compare against.
/// \param ptr The shared pointer to test.
/// \returns `true` if the pointer is empty.
template <typename T, typename RefCount>
inline bool operator==(
    std::nullptr_t null, const ReadMostlySharedPtr<T, RefCount>& ptr) {
  return ptr.get() == nullptr;
}

/// Tests whether a main pointer is non-empty.
///
/// \tparam T The pointee type.
/// \tparam RefCount The reference-count implementation.
/// \param ptr The main pointer to test.
/// \param null The null pointer constant to compare against.
/// \returns `true` if the pointer manages an object.
template <typename T, typename RefCount>
inline bool operator!=(
    const ReadMostlyMainPtr<T, RefCount>& ptr, std::nullptr_t null) {
  return !(ptr == nullptr);
}

/// Tests whether a main pointer is non-empty.
///
/// \tparam T The pointee type.
/// \tparam RefCount The reference-count implementation.
/// \param null The null pointer constant to compare against.
/// \param ptr The main pointer to test.
/// \returns `true` if the pointer manages an object.
template <typename T, typename RefCount>
inline bool operator!=(
    std::nullptr_t null, const ReadMostlyMainPtr<T, RefCount>& ptr) {
  return !(ptr == nullptr);
}

/// Tests whether a shared pointer is non-empty.
///
/// \tparam T The pointee type.
/// \tparam RefCount The reference-count implementation.
/// \param ptr The shared pointer to test.
/// \param null The null pointer constant to compare against.
/// \returns `true` if the pointer references an object.
template <typename T, typename RefCount>
inline bool operator!=(
    const ReadMostlySharedPtr<T, RefCount>& ptr, std::nullptr_t null) {
  return !(ptr == nullptr);
}

/// Tests whether a shared pointer is non-empty.
///
/// \tparam T The pointee type.
/// \tparam RefCount The reference-count implementation.
/// \param null The null pointer constant to compare against.
/// \param ptr The shared pointer to test.
/// \returns `true` if the pointer references an object.
template <typename T, typename RefCount>
inline bool operator!=(
    std::nullptr_t null, const ReadMostlySharedPtr<T, RefCount>& ptr) {
  return !(ptr == nullptr);
}
} // namespace folly
