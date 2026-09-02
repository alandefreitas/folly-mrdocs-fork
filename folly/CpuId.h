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
#include <folly/system/arch/x86.h>

namespace folly {

/**
 * Identification of an Intel CPU.
 * Supports CPUID feature flags (EAX=1) and extended features (EAX=7, ECX=0).
 * Values from
 * http://www.intel.com/content/www/us/en/processors/processor-identification-cpuid-instruction-note.html
 */
class CpuId {
 public:
  /** Construct a CpuId, issuing the CPUID instruction and caching the results.

      Always inline in order for this to be usable from a `__ifunc__`.
      In shared library mode, a `__ifunc__` runs at relocation time, while the
      PLT hasn't been fully populated yet; thus, ifuncs cannot use symbols
      with potentially external linkage. (This issue is less likely in opt
      mode since inlining happens more likely, and it doesn't happen for
      statically linked binaries which don't depend on the PLT)
  */
  FOLLY_ALWAYS_INLINE CpuId() {
    unsigned int reg[4];
    x86_cpuid(reg, 0);
    vendor_[0] = reg[1];
    vendor_[1] = reg[3];
    vendor_[2] = reg[2];
    const int n = reg[0];
    if (n >= 1) {
      x86_cpuid(reg, 1);
      f1c_ = reg[2];
      f1d_ = reg[3];
    }
    if (n >= 7) {
      x86_cpuid(reg, 7);
      f7b_ = reg[1];
      f7c_ = reg[2];
      f7d_ = reg[3];
    }
  }

#define FOLLY_DETAIL_CPUID_X(name, r, bit) \
  FOLLY_ALWAYS_INLINE bool name() const { return ((r) & (1U << bit)) != 0; }

// cpuid(1): Processor Info and Feature Bits.
#define FOLLY_DETAIL_CPUID_C(name, bit) FOLLY_DETAIL_CPUID_X(name, f1c_, bit)
  /// Query whether the CPU reports support for the `sse3` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(sse3, 0)
  /// Query whether the CPU reports support for the `pclmuldq` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(pclmuldq, 1)
  /// Query whether the CPU reports support for the `dtes64` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(dtes64, 2)
  /// Query whether the CPU reports support for the `monitor` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(monitor, 3)
  /// Query whether the CPU reports support for the `dscpl` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(dscpl, 4)
  /// Query whether the CPU reports support for the `vmx` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(vmx, 5)
  /// Query whether the CPU reports support for the `smx` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(smx, 6)
  /// Query whether the CPU reports support for the `eist` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(eist, 7)
  /// Query whether the CPU reports support for the `tm2` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(tm2, 8)
  /// Query whether the CPU reports support for the `ssse3` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(ssse3, 9)
  /// Query whether the CPU reports support for the `cnxtid` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(cnxtid, 10)
  /// Query whether the CPU reports support for the `fma` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(fma, 12)
  /// Query whether the CPU reports support for the `cx16` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(cx16, 13)
  /// Query whether the CPU reports support for the `xtpr` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(xtpr, 14)
  /// Query whether the CPU reports support for the `pdcm` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(pdcm, 15)
  /// Query whether the CPU reports support for the `pcid` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(pcid, 17)
  /// Query whether the CPU reports support for the `dca` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(dca, 18)
  /// Query whether the CPU reports support for the `sse41` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(sse41, 19)
  /// Query whether the CPU reports support for the `sse42` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(sse42, 20)
  /// Query whether the CPU reports support for the `x2apic` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(x2apic, 21)
  /// Query whether the CPU reports support for the `movbe` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(movbe, 22)
  /// Query whether the CPU reports support for the `popcnt` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(popcnt, 23)
  /// Query whether the CPU reports support for the `tscdeadline` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(tscdeadline, 24)
  /// Query whether the CPU reports support for the `aes` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(aes, 25)
  /// Query whether the CPU reports support for the `xsave` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(xsave, 26)
  /// Query whether the CPU reports support for the `osxsave` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(osxsave, 27)
  /// Query whether the CPU reports support for the `avx` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(avx, 28)
  /// Query whether the CPU reports support for the `f16c` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(f16c, 29)
  /// Query whether the CPU reports support for the `rdrand` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(rdrand, 30)
#undef FOLLY_DETAIL_CPUID_C
#define FOLLY_DETAIL_CPUID_D(name, bit) FOLLY_DETAIL_CPUID_X(name, f1d_, bit)
  /// Query whether the CPU reports support for the `fpu` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(fpu, 0)
  /// Query whether the CPU reports support for the `vme` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(vme, 1)
  /// Query whether the CPU reports support for the `de` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(de, 2)
  /// Query whether the CPU reports support for the `pse` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(pse, 3)
  /// Query whether the CPU reports support for the `tsc` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(tsc, 4)
  /// Query whether the CPU reports support for the `msr` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(msr, 5)
  /// Query whether the CPU reports support for the `pae` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(pae, 6)
  /// Query whether the CPU reports support for the `mce` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(mce, 7)
  /// Query whether the CPU reports support for the `cx8` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(cx8, 8)
  /// Query whether the CPU reports support for the `apic` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(apic, 9)
  /// Query whether the CPU reports support for the `sep` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(sep, 11)
  /// Query whether the CPU reports support for the `mtrr` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(mtrr, 12)
  /// Query whether the CPU reports support for the `pge` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(pge, 13)
  /// Query whether the CPU reports support for the `mca` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(mca, 14)
  /// Query whether the CPU reports support for the `cmov` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(cmov, 15)
  /// Query whether the CPU reports support for the `pat` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(pat, 16)
  /// Query whether the CPU reports support for the `pse36` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(pse36, 17)
  /// Query whether the CPU reports support for the `psn` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(psn, 18)
  /// Query whether the CPU reports support for the `clfsh` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(clfsh, 19)
  /// Query whether the CPU reports support for the `ds` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(ds, 21)
  /// Query whether the CPU reports support for the `acpi` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(acpi, 22)
  /// Query whether the CPU reports support for the `mmx` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(mmx, 23)
  /// Query whether the CPU reports support for the `fxsr` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(fxsr, 24)
  /// Query whether the CPU reports support for the `sse` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(sse, 25)
  /// Query whether the CPU reports support for the `sse2` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(sse2, 26)
  /// Query whether the CPU reports support for the `ss` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(ss, 27)
  /// Query whether the CPU reports support for the `htt` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(htt, 28)
  /// Query whether the CPU reports support for the `tm` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(tm, 29)
  /// Query whether the CPU reports support for the `pbe` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(pbe, 31)
#undef FOLLY_DETAIL_CPUID_D

