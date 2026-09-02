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

// Some helper functions for mallctl.

#pragma once

#include <folly/memory/Malloc.h>

#include <stdexcept>
#include <type_traits>

namespace folly {

namespace detail {

[[noreturn]] void handleMallctlError(const char* fn, const char* cmd, int err);

template <typename T>
void mallctlHelper(const char* cmd, T* out, T* in) {
  if (!usingJEMalloc()) {
    throw_exception<std::logic_error>("mallctl: not using jemalloc");
  }

  size_t outLen = sizeof(T);
  int err = mallctl(cmd, out, out ? &outLen : nullptr, in, in ? sizeof(T) : 0);
  if (err != 0) {
    handleMallctlError("mallctl", cmd, err);
  }
}

} // namespace detail

/// Reads a value from jemalloc through the named `mallctl` command.
///
/// \param cmd The mallctl command name to read.
/// \param out Destination for the value read from jemalloc.
template <typename T>
void mallctlRead(const char* cmd, T* out) {
  detail::mallctlHelper(cmd, out, static_cast<T*>(nullptr));
}

/// Writes a value to jemalloc through the named `mallctl` command.
///
/// \param cmd The mallctl command name to write.
/// \param in The value to write to jemalloc.
template <typename T>
void mallctlWrite(const char* cmd, T in) {
  detail::mallctlHelper(cmd, static_cast<T*>(nullptr), &in);
}

/// Writes a value and reads the previous one through the named `mallctl`
/// command.
///
/// \param cmd The mallctl command name to read and write.
/// \param out Destination for the previous value read from jemalloc.
/// \param in The value to write to jemalloc.
template <typename T>
void mallctlReadWrite(const char* cmd, T* out, T in) {
  detail::mallctlHelper(cmd, out, &in);
}

/// Invokes a `mallctl` command that neither reads nor writes a value.
///
/// \param cmd The mallctl command name to call.
inline void mallctlCall(const char* cmd) {
  // Use <unsigned> rather than <void> to avoid sizeof(void).
  mallctlRead<unsigned>(cmd, nullptr);
}

/*
 * The following implements a caching utility for usage cases where:
 * - the same mallctl command is called many times, and
 * - performance is important.
 */

namespace detail {

class MallctlMibCache {
 protected:
  explicit MallctlMibCache(const char* cmd) {
    if (!usingJEMalloc()) {
      throw_exception<std::logic_error>("mallctlnametomib: not using jemalloc");
    }
    int err = mallctlnametomib(cmd, mib, &miblen);
    if (err != 0) {
      handleMallctlError("mallctlnametomib", cmd, err);
    }
  }

  template <typename ReadType, typename WriteType>
  void mallctlbymibHelper(ReadType* out, WriteType* in) const {
    assert((out == nullptr) == std::is_void<ReadType>::value);
    assert((in == nullptr) == std::is_void<WriteType>::value);
    size_t outLen = sizeofHelper<ReadType>();
    int err = mallctlbymib(
        mib,
        miblen,
        out,
        out ? &outLen : nullptr,
        in,
        in ? sizeofHelper<WriteType>() : 0);
    if (err != 0) {
      handleMallctlError("mallctlbymib", nullptr, err);
    }
  }

 private:
  static constexpr size_t kMaxMibLen = 8;
  size_t mib[kMaxMibLen];
  size_t miblen = kMaxMibLen;

  template <typename T>
  constexpr size_t sizeofHelper() const {
    constexpr bool v = std::is_void<T>::value;
    using not_used = char;
    using S = std::conditional_t<v, not_used, T>;
    return v ? 0 : sizeof(S);
  }
};

} // namespace detail

/// Caches the resolved MIB for a `mallctl` command that neither reads nor
/// writes a value, for repeated fast invocation.
class MallctlMibCallCache : private detail::MallctlMibCache {
 public:
  /// Resolves and caches the MIB for the given command.
  ///
  /// \param cmd The mallctl command name to cache.
  explicit MallctlMibCallCache(const char* cmd) : MallctlMibCache(cmd) {}

  /// Invokes the cached command.
  void operator()() const {
    mallctlbymibHelper((void*)nullptr, (void*)nullptr);
  }
};

/// Caches the resolved MIB for a `mallctl` command that reads a value, for
/// repeated fast invocation.
template <typename ReadType>
class MallctlMibReadCache : private detail::MallctlMibCache {
 public:
  /// Resolves and caches the MIB for the given command.
  ///
  /// \param cmd The mallctl command name to cache.
  explicit MallctlMibReadCache(const char* cmd) : MallctlMibCache(cmd) {}

  /// Invokes the cached command and returns the value read.
  ///
  /// \returns The value read from jemalloc.
  ReadType operator()() const {
    ReadType out;
    mallctlbymibHelper(&out, (void*)nullptr);
    return out;
  }
};

/// Caches the resolved MIB for a `mallctl` command that writes a value, for
/// repeated fast invocation.
template <typename WriteType>
class MallctlMibWriteCache : private detail::MallctlMibCache {
 public:
  /// Resolves and caches the MIB for the given command.
  ///
  /// \param cmd The mallctl command name to cache.
  explicit MallctlMibWriteCache(const char* cmd) : MallctlMibCache(cmd) {}

  /// Invokes the cached command, writing the given value.
  ///
  /// \param in The value to write to jemalloc.
  void operator()(WriteType in) const {
    mallctlbymibHelper((void*)nullptr, &in);
  }
};

/// Caches the resolved MIB for a `mallctl` command that writes a value and
/// reads the previous one, for repeated fast invocation.
template <typename ReadType, typename WriteType>
class MallctlMibReadWriteCache : private detail::MallctlMibCache {
 public:
  /// Resolves and caches the MIB for the given command.
  ///
  /// \param cmd The mallctl command name to cache.
  explicit MallctlMibReadWriteCache(const char* cmd) : MallctlMibCache(cmd) {}

  /// Invokes the cached command, writing the given value and returning the
  /// previous one.
  ///
  /// \param in The value to write to jemalloc.
  /// \returns The previous value read from jemalloc.
  ReadType operator()(WriteType in) const {
    ReadType out;
    mallctlbymibHelper(&out, &in);
    return out;
  }
};

/// Caches the resolved MIB for a `mallctl` command that exchanges a value of a
/// single type.
template <typename ExchangeType>
using MallctlMibExchangeCache =
    MallctlMibReadWriteCache<ExchangeType, ExchangeType>;

} // namespace folly
