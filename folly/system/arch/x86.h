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
#include <cstdint>
#include <cstring>

#include <folly/Portability.h>

#if FOLLY_X86 || FOLLY_X64
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

namespace folly {

/// x86_cpuid
///
/// Wrapper around x86 instruction cpuid.
/// * Some platforms have intrinsics but not all do.
/// * The instruction is tricky to handle in some cases.
///
/// The instruction takes its arguments in eax, ecx and produces its results in
/// eax, ebx, ecx, edx. So it is rather straightforward to wrap. But, a wrinkle:
/// x86 PIC code uses ebx as the PIC register, so it must be preserved around
/// the cpuid instruction.
///
/// This function might be used in __ifunc__ code, which runs concurrently with
/// relocations. So using symbols with potentially external linkage, such as
/// calling functions that might be relocated, is forbidden. Address this by
/// by marking as [[always_inline]].
///
/// multi-thread-safe
/// async-signal-safe
/// reentrancy-safe
/// ifunc-safe
///
/// \param info Output array of four unsigned ints receiving eax, ebx, ecx, edx.
/// \param leaf The cpuid leaf, passed in eax.
/// \param tag The cpuid subleaf, passed in ecx.
FOLLY_ALWAYS_INLINE void x86_cpuid(
    unsigned int info[4],
    [[maybe_unused]] unsigned int leaf,
    [[maybe_unused]] unsigned int tag = 0) {
#if FOLLY_X86 || FOLLY_X64
#if defined(_MSC_VER)
  __cpuidex(reinterpret_cast<int*>(info), leaf, tag); // no inline asm
#else
  asm volatile(
#if defined(__pic__) && defined(__i386__)
      // ebx is PIC register - must preserve ebx around cpuid
      R"(
        mov %%ebx, %[tmp]
        cpuid
        xchg %%ebx, %[tmp]
      )"
#else
      // ebx is not special
      R"(
        cpuid
      )"
#endif
      : // outputs
      "=a"(info[0]),
#if defined(__pic__) && defined(__i386__)
      [tmp] "=&r"(info[1]), // ebx out was xchg'd to tmp; early clobber
#else
      "=b"(info[1]), // ebx out is in ebx
#endif
      "=c"(info[2]),
      "=d"(info[3])
      : // inputs
      "a"(leaf),
      "c"(tag)
      : // clobbers
  );
#endif
#else
  info[0] = info[1] = info[2] = info[3] = 0;
#endif
}

/// Returns the maximum supported cpuid leaf for the given leaf group.
///
/// \param leaf The cpuid leaf group to query.
/// \param sig Optional out-pointer receiving the vendor signature from ebx.
/// \returns The maximum supported leaf value from eax.
FOLLY_ALWAYS_INLINE unsigned int x86_cpuid_max( //
    unsigned int leaf,
    unsigned int* sig) {
  unsigned int info[4];
  x86_cpuid(info, leaf);
  if (sig) {
    *sig = info[1]; // ebx
  }
  return info[0]; // eax
}

/// Recognized x86 CPU vendors.
enum class x86_cpuid_vendor {
  unknown, ///< Vendor could not be determined.
  intel, ///< Intel CPU.
  amd, ///< AMD CPU.
};

/// Vendor identification string as returned by the cpuid instruction.
union x86_cpuid_vendor_name {
  /// The vendor string as a null-terminated character array.
  char const str[13];
  /// The vendor string as three 32-bit words.
  unsigned int words[3];
};
/// Vendor identification strings indexed by `x86_cpuid_vendor`.
inline constexpr x86_cpuid_vendor_name x86_cpuid_vendor_names[3] = {
    {},
    {"GenuineIntel"},
    {"AuthenticAMD"},
};

/// Detects the CPU vendor via the cpuid instruction.
///
/// \returns The detected CPU vendor, or `x86_cpuid_vendor::unknown`.
FOLLY_ALWAYS_INLINE x86_cpuid_vendor x86_cpuid_get_vendor() {
  if constexpr (kIsArchX86 || kIsArchAmd64) {
    unsigned int info[4];
    x86_cpuid(info, 0);
    constexpr auto num_names =
        sizeof(x86_cpuid_vendor_names) / sizeof(x86_cpuid_vendor_name);
    for (unsigned int i = 1; i < num_names; ++i) {
      auto& name = x86_cpuid_vendor_names[i].words;
      if (info[1] == name[0] && info[2] == name[2] && info[3] == name[1]) {
        return static_cast<x86_cpuid_vendor>(i);
      }
    }
  }
  return x86_cpuid_vendor::unknown;
}

