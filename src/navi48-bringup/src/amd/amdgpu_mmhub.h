//
//  amdgpu_mmhub.h — MMHUB register offsets selected by discovered IP version.
//
//  MMHUB owns regMMMC_VM_FB_LOCATION_BASE, which is where vram_start (the
//  GPU/MC base of VRAM) comes from. Every MC address the kext computes — the
//  discovery TMR, the PSP firmware staging area, every GART mapping — depends
//  on it, so a wrong value here does not fail loudly, it silently produces a
//  garbage base address and faults later.
//
//  The offsets moved completely between MMHUB 4.1.0 (Navi48) and MMHUB 3.0.0
//  (Navi33 / gfx1102); they are not a fixed delta, the register map is split
//  across three sub-blocks that each shifted by a different amount. Every value
//  below is transcribed from the upstream headers:
//
//    mmhub_4_1_0_offset.h  (RDNA4, Navi48)
//    mmhub_3_0_0_offset.h  (RDNA3, Navi33) — e.g. FB_LOCATION_BASE 0x08ec
//                                            vs 0x0554 on 4.1.0
//
//  Selection follows gmc_v11_0.c:577-614, which picks mmhub_v3_0_funcs /
//  mmhub_v3_0_1_funcs / mmhub_v3_0_2_funcs from the harvested MMHUB version.
//  We select the offset table the same way rather than branching on a chip name.
//
#pragma once

#include <stdint.h>

#include "amdgpu_ip.h"

namespace amdgpu {

struct MmhubRegs {
    // Framebuffer location. Base is read, never programmed, except by the SOS
    // the PSP boots first.
    uint32_t FB_LOCATION_BASE;
    uint32_t FB_LOCATION_TOP;
    uint32_t FB_OFFSET;

    uint32_t AGP_TOP;
    uint32_t AGP_BOT;
    uint32_t AGP_BASE;

    uint32_t SYSTEM_APERTURE_LOW_ADDR;
    uint32_t SYSTEM_APERTURE_HIGH_ADDR;
    uint32_t SYSTEM_APERTURE_DEFAULT_ADDR_LSB;
    uint32_t SYSTEM_APERTURE_DEFAULT_ADDR_MSB;

    uint32_t MX_L1_TLB_CNTL;

    uint32_t CONTEXT0_CNTL;
    uint32_t CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32;
    uint32_t CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32;
    uint32_t CONTEXT0_PAGE_TABLE_START_ADDR_LO32;
    uint32_t CONTEXT0_PAGE_TABLE_START_ADDR_HI32;
    uint32_t CONTEXT0_PAGE_TABLE_END_ADDR_LO32;
    uint32_t CONTEXT0_PAGE_TABLE_END_ADDR_HI32;

    uint32_t CONTEXT1_CNTL;
    uint32_t CONTEXT1_PAGE_TABLE_START_ADDR_LO32;
    uint32_t CONTEXT1_PAGE_TABLE_START_ADDR_HI32;
    uint32_t CONTEXT1_PAGE_TABLE_END_ADDR_LO32;
    uint32_t CONTEXT1_PAGE_TABLE_END_ADDR_HI32;

    uint32_t L2_CNTL;
    uint32_t L2_CNTL2;
    uint32_t L2_CNTL3;
    uint32_t L2_CNTL4;
    uint32_t L2_CNTL5;

    uint32_t L2_PROTECTION_FAULT_CNTL2;
    uint32_t L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32;
    uint32_t L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32;

    uint32_t L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32;
    uint32_t L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32;
    uint32_t L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32;
    uint32_t L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32;
    uint32_t L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32;
    uint32_t L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32;

    // Invalidation engine 0 (the only engine the kext drives).
    uint32_t INVALIDATE_ENG0_SEM;
    uint32_t INVALIDATE_ENG0_REQ;
    uint32_t INVALIDATE_ENG0_ACK;
    uint32_t INVALIDATE_ENG0_ADDR_RANGE_LO32;
    uint32_t INVALIDATE_ENG0_ADDR_RANGE_HI32;

