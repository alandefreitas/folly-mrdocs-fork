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
#include <cstring>

#include <folly/Portability.h>
#include <folly/Preprocessor.h>

#if defined(__linux__) && !FOLLY_MOBILE
#include <sys/auxv.h> // @manual
#endif

namespace folly {

/**
 * Identification of hardware capabilities via the ELF auxiliary vector.
 *
 * Exposes the AT_HWCAP vector and, where available, the AT_HWCAP2 vector.
 *
 * DO NOT USE THIS CLASS ON X86_64 MACHINES
 *
 * Glibc massages the values in AT_HWCAP on x86_64 machines in a way that
 * makes little sense to consumers. Use folly::CpuId on x86_64 platforms.
 *
 * For aarch64 the class exposes methods with names derived from the defines
 * found in the Linux source tree (https://fburl.com/wl8qfh30).
 *
 * When compiled on anything other than Linux all methods will return false.
 *
 * The default constructor will call getauxval to retrieve the required
 * auxiliary vector entries. If you wish to use the class from an ifunc
 * resolver you should pass the hwcap and hwcap2 values received by your
 * resolver to the constructor.
 */
class ElfHwCaps {
 public:
  /// Constructs from explicit AT_HWCAP and AT_HWCAP2 values.
  ///
  /// \param hwcap The AT_HWCAP auxiliary vector value.
  /// \param hwcap2 The AT_HWCAP2 auxiliary vector value.
  FOLLY_ALWAYS_INLINE ElfHwCaps(uint64_t hwcap, uint64_t hwcap2)
      : hwcap_(hwcap), hwcap2_(hwcap2) {}

  /// Constructs by reading the auxiliary vector via `getauxval`.
  FOLLY_ALWAYS_INLINE ElfHwCaps() {
#if defined(__linux__) && !FOLLY_MOBILE
    hwcap_ = getauxval(AT_HWCAP);
#if defined(AT_HWCAP2)
    hwcap2_ = getauxval(AT_HWCAP2);
#endif
#endif
  }

#define FOLLY_DETAIL_HWCAP_X(arch, name, r, bit)                \
  FOLLY_ALWAYS_INLINE bool FB_CONCATENATE(arch, name)() const { \
    return ((r) & (1UL << (bit))) != 0;                         \
  }
#define FOLLY_DETAIL_HWCAP_NOIMPL_X(arch, name) \
  FOLLY_ALWAYS_INLINE bool FB_CONCATENATE(arch, name)() const { return false; }

#if FOLLY_AARCH64
#define FOLLY_DETAIL_HWCAP_AARCH64(name, bit) \
  FOLLY_DETAIL_HWCAP_X(aarch64_, name, hwcap_, (bit))
#define FOLLY_DETAIL_HWCAP2_AARCH64(name, bit) \
  FOLLY_DETAIL_HWCAP_X(aarch64_, name, hwcap2_, (bit))
#else
#define FOLLY_DETAIL_HWCAP_AARCH64(name, bit) \
  FOLLY_DETAIL_HWCAP_NOIMPL_X(aarch64_, name)
#define FOLLY_DETAIL_HWCAP2_AARCH64(name, bit) \
  FOLLY_DETAIL_HWCAP_NOIMPL_X(aarch64_, name)
#endif

  /// Reports whether the aarch64 fp hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(fp, 0)
  /// Reports whether the aarch64 asimd hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(asimd, 1)
  /// Reports whether the aarch64 evtstrm hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(evtstrm, 2)
  /// Reports whether the aarch64 aes hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(aes, 3)
  /// Reports whether the aarch64 pmull hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(pmull, 4)
  /// Reports whether the aarch64 sha1 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(sha1, 5)
  /// Reports whether the aarch64 sha2 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(sha2, 6)
  /// Reports whether the aarch64 crc32 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(crc32, 7)
  /// Reports whether the aarch64 atomics hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(atomics, 8)
  /// Reports whether the aarch64 fphp hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(fphp, 9)
  /// Reports whether the aarch64 asimdhp hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(asimdhp, 10)
  /// Reports whether the aarch64 cpuid hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(cpuid, 11)
  /// Reports whether the aarch64 asimdrdm hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(asimdrdm, 12)
  /// Reports whether the aarch64 jscvt hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(jscvt, 13)
  /// Reports whether the aarch64 fcma hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(fcma, 14)
  /// Reports whether the aarch64 lrcpc hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(lrcpc, 15)
  /// Reports whether the aarch64 dcpop hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(dcpop, 16)
  /// Reports whether the aarch64 sha3 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(sha3, 17)
  /// Reports whether the aarch64 sm3 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(sm3, 18)
  /// Reports whether the aarch64 sm4 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(sm4, 19)
  /// Reports whether the aarch64 asimddp hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(asimddp, 20)
  /// Reports whether the aarch64 sha512 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(sha512, 21)
  /// Reports whether the aarch64 sve hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(sve, 22)
  /// Reports whether the aarch64 asimdfhm hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(asimdfhm, 23)
  /// Reports whether the aarch64 dit hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(dit, 24)
  /// Reports whether the aarch64 uscat hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(uscat, 25)
  /// Reports whether the aarch64 ilrcpc hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(ilrcpc, 26)
  /// Reports whether the aarch64 flagm hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(flagm, 27)
  /// Reports whether the aarch64 ssbs hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(ssbs, 28)
  /// Reports whether the aarch64 sb hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(sb, 29)
  /// Reports whether the aarch64 paca hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(paca, 30)
  /// Reports whether the aarch64 pacg hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP_AARCH64(pacg, 31)

