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

// Picks the table for a harvested MMHUB version. MMHUB 3.0.1 and 3.0.2 are
// gfx11.5/11.7 APU parts and share the 3.0.0 offsets for everything this kext
// touches; treating them as 3.0.0 keeps one table instead of three. Returns
// nullptr for a version we have no transcribed map for, which callers must
// treat as fatal rather than falling back to a guess.
const MmhubRegs *mmhubRegsForVersion(const IPVersion &v);

// Process-wide selection, made once after discovery. Defaults to nullptr so a
// caller that runs before discovery fails loudly instead of reading 4.1.0
// offsets off a 3.0.0 part.
void                  setMmhubRegs(const MmhubRegs *r);
const MmhubRegs      *mmhubRegs();
bool                  mmhubRegsResolved();

} // namespace amdgpu