    const char *ip_version;
};

// MMHUB 4.1.0 — Navi 48 / gfx1201. Transcribed independently from
// mmhub_4_1_0_offset.h; amdgpu_mmhub.cpp static_asserts every field against the
// MMHUBRegs constants in amdgpu_ip.h, so selecting this table is provably
// identical to the current Navi48 behaviour.
//
// `inline constexpr`, not plain `constexpr`: namespace-scope constexpr implies
// const implies INTERNAL linkage, which gives every translation unit its own
// private copy. mmhubRegsForVersion() returns a pointer to the copy inside
// amdgpu_mmhub.cpp, so any caller comparing that against its own kMmhub4_1_0
// compares two different addresses and fails. The values match either way, so
// this never bit in the kext — but it makes pointer identity a lie, and
// tests/asic_profile_test.cpp compares identity on purpose.
inline constexpr MmhubRegs kMmhub4_1_0 = {
    0x0554, 0x0555, 0x04c7,
    0x0556, 0x0557, 0x0558,
    0x0559, 0x055a, 0x04c8, 0x04c9,
    0x055b,
    0x0564, 0x05cf, 0x05d0, 0x05ef, 0x05f0, 0x060f, 0x0610,
    0x0565, 0x05f1, 0x05f2, 0x0611, 0x0612,
    0x04e4, 0x04e5, 0x04e6, 0x04fd, 0x0503,
    0x04ed, 0x04f4, 0x04f5,
    0x04f7, 0x04f8, 0x04f9, 0x04fa, 0x04fb, 0x04fc,
    0x0575, 0x0587, 0x0599, 0x05ab, 0x05ac,
    "4.1.0",
};

// MMHUB 3.0.0 — Navi 33 / gfx1102 (mmhub_3_0_0_offset.h).
inline constexpr MmhubRegs kMmhub3_0_0 = {
    0x08ec, 0x08ed, 0x08d7,
    0x08ee, 0x08ef, 0x08f0,
    0x08f1, 0x08f2, 0x08d8, 0x08d9,
    0x08f3,
    0x0740, 0x07ab, 0x07ac, 0x07cb, 0x07cc, 0x07eb, 0x07ec,
    0x0741, 0x07cd, 0x07ce, 0x07ed, 0x07ee,
    0x0700, 0x0701, 0x0702, 0x0718, 0x071e,
    0x0709, 0x070f, 0x0710,
    0x0712, 0x0713, 0x0714, 0x0715, 0x0716, 0x0717,
    0x0751, 0x0763, 0x0775, 0x0787, 0x0788,
    "3.0.0",
};

// MMHUB 3.0.2 — what a real RX 7600 (0x7480) reports from on-die discovery
// (mmhub_3_0_2_offset.h). NOT the same map as 3.0.0: every register this kext
// touches sits exactly 0x80 dwords lower (FB_LOCATION_BASE 0x086c, not 0x08ec).
// gmc_v11_0.c gives 3.0.2 its own mmhub_v3_0_2_funcs for this reason. Reading
// the 3.0.0 offsets on a 3.0.2 part returns an unrelated register, which is
// exactly the silent-garbage vram_start this file exists to prevent.
// Generated field-by-field from the upstream header, same order as MmhubRegs.
inline constexpr MmhubRegs kMmhub3_0_2 = {
    0x086c, 0x086d, 0x0857,
    0x086e, 0x086f, 0x0870,
    0x0871, 0x0872, 0x0858, 0x0859,
    0x0873,
    0x06c0, 0x072b, 0x072c, 0x074b, 0x074c, 0x076b, 0x076c,
    0x06c1, 0x074d, 0x074e, 0x076d, 0x076e,
    0x0680, 0x0681, 0x0682, 0x0698, 0x069e,
    0x0689, 0x068f, 0x0690,
    0x0692, 0x0693, 0x0694, 0x0695, 0x0696, 0x0697,
    0x06d1, 0x06e3, 0x06f5, 0x0707, 0x0708,
    "3.0.2",
};

// Picks the table for a harvested MMHUB version. 3.0.0 and 3.0.1 share one map
// (checked field-by-field against mmhub_3_0_0_offset.h and mmhub_3_0_1_offset.h);
// 3.0.2 has its own. Returns nullptr for a version we have no transcribed map
// for, which callers must treat as fatal rather than falling back to a guess.
const MmhubRegs *mmhubRegsForVersion(const IPVersion &v);

// Process-wide selection, made once after discovery. Defaults to nullptr so a
// caller that runs before discovery fails loudly instead of reading 4.1.0
// offsets off a 3.0.0 part.
void                  setMmhubRegs(const MmhubRegs *r);
const MmhubRegs      *mmhubRegs();
bool                  mmhubRegsResolved();

} // namespace amdgpu