  // cpuid(7): Extended Features.
#define FOLLY_DETAIL_CPUID_B(name, bit) FOLLY_DETAIL_CPUID_X(name, f7b_, bit)
  /// Query whether the CPU reports support for the `bmi1` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(bmi1, 3)
  /// Query whether the CPU reports support for the `hle` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(hle, 4)
  /// Query whether the CPU reports support for the `avx2` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(avx2, 5)
  /// Query whether the CPU reports support for the `smep` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(smep, 7)
  /// Query whether the CPU reports support for the `bmi2` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(bmi2, 8)
  /// Query whether the CPU reports support for the `erms` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(erms, 9)
  /// Query whether the CPU reports support for the `invpcid` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(invpcid, 10)
  /// Query whether the CPU reports support for the `rtm` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(rtm, 11)
  /// Query whether the CPU reports support for the `mpx` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(mpx, 14)
  /// Query whether the CPU reports support for the `avx512f` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(avx512f, 16)
  /// Query whether the CPU reports support for the `avx512dq` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(avx512dq, 17)
  /// Query whether the CPU reports support for the `rdseed` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(rdseed, 18)
  /// Query whether the CPU reports support for the `adx` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(adx, 19)
  /// Query whether the CPU reports support for the `smap` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(smap, 20)
  /// Query whether the CPU reports support for the `avx512ifma` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(avx512ifma, 21)
  /// Query whether the CPU reports support for the `pcommit` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(pcommit, 22)
  /// Query whether the CPU reports support for the `clflushopt` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(clflushopt, 23)
  /// Query whether the CPU reports support for the `clwb` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(clwb, 24)
  /// Query whether the CPU reports support for the `avx512pf` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(avx512pf, 26)
  /// Query whether the CPU reports support for the `avx512er` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(avx512er, 27)
  /// Query whether the CPU reports support for the `avx512cd` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(avx512cd, 28)
  /// Query whether the CPU reports support for the `sha` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(sha, 29)
  /// Query whether the CPU reports support for the `avx512bw` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(avx512bw, 30)
  /// Query whether the CPU reports support for the `avx512vl` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_B(avx512vl, 31)
#undef FOLLY_DETAIL_CPUID_B
#define FOLLY_DETAIL_CPUID_C(name, bit) FOLLY_DETAIL_CPUID_X(name, f7c_, bit)
  /// Query whether the CPU reports support for the `prefetchwt1` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(prefetchwt1, 0)
  /// Query whether the CPU reports support for the `avx512vbmi` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(avx512vbmi, 1)
  /// Query whether the CPU reports support for the `avx512vbmi2` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(avx512vbmi2, 6)
  /// Query whether the CPU reports support for the `vaes` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(vaes, 9)
  /// Query whether the CPU reports support for the `vpclmulqdq` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(vpclmulqdq, 10)
  /// Query whether the CPU reports support for the `avx512vnni` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(avx512vnni, 11)
  /// Query whether the CPU reports support for the `avx512bitalg` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(avx512bitalg, 12)
  /// Query whether the CPU reports support for the `avx512vpopcntdq` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(avx512vpopcntdq, 14)
  /// Query whether the CPU reports support for the `rdpid` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_C(rdpid, 22)
#undef FOLLY_DETAIL_CPUID_C
#define FOLLY_DETAIL_CPUID_D(name, bit) FOLLY_DETAIL_CPUID_X(name, f7d_, bit)
  /// Query whether the CPU reports support for the `avx5124vnniw` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(avx5124vnniw, 2)
  /// Query whether the CPU reports support for the `avx5124fmaps` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(avx5124fmaps, 3)
  /// Query whether the CPU reports support for the `avx512vp2intersect` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(avx512vp2intersect, 8)
  /// Query whether the CPU reports support for the `amxbf16` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(amxbf16, 22)
  /// Query whether the CPU reports support for the `avx512fp16` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(avx512fp16, 23)
  /// Query whether the CPU reports support for the `amxtile` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(amxtile, 24)
  /// Query whether the CPU reports support for the `amxint8` feature.
  /// \implementationdefined
  FOLLY_DETAIL_CPUID_D(amxint8, 25)
#undef FOLLY_DETAIL_CPUID_D

#undef FOLLY_DETAIL_CPUID_X

#define FOLLY_DETAIL_VENDOR(name, str)                         \
  FOLLY_ALWAYS_INLINE bool vendor_##name() const {             \
    /* Size of str should be 12 + NUL terminator. */           \
    static_assert(sizeof(str) == 13, "Bad CPU Vendor string"); \
    /* Just as with the main CpuId call above, this can also   \
    still be in an __ifunc__, so no function calls :( */       \
    return memcmp(&vendor_[0], &str[0], 12) == 0;              \
  }

  /// Query whether the CPU vendor identity is `intel`.
  /// \implementationdefined
  FOLLY_DETAIL_VENDOR(intel, "GenuineIntel")
  /// Query whether the CPU vendor identity is `amd`.
  /// \implementationdefined
  FOLLY_DETAIL_VENDOR(amd, "AuthenticAMD")

#undef FOLLY_DETAIL_VENDOR

 private:
  uint32_t vendor_[3] = {0};
  uint32_t f1c_ = 0;
  uint32_t f1d_ = 0;
  uint32_t f7b_ = 0;
  uint32_t f7c_ = 0;
  uint32_t f7d_ = 0;
};

} // namespace folly
