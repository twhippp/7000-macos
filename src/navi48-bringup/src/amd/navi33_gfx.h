//
//  navi33_gfx.h — Navi 33 (RX 7600, GC 11.0.2) stage 4: the first command.
//
//  After stage 3 (all GFX firmware loaded, RLC autoload complete), this does the two
//  things Linux does before its first SDMA ring test, and the ring test itself:
//
//    1. GFXHUB VMID 0 (gfxhub_v3_0_gart_enable): system aperture over the framebuffer,
//       L1 TLB / L2 cache, CONTEXT0 with a small empty GART, identity aperture closed,
//       invalidation ranges, one TLB flush. VMIDs 1-15 are left alone (nothing uses them).
//    2. SDMA0 queue 0 (sdma_v6_0_start + sdma_v6_0_gfx_resume_instance): unhalt, ring in
//       VRAM, rptr write-back, wptr poll memory, no doorbell (MMIO wptr, like Linux without
//       doorbells).
//    3. sdma_v6_0_ring_test_ring: one WRITE_LINEAR of 0xDEADBEEF to a VRAM slot.
//
//  Register offsets and field positions are gc_11_0_0_offset.h / gc_11_0_0_sh_mask.h
//  (Linux master); nothing here is shared with the GFX12 modules. Everything lives in a
//  64 KiB slot of the bring-up span (vramBase + kN33GfxVramOffset), above the PSP's
//  fw_buf, so the console framebuffer and the PSP's buffers are not touched.
//
#pragma once

#include <stdint.h>
#include <IOKit/IOReturn.h>
#include "amdgpu_regs.h"

namespace amdgpu {

constexpr uint64_t kN33GfxVramOffset = 0x1A00000;   // from dev.vramBase (26 MiB; fw_buf is 16-24 MiB)
constexpr uint64_t kN33GfxVramSize   = 0x10000;     // 64 KiB
constexpr uint32_t kN33SdmaTestValue = 0xDEADBEEFu;

struct N33GfxReport {
	// GFXHUB
	uint64_t fbStart, fbEnd, fbOffset;   // GCMC_VM_FB_LOCATION_BASE/TOP << 24, GCMC_VM_FB_OFFSET << 24
	uint32_t prevL1TlbCntl;              // GCMC_VM_MX_L1_TLB_CNTL as found
	uint32_t prevSysApLow, prevSysApHigh;
	bool     tlbFlushAcked;
	uint32_t faultStatus;                // GCVM_L2_PROTECTION_FAULT_STATUS after the test
	uint64_t faultAddr;
	// SDMA0
	uint32_t f32CntlBefore, f32CntlAfter;
	uint32_t statusReg;                  // SDMA0_STATUS_REG after the test
	uint32_t rbRptr;                     // SDMA0_QUEUE0_RB_RPTR (bytes)
	uint64_t rptrWriteback;              // what the engine wrote to the rptr slot
	uint32_t testValue;                  // the slot after the test
	uint32_t waitedUs;
	bool     passed;
	const char *stage;                   // where it stopped
};

// Programs GFXHUB VMID 0. Fails (writes nothing) when the GC FB location does not match
// dev.vramMcBase — every address this module hands the engine is derived from that.
kern_return_t navi33_gfxhub_enable(DeviceContext &dev, N33GfxReport &rep);

// Starts SDMA0 queue 0 and runs the WRITE_LINEAR ring test. Needs navi33_gfxhub_enable first.
kern_return_t navi33_sdma_ring_test(DeviceContext &dev, N33GfxReport &rep, uint32_t timeoutUs = 100000);

} // namespace amdgpu