/// Decoded x86 cpuid cache descriptor for one cache level.
struct x86_cpuid_cache_info {
  /// Maximum number of cache ids to probe.
  static inline constexpr unsigned int id_count_max = 32;

  /// Raw eax register value from the cpuid cache leaf.
  unsigned int eax = 0;
  /// Raw ebx register value from the cpuid cache leaf.
  unsigned int ebx = 0;
  /// Raw ecx register value from the cpuid cache leaf.
  unsigned int ecx = 0;

  /// Returns the raw cache type field.
  ///
  /// \returns The cache type field encoded in eax.
  size_t cache_type() const noexcept { return eax & 0x1F; }
  /// Returns whether the cache holds data.
  ///
  /// \returns True if this is a data or unified cache.
  bool cache_type_data() const noexcept { return cache_type() & 1; }
  /// Returns whether the cache holds instructions.
  ///
  /// \returns True if this is an instruction or unified cache.
  bool cache_type_inst() const noexcept { return cache_type() & 2; }
  /// Returns whether the cache descriptor is null.
  ///
  /// \returns True if there is no cache at this id.
  bool cache_type_null() const noexcept { return !cache_type(); }

  /// Returns the cache level.
  ///
  /// \returns The cache level (1 for L1, and so on).
  size_t level() const noexcept { return (eax >> 5) & 0x7; }
  /// Returns the cache line size in bytes.
  ///
  /// \returns The cache line size in bytes.
  size_t line_size() const noexcept { return (ebx & 0xFFF) + 1; }
  /// Returns the number of physical line partitions.
  ///
  /// \returns The number of physical line partitions.
  size_t partitions() const noexcept { return ((ebx >> 12) & 0x3FF) + 1; }
  /// Returns the associativity of the cache.
  ///
  /// \returns The number of ways of associativity.
  size_t ways() const noexcept { return ((ebx >> 22) & 0x3FF) + 1; }
  /// Returns the number of sets in the cache.
  ///
  /// \returns The number of sets.
  size_t sets() const noexcept { return ecx + 1; }
  /// Returns the total size of the cache in bytes.
  ///
  /// \returns The cache size in bytes, or 0 if the descriptor is null.
  size_t cache_size() const noexcept {
    return !cache_type_null() * ways() * partitions() * line_size() * sets();
  }
};

/// Returns cache info for the cache with the given id for the given vendor.
///
/// \param vend The detected CPU vendor.
/// \param id The zero-based cache level index to query.
/// \returns Cache info for the requested cache, or an empty value if none.
FOLLY_ALWAYS_INLINE x86_cpuid_cache_info
x86_cpuid_get_cache_info(x86_cpuid_vendor vend, unsigned int id) {
  unsigned int info[4];
  switch (vend) {
    case x86_cpuid_vendor::unknown:
      return x86_cpuid_cache_info{};
    case x86_cpuid_vendor::intel:
      x86_cpuid(info, /* leaf = */ 4, /* tag = */ id + 1);
      return x86_cpuid_cache_info{info[0], info[1], info[2]};
    case x86_cpuid_vendor::amd:
      x86_cpuid(info, /* leaf = */ 0x8000001D, /* tag = */ id + 1);
      return x86_cpuid_cache_info{info[0], info[1], info[2]};
    default:
      assert(0 && "unsupported x86 vendor");
      return x86_cpuid_cache_info{};
  }
}

/// Returns cache info for the last-level cache for the given vendor.
///
/// \param vend The detected CPU vendor.
/// \returns Cache info for the last-level data cache.
FOLLY_ALWAYS_INLINE x86_cpuid_cache_info
x86_cpuid_get_llc_cache_info(x86_cpuid_vendor vend) {
  x86_cpuid_cache_info cache_info{};
  for (unsigned int i = 0; i < x86_cpuid_cache_info::id_count_max; ++i) {
    auto const info = x86_cpuid_get_cache_info(vend, i);
    if (info.cache_type_null()) {
      break;
    }
    if (info.cache_type_data()) {
      cache_info = info;
    }
  }
  return cache_info;
}

/// Returns cache info for the last-level cache of the detected CPU vendor.
///
/// \returns Cache info for the last-level data cache.
FOLLY_ALWAYS_INLINE x86_cpuid_cache_info x86_cpuid_get_llc_cache_info() {
  auto const vend = x86_cpuid_get_vendor();
  return x86_cpuid_get_llc_cache_info(vend);
}

} // namespace folly