  /// Reports whether the aarch64 dcpodp hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(dcpodp, 0)
  /// Reports whether the aarch64 sve2 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sve2, 1)
  /// Reports whether the aarch64 sveaes hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sveaes, 2)
  /// Reports whether the aarch64 svepmull hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(svepmull, 3)
  /// Reports whether the aarch64 svebitperm hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(svebitperm, 4)
  /// Reports whether the aarch64 svesha3 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(svesha3, 5)
  /// Reports whether the aarch64 svesm4 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(svesm4, 6)
  /// Reports whether the aarch64 flagm2 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(flagm2, 7)
  /// Reports whether the aarch64 frint hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(frint, 8)
  /// Reports whether the aarch64 svei8mm hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(svei8mm, 9)
  /// Reports whether the aarch64 svef32mm hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(svef32mm, 10)
  /// Reports whether the aarch64 svef64mm hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(svef64mm, 11)
  /// Reports whether the aarch64 svebf16 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(svebf16, 12)
  /// Reports whether the aarch64 i8mm hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(i8mm, 13)
  /// Reports whether the aarch64 bf16 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(bf16, 14)
  /// Reports whether the aarch64 dgh hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(dgh, 15)
  /// Reports whether the aarch64 rng hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(rng, 16)
  /// Reports whether the aarch64 bti hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(bti, 17)
  /// Reports whether the aarch64 mte hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(mte, 18)
  /// Reports whether the aarch64 ecv hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(ecv, 19)
  /// Reports whether the aarch64 afp hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(afp, 20)
  /// Reports whether the aarch64 rpres hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(rpres, 21)
  /// Reports whether the aarch64 mte3 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(mte3, 22)
  /// Reports whether the aarch64 sme hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme, 23)
  /// Reports whether the aarch64 sme_i16i64 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_i16i64, 24)
  /// Reports whether the aarch64 sme_f64f64 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_f64f64, 25)
  /// Reports whether the aarch64 sme_i8i32 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_i8i32, 26)
  /// Reports whether the aarch64 sme_f16f32 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_f16f32, 27)
  /// Reports whether the aarch64 sme_b16f32 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_b16f32, 28)
  /// Reports whether the aarch64 sme_f32f32 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_f32f32, 29)
  /// Reports whether the aarch64 sme_fa64 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_fa64, 30)
  /// Reports whether the aarch64 wfxt hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(wfxt, 31)
  /// Reports whether the aarch64 ebf16 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(ebf16, 32)
  /// Reports whether the aarch64 sve_ebf16 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sve_ebf16, 33)
  /// Reports whether the aarch64 cssc hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(cssc, 34)
  /// Reports whether the aarch64 rprfm hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(rprfm, 35)
  /// Reports whether the aarch64 sve2p1 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sve2p1, 36)
  /// Reports whether the aarch64 sme2 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme2, 37)
  /// Reports whether the aarch64 sme2p1 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme2p1, 38)
  /// Reports whether the aarch64 sme_i16i32 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_i16i32, 39)
  /// Reports whether the aarch64 sme_bi32i32 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_bi32i32, 40)
  /// Reports whether the aarch64 sme_b16b16 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_b16b16, 41)
  /// Reports whether the aarch64 sme_f16f16 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_f16f16, 42)
  /// Reports whether the aarch64 mops hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(mops, 43)
  /// Reports whether the aarch64 hbc hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(hbc, 44)
  /// Reports whether the aarch64 sve_b16b16 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sve_b16b16, 45)
  /// Reports whether the aarch64 lrcpc3 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(lrcpc3, 46)
  /// Reports whether the aarch64 lse128 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(lse128, 47)
  /// Reports whether the aarch64 fpmr hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(fpmr, 48)
  /// Reports whether the aarch64 lut hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(lut, 49)
  /// Reports whether the aarch64 faminmax hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(faminmax, 50)
  /// Reports whether the aarch64 f8cvt hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(f8cvt, 51)
  /// Reports whether the aarch64 f8fma hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(f8fma, 52)
  /// Reports whether the aarch64 f8dp4 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(f8dp4, 53)
  /// Reports whether the aarch64 f8dp2 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(f8dp2, 54)
  /// Reports whether the aarch64 f8e4m3 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(f8e4m3, 55)
  /// Reports whether the aarch64 f8e5m2 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(f8e5m2, 56)
  /// Reports whether the aarch64 sme_lutv2 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_lutv2, 57)
  /// Reports whether the aarch64 sme_f8f16 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_f8f16, 58)
  /// Reports whether the aarch64 sme_f8f32 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_f8f32, 59)
  /// Reports whether the aarch64 sme_sf8fma hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_sf8fma, 60)
  /// Reports whether the aarch64 sme_sf8dp4 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_sf8dp4, 61)
  /// Reports whether the aarch64 sme_sf8dp2 hardware capability is present.
  ///
  /// \returns `true` if the feature bit is set, `false` otherwise.
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_sf8dp2, 62)

#undef FOLLY_DETAIL_HWCAP2_AARCH64
#undef FOLLY_DETAIL_HWCAP_AARCH64
#undef FOLLY_DETAIL_HWCAP_NOIMPL_X
#undef FOLLY_DETAIL_HWCAP_X

 private:
  // GCC would not warn about maybe unused here, but will warn
  // about the ignored attribute.
#if defined(__clang__)
  uint64_t hwcap_ [[maybe_unused]] = 0;
  uint64_t hwcap2_ [[maybe_unused]] = 0;
#else
  uint64_t hwcap_ = 0;
  uint64_t hwcap2_ = 0;
#endif
};

} // namespace folly
