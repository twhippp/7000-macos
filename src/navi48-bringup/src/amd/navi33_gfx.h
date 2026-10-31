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

#include <stddef.h>
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
	uint32_t tlbAckRaw;                  // GCVM_INVALIDATE_ENG17_ACK as last read
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

// ---- stage 5: the graphics command processor (CP) ----
//
// Linux gfx_v11_0_hw_init for PSP-loaded RS64 firmware, with amdgpu_async_gfx_ring=0 (the
// legacy kernel ring, which Linux still supports): config_gfx_rs64 (program-counter start
// from the PFP/ME/MEC headers + pipe resets), cp_gfx_resume (RB0 in VRAM, VMID 0, no
// doorbell), cp_gfx_enable (unhalt PFP/ME), then gfx_v11_0_ring_test_ring: SET_UCONFIG_REG
// SCRATCH_REG0 = 0xDEADBEEF, read back over MMIO.
// Not yet ported (not needed for that test): constants_init, rlc_resume's clear-state buffer
// + SRM, the clear-state preamble in cp_gfx_start, KIQ/KCQ, golden registers.
struct N33CpFw {   // gfx_firmware_header_v2_0 ucode_start_addr_lo/hi of each RS64 image
	uint32_t pfpLo, pfpHi, meLo, meHi, mecLo, mecHi;
};

struct N33CpReport {
	uint32_t meCntlBefore, meCntlAfter;
	uint32_t cpStat;            // CP_STAT after unhalt (0 = idle)
	bool     cpIdleAfterUnhalt;
	uint32_t scratch;           // SCRATCH_REG0 after the test
	uint32_t rbRptr;            // CP_RB0_RPTR (dwords)
	uint64_t rptrWriteback;
	uint32_t grbmStatus;
	uint32_t waitedUs;
	bool     passed;
	const char *stage;
};

// Reads the start addresses out of the three firmware files; false if any header is bad.
bool navi33_cp_fw_from_blobs(const uint8_t *pfp, size_t pfpSize, const uint8_t *me, size_t meSize,
                             const uint8_t *mec, size_t mecSize, N33CpFw &out);

// Needs navi33_gfxhub_enable (VMID 0) first.
kern_return_t navi33_cp_gfx_ring_test(DeviceContext &dev, const N33CpFw &fw, N33CpReport &rep,
                                      uint32_t timeoutUs = 100000);

// ---- stage 6: indirect buffers (the path all real work takes) ----
//
// gfx_v11_0_ring_test_ib + sdma_v6_0_ring_test_ib: each ring gets an INDIRECT_BUFFER
// packet pointing at a small command buffer in VRAM, and that buffer writes 0xDEADBEEF
// into a VRAM slot. Continues each ring where stages 4 and 5 left it.
struct N33IbReport {
	bool     sdmaPassed, cpPassed;
	uint32_t sdmaValue, cpValue;
	uint32_t sdmaWaitedUs, cpWaitedUs;
	uint32_t sdmaRptr, cpRptr;
	uint32_t faultStatus;
	const char *stage;
};
kern_return_t navi33_ib_tests(DeviceContext &dev, N33IbReport &rep, uint32_t timeoutUs = 100000);

// ---- stage 7: completion fences + a real copy ----
//
// The ring tests so far proved a write lands. Real work needs two more things:
//  * fences: the engine writing a sequence number when it has finished — RELEASE_MEM on the
//    gfx ring (gfx_v11_0_ring_emit_fence, with the cache flush), FENCE on SDMA
//    (sdma_v6_0_ring_emit_fence);
//  * bulk copies: SDMA COPY_LINEAR of 4 KiB inside an IB (sdma_v6_0_emit_copy_buffer).
// Continues both rings where stage 6 left them.
constexpr uint32_t kN33FenceSeq = 7;
struct N33FenceReport {
	bool     sdmaPassed, cpPassed;
	uint32_t sdmaFence, cpFence;     // the fence slots after the test (want kN33FenceSeq)
	uint32_t copyBadDwords;          // destination dwords that differ from the source
	uint32_t sdmaWaitedUs, cpWaitedUs;
	const char *stage;
};
kern_return_t navi33_fence_copy_tests(DeviceContext &dev, N33FenceReport &rep, uint32_t timeoutUs = 100000);

// ---- stage 8: real data — the live screen ----
//
// The copy engine works on the actual console framebuffer the firmware left in VRAM:
//  1. copies the whole screen to a backup area in VRAM (real pixels, verified by the CPU),
//  2. paints a white band over the top 1/16 of the screen (CONSTANT_FILL) — visible,
//  3. holds it for holdMs, then copies the band back from the backup (restored, verified).
// Each step is one IB + FENCE on SDMA0, continuing the ring where stage 7 left it.
struct N33ScreenReport {
	uint64_t fbOff, fbLen, backupOff, bandLen;
	uint32_t backupMismatches;   // sampled dwords where backup != screen after step 1
	uint32_t bandMismatches;     // sampled band dwords that are not white after step 2
	uint32_t restoreMismatches;  // sampled band dwords != backup after step 3
	uint32_t backupUs, bandUs, restoreUs;
	bool     passed;
	const char *stage;
};
kern_return_t navi33_screen_test(DeviceContext &dev, uint64_t fbOff, uint64_t fbLen, N33ScreenReport &rep,
                                 uint32_t holdMs = 3000, uint32_t timeoutUs = 2000000);

} // namespace amdgpu
