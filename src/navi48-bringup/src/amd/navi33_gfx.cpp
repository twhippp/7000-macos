//
//  navi33_gfx.cpp — see navi33_gfx.h. Ported from Linux master gfxhub_v3_0.c and
//  sdma_v6_0.c; every offset/field below is quoted from gc_11_0_0_offset.h /
//  gc_11_0_0_sh_mask.h / gc_11_0_0_default.h.
//
#include "navi33_gfx.h"
#include "amdgpu_log.h"
#include "amdgpu_ucode_psp.h"

namespace amdgpu {

namespace {

// ---- gc_11_0_0_offset.h — all GFXHUB registers here are BASE_IDX 0 ----
namespace R {
	constexpr uint32_t GCMC_VM_FB_OFFSET                          = 0x15a7;
	constexpr uint32_t GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB   = 0x15a8;
	constexpr uint32_t GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB   = 0x15a9;
	constexpr uint32_t GCVM_L2_CNTL                               = 0x15bc;
	constexpr uint32_t GCVM_L2_CNTL2                              = 0x15bd;
	constexpr uint32_t GCVM_L2_CNTL3                              = 0x15be;
	constexpr uint32_t GCVM_L2_PROTECTION_FAULT_CNTL2             = 0x15c5;
	constexpr uint32_t GCVM_L2_PROTECTION_FAULT_STATUS            = 0x15c8;
	constexpr uint32_t GCVM_L2_PROTECTION_FAULT_ADDR_LO32         = 0x15c9;
	constexpr uint32_t GCVM_L2_PROTECTION_FAULT_ADDR_HI32         = 0x15ca;
	constexpr uint32_t GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32  = 0x15ce;
	constexpr uint32_t GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32  = 0x15cf;
	constexpr uint32_t GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32 = 0x15d0;
	constexpr uint32_t GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32 = 0x15d1;
	constexpr uint32_t GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32     = 0x15d2;
	constexpr uint32_t GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32     = 0x15d3;
	constexpr uint32_t GCVM_L2_CNTL4                              = 0x15d4;
	constexpr uint32_t GCVM_L2_CNTL5                              = 0x15da;
	constexpr uint32_t GCMC_VM_FB_LOCATION_BASE                   = 0x1678;
	constexpr uint32_t GCMC_VM_FB_LOCATION_TOP                    = 0x1679;
	constexpr uint32_t GCMC_VM_AGP_TOP                            = 0x167a;
	constexpr uint32_t GCMC_VM_AGP_BOT                            = 0x167b;
	constexpr uint32_t GCMC_VM_AGP_BASE                           = 0x167c;
	constexpr uint32_t GCMC_VM_SYSTEM_APERTURE_LOW_ADDR           = 0x167d;
	constexpr uint32_t GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR          = 0x167e;
	constexpr uint32_t GCMC_VM_MX_L1_TLB_CNTL                     = 0x167f;
	constexpr uint32_t GCVM_CONTEXT0_CNTL                         = 0x1688;
	constexpr uint32_t GCVM_INVALIDATE_ENG17_SEM                  = 0x16aa;
	constexpr uint32_t GCVM_INVALIDATE_ENG17_REQ                  = 0x16bc;
	constexpr uint32_t GCVM_INVALIDATE_ENG17_ACK                  = 0x16ce;
	constexpr uint32_t GCVM_INVALIDATE_ENG0_ADDR_RANGE_LO32       = 0x16cf;
	constexpr uint32_t GCVM_INVALIDATE_ENG0_ADDR_RANGE_HI32       = 0x16d0;
	constexpr uint32_t kEngAddrDistance                           = 2;   // ENG1_ADDR_RANGE_LO32 0x16d1 - 0x16cf
	constexpr uint32_t GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32    = 0x16f3;
	constexpr uint32_t GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32    = 0x16f4;
	constexpr uint32_t GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32   = 0x1713;
	constexpr uint32_t GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32   = 0x1714;
	constexpr uint32_t GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32     = 0x1733;
	constexpr uint32_t GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32     = 0x1734;

	// SDMA0 — BASE_IDX 0, except F32_CNTL (0x5880..0x589a range, BASE_IDX 1;
	// sdma_v6_0_get_reg_offset)
	constexpr uint32_t SDMA0_CNTL                       = 0x001c;
	constexpr uint32_t SDMA0_SEM_WAIT_FAIL_TIMER_CNTL   = 0x0022;
	constexpr uint32_t SDMA0_STATUS_REG                 = 0x0025;
	constexpr uint32_t SDMA0_WATCHDOG_CNTL              = 0x002e;
	constexpr uint32_t SDMA0_UTCL1_CNTL                 = 0x003c;
	constexpr uint32_t SDMA0_UTCL1_PAGE                 = 0x003f;
	constexpr uint32_t SDMA0_QUEUE0_RB_CNTL             = 0x0080;
	constexpr uint32_t SDMA0_QUEUE0_RB_BASE             = 0x0081;
	constexpr uint32_t SDMA0_QUEUE0_RB_BASE_HI          = 0x0082;
	constexpr uint32_t SDMA0_QUEUE0_RB_RPTR             = 0x0083;
	constexpr uint32_t SDMA0_QUEUE0_RB_RPTR_HI          = 0x0084;
	constexpr uint32_t SDMA0_QUEUE0_RB_WPTR             = 0x0085;
	constexpr uint32_t SDMA0_QUEUE0_RB_WPTR_HI          = 0x0086;
	constexpr uint32_t SDMA0_QUEUE0_RB_RPTR_ADDR_HI     = 0x0088;
	constexpr uint32_t SDMA0_QUEUE0_RB_RPTR_ADDR_LO     = 0x0089;
	constexpr uint32_t SDMA0_QUEUE0_IB_CNTL             = 0x008a;
	constexpr uint32_t SDMA0_QUEUE0_DOORBELL            = 0x0092;
	constexpr uint32_t SDMA0_QUEUE0_DOORBELL_OFFSET     = 0x00ab;
	constexpr uint32_t SDMA0_QUEUE0_RB_WPTR_POLL_ADDR_HI = 0x00b2;
	constexpr uint32_t SDMA0_QUEUE0_RB_WPTR_POLL_ADDR_LO = 0x00b3;
	constexpr uint32_t SDMA0_QUEUE0_MINOR_PTR_UPDATE    = 0x00b5;
	constexpr uint32_t SDMA0_F32_CNTL                   = 0x589a;   // BASE_IDX 1
}

// gc_11_0_0_default.h
constexpr uint32_t kGCVM_L2_CNTL3_DEFAULT = 0x80120007u;
constexpr uint32_t kGCVM_L2_CNTL4_DEFAULT = 0x000000c1u;
constexpr uint32_t kGCVM_L2_CNTL5_DEFAULT = 0x00003fe0u;

// field = (shift, mask) from gc_11_0_0_sh_mask.h
struct F { uint32_t shift, mask; };
inline uint32_t setf(uint32_t v, F f, uint32_t x) { return (v & ~f.mask) | ((x << f.shift) & f.mask); }

constexpr F L1TLB_ENABLE_L1_TLB                 { 0x0, 0x00000001 };
constexpr F L1TLB_SYSTEM_ACCESS_MODE            { 0x3, 0x00000018 };
constexpr F L1TLB_SYSTEM_APERTURE_UNMAPPED_ACCESS { 0x5, 0x00000020 };
constexpr F L1TLB_ENABLE_ADVANCED_DRIVER_MODEL  { 0x6, 0x00000040 };
constexpr F L1TLB_ECO_BITS                      { 0x7, 0x00000780 };
constexpr F L1TLB_MTYPE                         { 0xb, 0x00003800 };
constexpr uint32_t kMTYPE_UC = 3;                                    // soc21_enum.h

constexpr F L2_ENABLE_L2_CACHE                  { 0x0,  0x00000001 };
constexpr F L2_ENABLE_L2_FRAGMENT_PROCESSING    { 0x1,  0x00000002 };
constexpr F L2_L2_PDE0_CACHE_TAG_GENERATION_MODE { 0x8, 0x00000100 };
constexpr F L2_ENABLE_DEFAULT_PAGE_OUT_TO_SYSTEM_MEMORY { 0xb, 0x00000800 };
constexpr F L2_PDE_FAULT_CLASSIFICATION         { 0x12, 0x00040000 };
constexpr F L2_CONTEXT1_IDENTITY_ACCESS_MODE    { 0x13, 0x00180000 };
constexpr F L2_IDENTITY_MODE_FRAGMENT_SIZE      { 0x15, 0x03E00000 };
constexpr F L2C2_INVALIDATE_ALL_L1_TLBS         { 0x0, 0x00000001 };
constexpr F L2C2_INVALIDATE_L2_CACHE            { 0x1, 0x00000002 };
constexpr F L2C3_BANK_SELECT                    { 0x0, 0x0000003F };
constexpr F L2C3_L2_CACHE_BIGK_FRAGMENT_SIZE    { 0xf, 0x000F8000 };
constexpr F L2C4_VMC_TAP_PDE_REQUEST_PHYSICAL   { 0x6, 0x00000040 };
constexpr F L2C4_VMC_TAP_PTE_REQUEST_PHYSICAL   { 0x7, 0x00000080 };
constexpr F L2C5_L2_CACHE_SMALLK_FRAGMENT_SIZE  { 0x0, 0x0000001F };
constexpr F PFC2_ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY { 0x12, 0x00040000 };
constexpr F CTX0_ENABLE_CONTEXT                 { 0x0, 0x00000001 };
constexpr F CTX0_PAGE_TABLE_DEPTH               { 0x1, 0x00000006 };
constexpr F CTX0_RETRY_PERMISSION_OR_INVALID_PAGE_FAULT { 0x7, 0x00000080 };

constexpr F INV_PER_VMID_INVALIDATE_REQ { 0x0,  0x0000FFFF };
constexpr F INV_FLUSH_TYPE              { 0x10, 0x00070000 };
constexpr F INV_INVALIDATE_L2_PTES      { 0x13, 0x00080000 };
constexpr F INV_INVALIDATE_L2_PDE0      { 0x14, 0x00100000 };
constexpr F INV_INVALIDATE_L2_PDE1      { 0x15, 0x00200000 };
constexpr F INV_INVALIDATE_L2_PDE2      { 0x16, 0x00400000 };
constexpr F INV_INVALIDATE_L1_PTES      { 0x17, 0x00800000 };

constexpr F RB_ENABLE               { 0x0,  0x00000001 };
constexpr F RB_SIZE                 { 0x1,  0x0000003E };
constexpr F RB_WPTR_POLL_ENABLE     { 0x8,  0x00000100 };
constexpr F RB_F32_WPTR_POLL_ENABLE { 0xb,  0x00000800 };
constexpr F RB_RPTR_WRITEBACK_ENABLE { 0xc, 0x00001000 };
constexpr F RB_PRIV                 { 0x17, 0x00800000 };
constexpr F IB_ENABLE               { 0x0,  0x00000001 };
constexpr F F32_HALT                { 0x0,  0x00000001 };
constexpr F F32_TH1_RESET           { 0xd,  0x00002000 };
constexpr F DOORBELL_ENABLE         { 0x1c, 0x10000000 };
constexpr F WATCHDOG_QUEUE_HANG_COUNT { 0x0, 0x000000FF };
constexpr F UTCL1_REDO_DELAY        { 0x0,  0x0000001F };
constexpr F UTCL1_RESP_MODE         { 0x9,  0x00000600 };
constexpr uint32_t kUTCL1_PAGE_LLC_NOALLOC = 0x01000000u;
constexpr uint32_t kCACHE_READ_POLICY_L2_DEFAULT  = 2;   // sdma_common.h: NOA
constexpr uint32_t kCACHE_WRITE_POLICY_L2_DEFAULT = 3;   // sdma_common.h: BYPASS

// sdma_v6_0_0_pkt_open.h
constexpr uint32_t kSDMA_OP_WRITE = 2, kSDMA_SUBOP_WRITE_LINEAR = 0;

// Layout of our 64 KiB slot (offsets from its start).
constexpr uint64_t kGartPtOff  = 0x0000;   // 4 KiB: 512 PTEs, all invalid
constexpr uint64_t kScratchOff = 0x1000;   // system-aperture default page
constexpr uint64_t kRingOff    = 0x2000;   // 4 KiB ring = 1024 dwords
constexpr uint32_t kRingBytes  = 0x1000;
static_assert(kRingBytes == (1u << 10) * 4, "RB_SIZE 10 below is log2(kRingBytes / 4)");
constexpr uint64_t kRptrOff    = 0x3000;   // 8-byte rptr write-back
constexpr uint64_t kWptrOff    = 0x3008;   // 8-byte wptr poll slot
constexpr uint64_t kTestOff    = 0x3100;   // the WRITE_LINEAR target

// GART: 2 MiB at the very top of the 48-bit MC space (Linux AMDGPU_GART_PLACEMENT_HIGH),
// far away from the framebuffer at 0x80_0000_0000. Nothing in this stage uses it.
constexpr uint64_t kGartSize  = 0x200000;
constexpr uint64_t kGartStart = 0x1000000000000ull - kGartSize;
constexpr uint64_t kGartEnd   = 0xFFFFFFFFFFFFull;

inline uint32_t gc0(const DeviceContext &d, uint32_t r) { return SOC15_REG_OFFSET_BIDX(d, IPBlock::GC, 0, r); }
inline uint32_t gc1(const DeviceContext &d, uint32_t r) { return SOC15_REG_OFFSET_BIDX(d, IPBlock::GC, 1, r); }

inline uint64_t slot(const DeviceContext &d) { return d.vramBase + kN33GfxVramOffset; }

} // namespace

kern_return_t navi33_gfxhub_enable(DeviceContext &dev, N33GfxReport &rep) {
	rep.stage = "gfxhub";
	if (!dev.ip.isResolved(IPBlock::GC, 0) || !dev.ip.isResolved(IPBlock::GC, 1)) { rep.stage = "gc-unresolved"; return kIOReturnNotReady; }
	if (!dev.bar0 || slot(dev) + kN33GfxVramSize > dev.vramLimit || slot(dev) + kN33GfxVramSize > dev.bar0Size) {
		rep.stage = "no-vram-slot"; return kIOReturnNoSpace;
	}

	// gfxhub_v3_0_get_fb_location / get_mc_fb_offset
	const uint32_t fbBase = RREG32(dev, gc0(dev, R::GCMC_VM_FB_LOCATION_BASE)) & 0x00FFFFFFu;
	const uint32_t fbTop  = RREG32(dev, gc0(dev, R::GCMC_VM_FB_LOCATION_TOP))  & 0x00FFFFFFu;
	rep.fbStart  = (uint64_t)fbBase << 24;
	rep.fbEnd    = ((uint64_t)fbTop << 24) | 0xFFFFFFull;
	rep.fbOffset = (uint64_t)RREG32(dev, gc0(dev, R::GCMC_VM_FB_OFFSET)) << 24;
	rep.prevL1TlbCntl = RREG32(dev, gc0(dev, R::GCMC_VM_MX_L1_TLB_CNTL));
	rep.prevSysApLow  = RREG32(dev, gc0(dev, R::GCMC_VM_SYSTEM_APERTURE_LOW_ADDR));
	rep.prevSysApHigh = RREG32(dev, gc0(dev, R::GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR));
	if (rep.fbStart != dev.vramMcBase || rep.fbEnd <= rep.fbStart) {
		UCODE_LOG("navi33-gfx: GC FB location %#llx-%#llx does not match vramMcBase %#llx — nothing written",
		          (unsigned long long)rep.fbStart, (unsigned long long)rep.fbEnd, (unsigned long long)dev.vramMcBase);
		rep.stage = "fb-mismatch"; return kIOReturnNotReady;
	}
	auto mc2pa = [&](uint64_t vramOff) { return rep.fbOffset + vramOff; };   // amdgpu_gmc_vram_mc2pa

	bar0_memset_vram(dev, slot(dev), 0u, kN33GfxVramSize);

	// init_gart_aperture_regs: PD base = PA of the (empty) table | VALID (amdgpu_gmc_pd_addr).
	const uint64_t pt = mc2pa(slot(dev) + kGartPtOff) | 1ull;
	WREG32(dev, gc0(dev, R::GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32), (uint32_t)pt);
	WREG32(dev, gc0(dev, R::GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32), (uint32_t)(pt >> 32));
	WREG32(dev, gc0(dev, R::GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32), (uint32_t)(kGartStart >> 12));
	WREG32(dev, gc0(dev, R::GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32), (uint32_t)(kGartStart >> 44));
	WREG32(dev, gc0(dev, R::GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32), (uint32_t)(kGartEnd >> 12));
	WREG32(dev, gc0(dev, R::GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32), (uint32_t)(kGartEnd >> 44));

	// init_system_aperture_regs. AGP off (amdgpu_gmc_set_agp_default: start 0xffffffffffff,
	// end 0), so the aperture is exactly the framebuffer.
	WREG32(dev, gc0(dev, R::GCMC_VM_AGP_BASE), 0);
	WREG32(dev, gc0(dev, R::GCMC_VM_AGP_BOT), (uint32_t)(0xFFFFFFFFFFFFull >> 24));
	WREG32(dev, gc0(dev, R::GCMC_VM_AGP_TOP), 0);
	WREG32(dev, gc0(dev, R::GCMC_VM_SYSTEM_APERTURE_LOW_ADDR),  (uint32_t)(rep.fbStart >> 18));
	WREG32(dev, gc0(dev, R::GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR), (uint32_t)(rep.fbEnd >> 18));
	const uint64_t scratch = mc2pa(slot(dev) + kScratchOff);
	WREG32(dev, gc0(dev, R::GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB), (uint32_t)(scratch >> 12));
	WREG32(dev, gc0(dev, R::GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB), (uint32_t)(scratch >> 44));
	// [DEVIATION] GCVM_L2_PROTECTION_FAULT_DEFAULT_ADDR is left as found: Linux points it at a
	// system-memory dummy page, and this stage has none. It only matters on a fault.
	uint32_t t = RREG32(dev, gc0(dev, R::GCVM_L2_PROTECTION_FAULT_CNTL2));
	t = setf(t, PFC2_ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY, 1);
	WREG32(dev, gc0(dev, R::GCVM_L2_PROTECTION_FAULT_CNTL2), t);

	// init_tlb_regs
	t = rep.prevL1TlbCntl;
	t = setf(t, L1TLB_ENABLE_L1_TLB, 1);
	t = setf(t, L1TLB_SYSTEM_ACCESS_MODE, 3);
	t = setf(t, L1TLB_ENABLE_ADVANCED_DRIVER_MODEL, 1);
	t = setf(t, L1TLB_SYSTEM_APERTURE_UNMAPPED_ACCESS, 0);
	t = setf(t, L1TLB_ECO_BITS, 0);
	t = setf(t, L1TLB_MTYPE, kMTYPE_UC);
	WREG32(dev, gc0(dev, R::GCMC_VM_MX_L1_TLB_CNTL), t);

	// init_cache_regs
	t = RREG32(dev, gc0(dev, R::GCVM_L2_CNTL));
	t = setf(t, L2_ENABLE_L2_CACHE, 1);
	t = setf(t, L2_ENABLE_L2_FRAGMENT_PROCESSING, 0);
	t = setf(t, L2_ENABLE_DEFAULT_PAGE_OUT_TO_SYSTEM_MEMORY, 1);
	t = setf(t, L2_L2_PDE0_CACHE_TAG_GENERATION_MODE, 0);
	t = setf(t, L2_PDE_FAULT_CLASSIFICATION, 0);
	t = setf(t, L2_CONTEXT1_IDENTITY_ACCESS_MODE, 1);
	t = setf(t, L2_IDENTITY_MODE_FRAGMENT_SIZE, 0);
	WREG32(dev, gc0(dev, R::GCVM_L2_CNTL), t);
	t = RREG32(dev, gc0(dev, R::GCVM_L2_CNTL2));
	t = setf(t, L2C2_INVALIDATE_ALL_L1_TLBS, 1);
	t = setf(t, L2C2_INVALIDATE_L2_CACHE, 1);
	WREG32(dev, gc0(dev, R::GCVM_L2_CNTL2), t);
	t = setf(setf(kGCVM_L2_CNTL3_DEFAULT, L2C3_BANK_SELECT, 9), L2C3_L2_CACHE_BIGK_FRAGMENT_SIZE, 6);
	WREG32(dev, gc0(dev, R::GCVM_L2_CNTL3), t);
	t = setf(setf(kGCVM_L2_CNTL4_DEFAULT, L2C4_VMC_TAP_PDE_REQUEST_PHYSICAL, 0), L2C4_VMC_TAP_PTE_REQUEST_PHYSICAL, 0);
	WREG32(dev, gc0(dev, R::GCVM_L2_CNTL4), t);
	WREG32(dev, gc0(dev, R::GCVM_L2_CNTL5), setf(kGCVM_L2_CNTL5_DEFAULT, L2C5_L2_CACHE_SMALLK_FRAGMENT_SIZE, 0));

	// enable_system_domain
	t = RREG32(dev, gc0(dev, R::GCVM_CONTEXT0_CNTL));
	t = setf(t, CTX0_ENABLE_CONTEXT, 1);
	t = setf(t, CTX0_PAGE_TABLE_DEPTH, 0);
	t = setf(t, CTX0_RETRY_PERMISSION_OR_INVALID_PAGE_FAULT, 0);
	WREG32(dev, gc0(dev, R::GCVM_CONTEXT0_CNTL), t);

	// disable_identity_aperture
	WREG32(dev, gc0(dev, R::GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32), 0xFFFFFFFFu);
	WREG32(dev, gc0(dev, R::GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32), 0x0000000Fu);
	WREG32(dev, gc0(dev, R::GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32), 0);
	WREG32(dev, gc0(dev, R::GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32), 0);
	WREG32(dev, gc0(dev, R::GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32), 0);
	WREG32(dev, gc0(dev, R::GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32), 0);

	// (setup_vmid_config skipped: VMIDs 1-15 have no user yet.)

	// program_invalidation
	for (uint32_t i = 0; i < 18; i++) {
		WREG32(dev, gc0(dev, R::GCVM_INVALIDATE_ENG0_ADDR_RANGE_LO32) + i * R::kEngAddrDistance, 0xFFFFFFFFu);
		WREG32(dev, gc0(dev, R::GCVM_INVALIDATE_ENG0_ADDR_RANGE_HI32) + i * R::kEngAddrDistance, 0x1Fu);
	}

	// gmc_v11_0_flush_gpu_tlb(vmid 0, GFXHUB) over MMIO: engine 17, no semaphore on GFXHUB.
	amdgpu_hdp_flush(dev);
	uint32_t req = 0;
	req = setf(req, INV_PER_VMID_INVALIDATE_REQ, 1u << 0);
	req = setf(req, INV_FLUSH_TYPE, 0);
	req = setf(req, INV_INVALIDATE_L2_PTES, 1);
	req = setf(req, INV_INVALIDATE_L2_PDE0, 1);
	req = setf(req, INV_INVALIDATE_L2_PDE1, 1);
	req = setf(req, INV_INVALIDATE_L2_PDE2, 1);
	req = setf(req, INV_INVALIDATE_L1_PTES, 1);
	WREG32(dev, gc0(dev, R::GCVM_INVALIDATE_ENG17_REQ), req);
	rep.tlbFlushAcked = false;
	for (uint32_t us = 0; us < 100000; us += 10) {
		rep.tlbAckRaw = RREG32(dev, gc0(dev, R::GCVM_INVALIDATE_ENG17_ACK));
		if (rep.tlbAckRaw & 1u) { rep.tlbFlushAcked = true; break; }
		IODelay(10);
	}
	(void)R::GCVM_INVALIDATE_ENG17_SEM;

	UCODE_LOG("navi33-gfx: GFXHUB VMID0 up — FB %#llx-%#llx offset %#llx, L1_TLB %#x -> %#x, TLB flush %s",
	          (unsigned long long)rep.fbStart, (unsigned long long)rep.fbEnd, (unsigned long long)rep.fbOffset,
	          rep.prevL1TlbCntl, RREG32(dev, gc0(dev, R::GCMC_VM_MX_L1_TLB_CNTL)),
	          rep.tlbFlushAcked ? "acked" : "NOT acked");
	rep.stage = "gfxhub-ok";
	return kIOReturnSuccess;
}

kern_return_t navi33_sdma_ring_test(DeviceContext &dev, N33GfxReport &rep, uint32_t timeoutUs) {
	rep.stage = "sdma";
	const uint64_t base   = slot(dev);
	const uint64_t ringMc = dev.vramMC(base + kRingOff);
	const uint64_t rptrMc = dev.vramMC(base + kRptrOff);
	const uint64_t wptrMc = dev.vramMC(base + kWptrOff);
	const uint64_t testMc = dev.vramMC(base + kTestOff);
	auto sd = [&](uint32_t r) { return gc0(dev, r); };

	rep.f32CntlBefore = RREG32(dev, gc1(dev, R::SDMA0_F32_CNTL));

	// sdma_v6_0_start (PSP load): unhalt the ME, then resume the ring.
	// [DEVIATION] ctxempty_int_enable is skipped: no interrupt handler exists yet.
	uint32_t t = setf(rep.f32CntlBefore, F32_HALT, 0);
	WREG32(dev, gc1(dev, R::SDMA0_F32_CNTL), t);

	// sdma_v6_0_gfx_resume_instance(0, restore=false)
	WREG32(dev, sd(R::SDMA0_SEM_WAIT_FAIL_TIMER_CNTL), 0);
	uint32_t rb = RREG32(dev, sd(R::SDMA0_QUEUE0_RB_CNTL));
	rb = setf(rb, RB_SIZE, 10);                    // order_base_2(1024 dwords)
	rb = setf(rb, RB_PRIV, 1);
	WREG32(dev, sd(R::SDMA0_QUEUE0_RB_CNTL), rb);
	WREG32(dev, sd(R::SDMA0_QUEUE0_RB_RPTR), 0);
	WREG32(dev, sd(R::SDMA0_QUEUE0_RB_RPTR_HI), 0);
	WREG32(dev, sd(R::SDMA0_QUEUE0_RB_WPTR), 0);
	WREG32(dev, sd(R::SDMA0_QUEUE0_RB_WPTR_HI), 0);
	WREG32(dev, sd(R::SDMA0_QUEUE0_RB_WPTR_POLL_ADDR_LO), (uint32_t)wptrMc);
	WREG32(dev, sd(R::SDMA0_QUEUE0_RB_WPTR_POLL_ADDR_HI), (uint32_t)(wptrMc >> 32));
	WREG32(dev, sd(R::SDMA0_QUEUE0_RB_RPTR_ADDR_HI), (uint32_t)(rptrMc >> 32));
	WREG32(dev, sd(R::SDMA0_QUEUE0_RB_RPTR_ADDR_LO), (uint32_t)rptrMc & 0xFFFFFFFCu);
	rb = setf(rb, RB_RPTR_WRITEBACK_ENABLE, 1);
	rb = setf(rb, RB_WPTR_POLL_ENABLE, 0);
	rb = setf(rb, RB_F32_WPTR_POLL_ENABLE, 1);
	WREG32(dev, sd(R::SDMA0_QUEUE0_RB_BASE), (uint32_t)(ringMc >> 8));
	WREG32(dev, sd(R::SDMA0_QUEUE0_RB_BASE_HI), (uint32_t)(ringMc >> 40));
	WREG32(dev, sd(R::SDMA0_QUEUE0_MINOR_PTR_UPDATE), 1);
	WREG32(dev, sd(R::SDMA0_QUEUE0_RB_WPTR), 0);
	WREG32(dev, sd(R::SDMA0_QUEUE0_RB_WPTR_HI), 0);
	t = setf(RREG32(dev, sd(R::SDMA0_QUEUE0_DOORBELL)), DOORBELL_ENABLE, 0);
	WREG32(dev, sd(R::SDMA0_QUEUE0_DOORBELL), t);
	WREG32(dev, sd(R::SDMA0_QUEUE0_DOORBELL_OFFSET), RREG32(dev, sd(R::SDMA0_QUEUE0_DOORBELL_OFFSET)));
	WREG32(dev, sd(R::SDMA0_QUEUE0_MINOR_PTR_UPDATE), 0);
	t = setf(RREG32(dev, sd(R::SDMA0_WATCHDOG_CNTL)), WATCHDOG_QUEUE_HANG_COUNT, 1);
	WREG32(dev, sd(R::SDMA0_WATCHDOG_CNTL), t);
	t = RREG32(dev, sd(R::SDMA0_UTCL1_CNTL));
	t = setf(t, UTCL1_RESP_MODE, 3);
	t = setf(t, UTCL1_REDO_DELAY, 9);
	WREG32(dev, sd(R::SDMA0_UTCL1_CNTL), t);
	t = RREG32(dev, sd(R::SDMA0_UTCL1_PAGE)) & 0xFF0FFFu;
	t |= (kCACHE_READ_POLICY_L2_DEFAULT << 12) | (kCACHE_WRITE_POLICY_L2_DEFAULT << 14) | kUTCL1_PAGE_LLC_NOALLOC;
	WREG32(dev, sd(R::SDMA0_UTCL1_PAGE), t);
	t = RREG32(dev, gc1(dev, R::SDMA0_F32_CNTL));
	t = setf(t, F32_HALT, 0);
	t = setf(t, F32_TH1_RESET, 0);
	WREG32(dev, gc1(dev, R::SDMA0_F32_CNTL), t);
	rb = setf(rb, RB_ENABLE, 1);
	WREG32(dev, sd(R::SDMA0_QUEUE0_RB_CNTL), rb);
	t = setf(RREG32(dev, sd(R::SDMA0_QUEUE0_IB_CNTL)), IB_ENABLE, 1);
	WREG32(dev, sd(R::SDMA0_QUEUE0_IB_CNTL), t);
	rep.f32CntlAfter = RREG32(dev, gc1(dev, R::SDMA0_F32_CNTL));
	(void)R::SDMA0_CNTL;

	// sdma_v6_0_ring_test_ring: WRITE_LINEAR one dword, padded with NOPs (0) to the
	// ring's 16-dword alignment (align_mask 0xf), then commit.
	WBAR0_32(dev, base + kTestOff, 0xCAFEDEADu);
	uint32_t pkt[16] = {};
	pkt[0] = kSDMA_OP_WRITE | (kSDMA_SUBOP_WRITE_LINEAR << 8);
	pkt[1] = (uint32_t)testMc;
	pkt[2] = (uint32_t)(testMc >> 32);
	pkt[3] = 0;                        // COUNT = dwords - 1
	pkt[4] = kN33SdmaTestValue;
	bar0_memcpy_to_vram(dev, base + kRingOff, pkt, sizeof(pkt));
	const uint64_t wptrBytes = sizeof(pkt);
	WBAR0_64(dev, base + kWptrOff, wptrBytes);   // the F32 polls this (F32_WPTR_POLL_ENABLE)
	amdgpu_hdp_flush(dev);
	WREG32(dev, sd(R::SDMA0_QUEUE0_RB_WPTR), (uint32_t)wptrBytes);
	WREG32(dev, sd(R::SDMA0_QUEUE0_RB_WPTR_HI), (uint32_t)(wptrBytes >> 32));

	rep.passed = false;
	uint32_t us = 0;
	for (; us < timeoutUs; us += 10) {
		if (RBAR0_32(dev, base + kTestOff) == kN33SdmaTestValue) { rep.passed = true; break; }
		IODelay(10);
	}
	rep.waitedUs      = us;
	rep.testValue     = RBAR0_32(dev, base + kTestOff);
	rep.rptrWriteback = RBAR0_64(dev, base + kRptrOff);
	rep.rbRptr        = RREG32(dev, sd(R::SDMA0_QUEUE0_RB_RPTR));
	rep.statusReg     = RREG32(dev, sd(R::SDMA0_STATUS_REG));
	rep.faultStatus   = RREG32(dev, gc0(dev, R::GCVM_L2_PROTECTION_FAULT_STATUS));
	rep.faultAddr     = ((uint64_t)RREG32(dev, gc0(dev, R::GCVM_L2_PROTECTION_FAULT_ADDR_HI32)) << 32) |
	                    RREG32(dev, gc0(dev, R::GCVM_L2_PROTECTION_FAULT_ADDR_LO32));
	UCODE_LOG("navi33-gfx: SDMA0 ring test %s after %u us (slot %#x, rptr wb %#llx, RB_RPTR %#x, STATUS %#x, F32 %#x->%#x, VM fault %#x @%#llx)",
	          rep.passed ? "PASSED" : "FAILED", us, rep.testValue, (unsigned long long)rep.rptrWriteback,
	          rep.rbRptr, rep.statusReg, rep.f32CntlBefore, rep.f32CntlAfter, rep.faultStatus,
	          (unsigned long long)rep.faultAddr);
	rep.stage = rep.passed ? "sdma-write-ok" : "sdma-timeout";
	return rep.passed ? kIOReturnSuccess : kIOReturnTimeout;
}

// ============================================================================
// Stage 5 — gfx CP ring 0 (gfx_v11_0.c, amdgpu_async_gfx_ring = 0)
// ============================================================================
namespace {
namespace C {   // gc_11_0_0_offset.h: register, BASE_IDX
	constexpr uint32_t CP_RB0_RPTR           = 0x0f60;  // 0
	constexpr uint32_t CP_RB_WPTR_DELAY      = 0x0f61;  // 0
	constexpr uint32_t CP_STAT               = 0x0f40;  // 0
	constexpr uint32_t GRBM_STATUS           = 0x0da4;  // 0
	constexpr uint32_t CP_RB0_BASE           = 0x1de0;  // 0
	constexpr uint32_t CP_RB0_CNTL           = 0x1de1;  // 0
	constexpr uint32_t CP_RB0_RPTR_ADDR      = 0x1de3;  // 0
	constexpr uint32_t CP_RB0_RPTR_ADDR_HI   = 0x1de4;  // 0
	constexpr uint32_t CP_DEVICE_ID          = 0x1deb;  // 0
	constexpr uint32_t CP_RB_VMID            = 0x1df1;  // 0
	constexpr uint32_t CP_RB0_WPTR           = 0x1df4;  // 0
	constexpr uint32_t CP_RB0_WPTR_HI        = 0x1df5;  // 0
	constexpr uint32_t CP_PFP_PRGRM_CNTR_START = 0x1e44; // 0
	constexpr uint32_t CP_ME_PRGRM_CNTR_START  = 0x1e45; // 0
	constexpr uint32_t CP_MAX_CONTEXT        = 0x1e4e;  // 0
	constexpr uint32_t CP_RB0_BASE_HI        = 0x1e51;  // 0
	constexpr uint32_t CP_PFP_PRGRM_CNTR_START_HI = 0x1e59; // 0
	constexpr uint32_t CP_ME_PRGRM_CNTR_START_HI  = 0x1e79; // 0
	constexpr uint32_t CP_RB_WPTR_POLL_ADDR_LO = 0x1e8b; // 0
	constexpr uint32_t CP_RB_WPTR_POLL_ADDR_HI = 0x1e8c; // 0
	constexpr uint32_t CP_RB_DOORBELL_CONTROL  = 0x1e8d; // 0
	constexpr uint32_t CP_RB_ACTIVE          = 0x1f40;  // 0
	constexpr uint32_t CP_ME_CNTL            = 0x0803;  // 1
	constexpr uint32_t GRBM_GFX_CNTL         = 0x0900;  // 1
	constexpr uint32_t SCRATCH_REG0          = 0x2040;  // 1
	constexpr uint32_t CP_MEC_RS64_PRGRM_CNTR_START    = 0x2900; // 1
	constexpr uint32_t CP_MEC_RS64_CNTL      = 0x2904;  // 1
	constexpr uint32_t CP_MEC_RS64_PRGRM_CNTR_START_HI = 0x2938; // 1
}
constexpr F ME_PFP_PIPE0_RESET { 0x12, 0x00040000 };
constexpr F ME_PFP_PIPE1_RESET { 0x13, 0x00080000 };
constexpr F ME_ME_PIPE0_RESET  { 0x14, 0x00100000 };
constexpr F ME_ME_PIPE1_RESET  { 0x15, 0x00200000 };
constexpr F ME_PFP_HALT        { 0x1a, 0x04000000 };
constexpr F ME_ME_HALT         { 0x1c, 0x10000000 };
constexpr F MEC_PIPE0_RESET    { 0x10, 0x00010000 };
constexpr F MEC_PIPE1_RESET    { 0x11, 0x00020000 };
constexpr F MEC_PIPE2_RESET    { 0x12, 0x00040000 };
constexpr F MEC_PIPE3_RESET    { 0x13, 0x00080000 };
constexpr F RB0_BUFSZ          { 0x0,  0x0000003F };
constexpr F RB0_BLKSZ          { 0x8,  0x00003F00 };
constexpr F RB0_KMD_QUEUE      { 0x1d, 0x20000000 };
constexpr F RBDB_DOORBELL_EN   { 0x1e, 0x40000000 };
constexpr F GFXC_PIPEID        { 0x0,  0x00000003 };
constexpr F GFXC_MEID          { 0x2,  0x0000000C };
constexpr F GFXC_VMID          { 0x4,  0x000000F0 };
constexpr F GFXC_QUEUEID       { 0x8,  0x00000700 };
constexpr uint32_t kRB_RPTR_ADDR_HI_MASK = 0x0000FFFFu;

// nvd.h
constexpr uint32_t pkt3(uint32_t op, uint32_t n) { return (3u << 30) | ((n & 0x3FFFu) << 16) | ((op & 0xFFu) << 8); }
constexpr uint32_t kPKT3_NOP = 0x10, kPKT3_SET_UCONFIG_REG = 0x79, kSET_UCONFIG_REG_START = 0x0000c000;
constexpr uint32_t kGfxNop = pkt3(kPKT3_NOP, 0x3FFF);   // gfx_v11_0_ring_funcs_gfx .nop

constexpr uint64_t kCpRingOff  = 0x4000;   // 4 KiB = 1024 dwords
constexpr uint32_t kCpRingBytes = 0x1000;
constexpr uint64_t kCpRptrOff  = 0x5000;
constexpr uint64_t kCpWptrOff  = 0x5008;
constexpr uint32_t kCpPad      = 256;      // gfx ring align_mask 0xff

// soc21_grbm_select
void grbm_select(const DeviceContext &d, uint32_t me, uint32_t pipe, uint32_t queue, uint32_t vmid) {
	uint32_t t = 0;
	t = setf(t, GFXC_PIPEID, pipe);
	t = setf(t, GFXC_MEID, me);
	t = setf(t, GFXC_VMID, vmid);
	t = setf(t, GFXC_QUEUEID, queue);
	WREG32(d, gc1(d, C::GRBM_GFX_CNTL), t);
}

bool startAddr(const uint8_t *b, size_t n, uint32_t &lo, uint32_t &hi) {
	if (!b || n < sizeof(gfx_firmware_header_v2_0)) return false;
	const auto *h = reinterpret_cast<const gfx_firmware_header_v2_0 *>(b);
	if (h->header.header_version_major != 2) return false;
	lo = h->ucode_start_addr_lo; hi = h->ucode_start_addr_hi;
	return true;
}
} // namespace

bool navi33_cp_fw_from_blobs(const uint8_t *pfp, size_t pfpSize, const uint8_t *me, size_t meSize,
                             const uint8_t *mec, size_t mecSize, N33CpFw &out) {
	return startAddr(pfp, pfpSize, out.pfpLo, out.pfpHi) &&
	       startAddr(me, meSize, out.meLo, out.meHi) &&
	       startAddr(mec, mecSize, out.mecLo, out.mecHi);
}

kern_return_t navi33_cp_gfx_ring_test(DeviceContext &dev, const N33CpFw &fw, N33CpReport &rep, uint32_t timeoutUs) {
	rep.stage = "cp";
	const uint64_t base   = slot(dev);
	const uint64_t ringMc = dev.vramMC(base + kCpRingOff);
	const uint64_t rptrMc = dev.vramMC(base + kCpRptrOff);
	const uint64_t wptrMc = dev.vramMC(base + kCpWptrOff);
	auto g0 = [&](uint32_t r) { return gc0(dev, r); };
	auto g1 = [&](uint32_t r) { return gc1(dev, r); };
	rep.meCntlBefore = RREG32(dev, g1(C::CP_ME_CNTL));

	// gfx_v11_0_config_gfx_rs64
	for (uint32_t pipe = 0; pipe < 2; pipe++) {
		grbm_select(dev, 0, pipe, 0, 0);
		WREG32(dev, g0(C::CP_PFP_PRGRM_CNTR_START), (fw.pfpHi << 30) | (fw.pfpLo >> 2));
		WREG32(dev, g0(C::CP_PFP_PRGRM_CNTR_START_HI), fw.pfpHi >> 2);
	}
	grbm_select(dev, 0, 0, 0, 0);
	uint32_t t = RREG32(dev, g1(C::CP_ME_CNTL));
	t = setf(setf(t, ME_PFP_PIPE0_RESET, 1), ME_PFP_PIPE1_RESET, 1);
	WREG32(dev, g1(C::CP_ME_CNTL), t);
	t = setf(setf(t, ME_PFP_PIPE0_RESET, 0), ME_PFP_PIPE1_RESET, 0);
	WREG32(dev, g1(C::CP_ME_CNTL), t);
	for (uint32_t pipe = 0; pipe < 2; pipe++) {
		grbm_select(dev, 0, pipe, 0, 0);
		WREG32(dev, g0(C::CP_ME_PRGRM_CNTR_START), (fw.meHi << 30) | (fw.meLo >> 2));
		WREG32(dev, g0(C::CP_ME_PRGRM_CNTR_START_HI), fw.meHi >> 2);
	}
	grbm_select(dev, 0, 0, 0, 0);
	t = RREG32(dev, g1(C::CP_ME_CNTL));
	t = setf(setf(t, ME_ME_PIPE0_RESET, 1), ME_ME_PIPE1_RESET, 1);
	WREG32(dev, g1(C::CP_ME_CNTL), t);
	t = setf(setf(t, ME_ME_PIPE0_RESET, 0), ME_ME_PIPE1_RESET, 0);
	WREG32(dev, g1(C::CP_ME_CNTL), t);
	for (uint32_t pipe = 0; pipe < 4; pipe++) {
		grbm_select(dev, 1, pipe, 0, 0);
		WREG32(dev, g1(C::CP_MEC_RS64_PRGRM_CNTR_START), (fw.mecLo >> 2) | (fw.mecHi << 30));
		WREG32(dev, g1(C::CP_MEC_RS64_PRGRM_CNTR_START_HI), fw.mecHi >> 2);
	}
	grbm_select(dev, 0, 0, 0, 0);
	t = RREG32(dev, g1(C::CP_MEC_RS64_CNTL));
	t = setf(setf(setf(setf(t, MEC_PIPE0_RESET, 1), MEC_PIPE1_RESET, 1), MEC_PIPE2_RESET, 1), MEC_PIPE3_RESET, 1);
	WREG32(dev, g1(C::CP_MEC_RS64_CNTL), t);
	t = setf(setf(setf(setf(t, MEC_PIPE0_RESET, 0), MEC_PIPE1_RESET, 0), MEC_PIPE2_RESET, 0), MEC_PIPE3_RESET, 0);
	WREG32(dev, g1(C::CP_MEC_RS64_CNTL), t);

	// gfx_v11_0_cp_gfx_resume — ring 0, pipe 0, VMID 0, no doorbell
	bar0_memset_vram(dev, base + kCpRingOff, kGfxNop, kCpRingBytes);
	WBAR0_64(dev, base + kCpRptrOff, 0);
	WBAR0_64(dev, base + kCpWptrOff, 0);
	WREG32(dev, g0(C::CP_RB_WPTR_DELAY), 0);
	WREG32(dev, g0(C::CP_RB_VMID), 0);
	grbm_select(dev, 0, 0, 0, 0);                      // cp_gfx_switch_pipe(PIPE_ID0)
	constexpr uint32_t bufsz = 9;                      // order_base_2(4096 / 8)
	static_assert(kCpRingBytes / 8 == (1u << bufsz), "RB_BUFSZ");
	uint32_t rb = setf(setf(setf(0, RB0_BUFSZ, bufsz), RB0_BLKSZ, bufsz - 2), RB0_KMD_QUEUE, 1);
	WREG32(dev, g0(C::CP_RB0_CNTL), rb);
	WREG32(dev, g0(C::CP_RB0_WPTR), 0);
	WREG32(dev, g0(C::CP_RB0_WPTR_HI), 0);
	WREG32(dev, g0(C::CP_RB0_RPTR_ADDR), (uint32_t)rptrMc);
	WREG32(dev, g0(C::CP_RB0_RPTR_ADDR_HI), (uint32_t)(rptrMc >> 32) & kRB_RPTR_ADDR_HI_MASK);
	WREG32(dev, g0(C::CP_RB_WPTR_POLL_ADDR_LO), (uint32_t)wptrMc);
	WREG32(dev, g0(C::CP_RB_WPTR_POLL_ADDR_HI), (uint32_t)(wptrMc >> 32));
	IOSleep(1);
	WREG32(dev, g0(C::CP_RB0_CNTL), rb);
	WREG32(dev, g0(C::CP_RB0_BASE), (uint32_t)(ringMc >> 8));
	WREG32(dev, g0(C::CP_RB0_BASE_HI), (uint32_t)((ringMc >> 8) >> 32));
	WREG32(dev, g0(C::CP_RB_ACTIVE), 1);
	t = setf(RREG32(dev, g0(C::CP_RB_DOORBELL_CONTROL)), RBDB_DOORBELL_EN, 0);
	WREG32(dev, g0(C::CP_RB_DOORBELL_CONTROL), t);

	// gfx_v11_0_cp_gfx_start (without the clear-state preamble): init the CP, unhalt.
	WREG32(dev, g0(C::CP_MAX_CONTEXT), 8 - 1);         // max_hw_contexts 8
	WREG32(dev, g0(C::CP_DEVICE_ID), 1);
	t = RREG32(dev, g1(C::CP_ME_CNTL));
	t = setf(setf(t, ME_ME_HALT, 0), ME_PFP_HALT, 0);
	WREG32(dev, g1(C::CP_ME_CNTL), t);
	rep.cpIdleAfterUnhalt = false;
	for (uint32_t us = 0; us < 100000; us += 10) {
		rep.cpStat = RREG32(dev, g0(C::CP_STAT));
		if (rep.cpStat == 0) { rep.cpIdleAfterUnhalt = true; break; }
		IODelay(10);
	}
	rep.meCntlAfter = RREG32(dev, g1(C::CP_ME_CNTL));

	// gfx_v11_0_ring_test_ring
	const uint32_t scratch = g1(C::SCRATCH_REG0);       // absolute dword index
	WREG32(dev, scratch, 0xCAFEDEADu);
	uint32_t ring[kCpPad];
	for (uint32_t i = 0; i < kCpPad; i++) ring[i] = kGfxNop;
	ring[0] = pkt3(kPKT3_SET_UCONFIG_REG, 1);
	ring[1] = scratch - kSET_UCONFIG_REG_START;
	ring[2] = 0xDEADBEEFu;
	bar0_memcpy_to_vram(dev, base + kCpRingOff, ring, sizeof(ring));
	WBAR0_64(dev, base + kCpWptrOff, kCpPad);           // gfx wptr is in dwords
	amdgpu_hdp_flush(dev);
	WREG32(dev, g0(C::CP_RB0_WPTR), kCpPad);
	WREG32(dev, g0(C::CP_RB0_WPTR_HI), 0);

	rep.passed = false;
	uint32_t us = 0;
	for (; us < timeoutUs; us += 10) {
		if (RREG32(dev, scratch) == 0xDEADBEEFu) { rep.passed = true; break; }
		IODelay(10);
	}
	rep.waitedUs      = us;
	rep.scratch       = RREG32(dev, scratch);
	rep.rbRptr        = RREG32(dev, g0(C::CP_RB0_RPTR));
	rep.rptrWriteback = RBAR0_64(dev, base + kCpRptrOff);
	rep.grbmStatus    = RREG32(dev, g0(C::GRBM_STATUS));
	if (!rep.passed) rep.cpStat = RREG32(dev, g0(C::CP_STAT));
	UCODE_LOG("navi33-gfx: CP gfx ring test %s after %u us (SCRATCH %#x, RB0_RPTR %#x, rptr wb %#llx, CP_STAT %#x, GRBM %#x, ME_CNTL %#x->%#x)",
	          rep.passed ? "PASSED" : "FAILED", us, rep.scratch, rep.rbRptr, (unsigned long long)rep.rptrWriteback,
	          rep.cpStat, rep.grbmStatus, rep.meCntlBefore, rep.meCntlAfter);
	rep.stage = rep.passed ? "cp-write-ok" : (rep.cpIdleAfterUnhalt ? "cp-timeout" : "cp-not-idle");
	return rep.passed ? kIOReturnSuccess : kIOReturnTimeout;
}

// ============================================================================
// Stage 6 — indirect buffers on both rings
// ============================================================================
namespace {
constexpr uint64_t kIbSdmaOff     = 0x6000;   // 32-byte aligned (SDMA requirement)
constexpr uint64_t kIbSdmaDstOff  = 0x6100;
constexpr uint64_t kIbCpOff       = 0x6200;
constexpr uint64_t kIbCpDstOff    = 0x6300;
constexpr uint32_t kSdmaWptrAfterS4 = 64;     // bytes: navi33_sdma_ring_test's 16 dwords
constexpr uint32_t kCpWptrAfterS5   = kCpPad; // dwords: navi33_cp_gfx_ring_test's 256
constexpr uint32_t kSDMA_OP_INDIRECT = 4;     // sdma_v6_0_0_pkt_open.h
constexpr uint32_t kPKT3_INDIRECT_BUFFER = 0x3F, kPKT3_WRITE_DATA = 0x37;   // nvd.h
constexpr uint32_t kWRITE_DATA_DST_SEL_MEM = 5u << 8, kWR_CONFIRM = 1u << 20;
} // namespace

kern_return_t navi33_ib_tests(DeviceContext &dev, N33IbReport &rep, uint32_t timeoutUs) {
	const uint64_t base = slot(dev);
	auto mc = [&](uint64_t off) { return dev.vramMC(base + off); };

	WBAR0_32(dev, base + kIbSdmaDstOff, 0xCAFEDEADu);
	WBAR0_32(dev, base + kIbCpDstOff, 0xCAFEDEADu);

	// SDMA IB: WRITE_LINEAR + 3 NOPs, length 8 (sdma_v6_0_ring_test_ib).
	{
		const uint64_t dst = mc(kIbSdmaDstOff);
		uint32_t ib[8] = { kSDMA_OP_WRITE | (kSDMA_SUBOP_WRITE_LINEAR << 8), (uint32_t)dst, (uint32_t)(dst >> 32), 0,
		                   kN33SdmaTestValue, 0, 0, 0 };
		bar0_memcpy_to_vram(dev, base + kIbSdmaOff, ib, sizeof(ib));
	}
	// gfx IB: WRITE_DATA to memory with confirm (gfx_v11_0_ring_test_ib).
	{
		const uint64_t dst = mc(kIbCpDstOff);
		uint32_t ib[5] = { pkt3(kPKT3_WRITE_DATA, 3), kWRITE_DATA_DST_SEL_MEM | kWR_CONFIRM, (uint32_t)dst,
		                   (uint32_t)(dst >> 32), kN33SdmaTestValue };
		bar0_memcpy_to_vram(dev, base + kIbCpOff, ib, sizeof(ib));
	}

	// SDMA ring: at dword 16, NOPs so the 6-dword IB packet ends on an 8-dword boundary
	// ((2 - wptr) & 7 = 2), the packet, then NOPs to the 16-dword commit alignment.
	{
		const uint64_t ibMc = mc(kIbSdmaOff);
		uint32_t pkt[16] = {};
		pkt[2] = kSDMA_OP_INDIRECT | (0u << 16);       // VMID 0
		pkt[3] = (uint32_t)ibMc & 0xFFFFFFE0u;
		pkt[4] = (uint32_t)(ibMc >> 32);
		pkt[5] = 8;                                   // length_dw
		pkt[6] = 0; pkt[7] = 0;                       // CSA: none for VMID 0
		bar0_memcpy_to_vram(dev, base + kRingOff + kSdmaWptrAfterS4, pkt, sizeof(pkt));
	}
	// gfx ring: at dword 256, INDIRECT_BUFFER (VMID 0, length 5), then NOPs to 512.
	{
		const uint64_t ibMc = mc(kIbCpOff);
		uint32_t ring[kCpPad];
		for (uint32_t i = 0; i < kCpPad; i++) ring[i] = kGfxNop;
		ring[0] = pkt3(kPKT3_INDIRECT_BUFFER, 2);
		ring[1] = (uint32_t)ibMc;
		ring[2] = (uint32_t)(ibMc >> 32);
		ring[3] = 5u | (0u << 24);
		bar0_memcpy_to_vram(dev, base + kCpRingOff + kCpWptrAfterS5 * 4, ring, sizeof(ring));
	}
	amdgpu_hdp_flush(dev);

	const uint64_t sdmaW = kSdmaWptrAfterS4 + 64;      // bytes
	const uint32_t cpW   = kCpWptrAfterS5 + kCpPad;    // dwords
	WBAR0_64(dev, base + kWptrOff, sdmaW);
	WBAR0_64(dev, base + kCpWptrOff, cpW);
	amdgpu_hdp_flush(dev);
	WREG32(dev, gc0(dev, R::SDMA0_QUEUE0_RB_WPTR), (uint32_t)sdmaW);
	WREG32(dev, gc0(dev, R::SDMA0_QUEUE0_RB_WPTR_HI), 0);
	WREG32(dev, gc0(dev, C::CP_RB0_WPTR), cpW);
	WREG32(dev, gc0(dev, C::CP_RB0_WPTR_HI), 0);

	rep.sdmaPassed = rep.cpPassed = false;
	rep.sdmaWaitedUs = rep.cpWaitedUs = timeoutUs;
	for (uint32_t us = 0; us < timeoutUs && !(rep.sdmaPassed && rep.cpPassed); us += 10) {
		if (!rep.sdmaPassed && RBAR0_32(dev, base + kIbSdmaDstOff) == kN33SdmaTestValue) { rep.sdmaPassed = true; rep.sdmaWaitedUs = us; }
		if (!rep.cpPassed && RBAR0_32(dev, base + kIbCpDstOff) == kN33SdmaTestValue) { rep.cpPassed = true; rep.cpWaitedUs = us; }
		IODelay(10);
	}
	rep.sdmaValue   = RBAR0_32(dev, base + kIbSdmaDstOff);
	rep.cpValue     = RBAR0_32(dev, base + kIbCpDstOff);
	rep.sdmaRptr    = RREG32(dev, gc0(dev, R::SDMA0_QUEUE0_RB_RPTR));
	rep.cpRptr      = RREG32(dev, gc0(dev, C::CP_RB0_RPTR));
	rep.faultStatus = RREG32(dev, gc0(dev, R::GCVM_L2_PROTECTION_FAULT_STATUS));
	rep.stage = rep.sdmaPassed && rep.cpPassed ? "ib-ok" :
	            rep.sdmaPassed ? "ib-sdma-ok-cp-timeout" : rep.cpPassed ? "ib-cp-ok-sdma-timeout" : "ib-timeout";
	UCODE_LOG("navi33-gfx: IB tests — SDMA %s (%#x, %u us, rptr %#x), CP %s (%#x, %u us, rptr %#x), VM fault %#x",
	          rep.sdmaPassed ? "ok" : "FAILED", rep.sdmaValue, rep.sdmaWaitedUs, rep.sdmaRptr,
	          rep.cpPassed ? "ok" : "FAILED", rep.cpValue, rep.cpWaitedUs, rep.cpRptr, rep.faultStatus);
	return rep.sdmaPassed && rep.cpPassed ? kIOReturnSuccess : kIOReturnTimeout;
}

// ============================================================================
// Stage 7 — fences + SDMA copy
// ============================================================================
namespace {
constexpr uint32_t kSdmaWptrAfterS6 = kSdmaWptrAfterS4 + 64;   // bytes
constexpr uint32_t kCpWptrAfterS6   = kCpWptrAfterS5 + kCpPad; // dwords
static_assert(kCpWptrAfterS6 + kCpPad <= kCpRingBytes / 4, "stage 7 must not wrap the gfx ring");

constexpr uint32_t kSDMA_OP_COPY = 1, kSDMA_SUBOP_COPY_LINEAR = 0, kSDMA_OP_FENCE = 5;
constexpr uint32_t kPKT3_RELEASE_MEM = 0x49;                    // nvd.h
constexpr uint32_t kRM_EVENT_CACHE_FLUSH_AND_INV_TS = 0x14;     // soc21_enum.h
constexpr uint32_t kRM_DW1 = (1u << 22) /*GCR_SEQ*/ | (1u << 21) /*GCR_GL2_WB*/ | (1u << 13) /*GCR_GLM_INV*/ |
                             (1u << 12) /*GCR_GLM_WB*/ | (3u << 25) /*CACHE_POLICY(3)*/ |
                             kRM_EVENT_CACHE_FLUSH_AND_INV_TS | (5u << 8) /*EVENT_INDEX(5)*/;
constexpr uint32_t kRM_DW2_DATA32 = 1u << 29;                   // DATA_SEL(1), INT_SEL(0)

constexpr uint64_t kS7SdmaIbOff   = 0x7000;   // 32-byte aligned
constexpr uint64_t kS7SdmaFenceOff = 0x7100;
constexpr uint64_t kS7CpFenceOff  = 0x7108;
constexpr uint64_t kS7SrcOff      = 0x8000;
constexpr uint64_t kS7DstOff      = 0x9000;
constexpr uint32_t kS7CopyBytes   = 0x1000;
static_assert(kS7DstOff + kS7CopyBytes <= kN33GfxVramSize, "stage 7 stays inside the slot");
} // namespace

kern_return_t navi33_fence_copy_tests(DeviceContext &dev, N33FenceReport &rep, uint32_t timeoutUs) {
	const uint64_t base = slot(dev);
	auto mc = [&](uint64_t off) { return dev.vramMC(base + off); };

	for (uint32_t i = 0; i < kS7CopyBytes / 4; i++) WBAR0_32(dev, base + kS7SrcOff + i * 4, 0x5A000000u | i);
	bar0_memset_vram(dev, base + kS7DstOff, 0u, kS7CopyBytes);
	WBAR0_32(dev, base + kS7SdmaFenceOff, 0);
	WBAR0_32(dev, base + kS7CpFenceOff, 0);

	// SDMA IB: COPY_LINEAR 4 KiB, padded to 8 dwords.
	{
		const uint64_t src = mc(kS7SrcOff), dst = mc(kS7DstOff);
		const uint32_t ib[8] = { kSDMA_OP_COPY | (kSDMA_SUBOP_COPY_LINEAR << 8), kS7CopyBytes - 1, 0,
		                         (uint32_t)src, (uint32_t)(src >> 32), (uint32_t)dst, (uint32_t)(dst >> 32), 0 };
		bar0_memcpy_to_vram(dev, base + kS7SdmaIbOff, ib, sizeof(ib));
	}
	// SDMA ring at dword 32: 2 NOPs ((2 - 32) & 7), INDIRECT (6 dw), FENCE (4 dw), NOPs to 16.
	{
		const uint64_t ibMc = mc(kS7SdmaIbOff), f = mc(kS7SdmaFenceOff);
		static_assert(((2u - kSdmaWptrAfterS6 / 4) & 7u) == 2, "IB packet alignment");
		uint32_t pkt[16] = {};
		pkt[2] = kSDMA_OP_INDIRECT | (0u << 16);
		pkt[3] = (uint32_t)ibMc & 0xFFFFFFE0u;
		pkt[4] = (uint32_t)(ibMc >> 32);
		pkt[5] = 8;
		pkt[6] = 0; pkt[7] = 0;
		pkt[8]  = kSDMA_OP_FENCE | (3u << 16);       // MTYPE UC
		pkt[9]  = (uint32_t)f;
		pkt[10] = (uint32_t)(f >> 32);
		pkt[11] = kN33FenceSeq;
		bar0_memcpy_to_vram(dev, base + kRingOff + kSdmaWptrAfterS6, pkt, sizeof(pkt));
	}
	// gfx ring at dword 512: RELEASE_MEM (flush caches, write seq), NOPs to 768.
	{
		const uint64_t f = mc(kS7CpFenceOff);
		uint32_t ring[kCpPad];
		for (uint32_t i = 0; i < kCpPad; i++) ring[i] = kGfxNop;
		ring[0] = pkt3(kPKT3_RELEASE_MEM, 6);
		ring[1] = kRM_DW1;
		ring[2] = kRM_DW2_DATA32;
		ring[3] = (uint32_t)f;
		ring[4] = (uint32_t)(f >> 32);
		ring[5] = kN33FenceSeq;
		ring[6] = 0;
		ring[7] = 0;
		bar0_memcpy_to_vram(dev, base + kCpRingOff + kCpWptrAfterS6 * 4, ring, sizeof(ring));
	}
	amdgpu_hdp_flush(dev);
	const uint64_t sdmaW = kSdmaWptrAfterS6 + 64;
	const uint32_t cpW   = kCpWptrAfterS6 + kCpPad;
	WBAR0_64(dev, base + kWptrOff, sdmaW);
	WBAR0_64(dev, base + kCpWptrOff, cpW);
	amdgpu_hdp_flush(dev);
	WREG32(dev, gc0(dev, R::SDMA0_QUEUE0_RB_WPTR), (uint32_t)sdmaW);
	WREG32(dev, gc0(dev, R::SDMA0_QUEUE0_RB_WPTR_HI), 0);
	WREG32(dev, gc0(dev, C::CP_RB0_WPTR), cpW);
	WREG32(dev, gc0(dev, C::CP_RB0_WPTR_HI), 0);

	bool sd = false, cp = false;
	rep.sdmaWaitedUs = rep.cpWaitedUs = timeoutUs;
	for (uint32_t us = 0; us < timeoutUs && !(sd && cp); us += 10) {
		if (!sd && RBAR0_32(dev, base + kS7SdmaFenceOff) == kN33FenceSeq) { sd = true; rep.sdmaWaitedUs = us; }
		if (!cp && RBAR0_32(dev, base + kS7CpFenceOff) == kN33FenceSeq)   { cp = true; rep.cpWaitedUs = us; }
		IODelay(10);
	}
	rep.sdmaFence = RBAR0_32(dev, base + kS7SdmaFenceOff);
	rep.cpFence   = RBAR0_32(dev, base + kS7CpFenceOff);
	rep.copyBadDwords = 0;
	for (uint32_t i = 0; i < kS7CopyBytes / 4; i++)
		if (RBAR0_32(dev, base + kS7DstOff + i * 4) != (0x5A000000u | i)) rep.copyBadDwords++;
	rep.sdmaPassed = sd && rep.copyBadDwords == 0;
	rep.cpPassed   = cp;
	rep.stage = rep.sdmaPassed && rep.cpPassed ? "fence-copy-ok" :
	            (sd && rep.copyBadDwords) ? "sdma-copy-wrong-data" :
	            !sd && !cp ? "fence-timeout" : !sd ? "sdma-fence-timeout" : "cp-fence-timeout";
	UCODE_LOG("navi33-gfx: fence/copy — SDMA fence %u (%u us, %u bad dwords), CP fence %u (%u us) -> %s",
	          rep.sdmaFence, rep.sdmaWaitedUs, rep.copyBadDwords, rep.cpFence, rep.cpWaitedUs, rep.stage);
	return rep.sdmaPassed && rep.cpPassed ? kIOReturnSuccess : kIOReturnTimeout;
}

// ============================================================================
// Stage 8 — real data: the live console framebuffer
// ============================================================================
namespace {
constexpr uint32_t kSDMA_OP_CONST_FILL = 11, kFILLSIZE_DWORD = 2u << 30;   // sdma_v6_0_0_pkt_open.h
constexpr uint64_t kS8IbOff    = 0xA000;   // three 32-byte-aligned IBs, 0x100 apart
constexpr uint64_t kS8FenceOff = 0x7200;
constexpr uint32_t kBandColor  = 0xFFFFFFFFu;   // white: identical in every byte, so fill width can't matter
constexpr uint64_t kOneMiB     = 1ull << 20;
constexpr uint64_t kSpan       = 32ull << 20;   // the bring-up span starting at vramBase

// One IB + FENCE on SDMA0, appended at the current hardware wptr. Waits for the fence.
bool sdmaSubmit(DeviceContext &dev, uint64_t ibOff, uint32_t ibDw, uint32_t seq, uint32_t timeoutUs, uint32_t &waitedUs) {
	const uint64_t base = slot(dev);
	const uint64_t ibMc = dev.vramMC(base + ibOff), f = dev.vramMC(base + kS8FenceOff);
	WBAR0_32(dev, base + kS8FenceOff, 0);
	const uint32_t wpDw = RREG32(dev, gc0(dev, R::SDMA0_QUEUE0_RB_WPTR)) / 4;
	uint32_t pkt[16] = {};
	uint32_t n = (2u - wpDw) & 7u;
	pkt[n++] = kSDMA_OP_INDIRECT;
	pkt[n++] = (uint32_t)ibMc & 0xFFFFFFE0u;
	pkt[n++] = (uint32_t)(ibMc >> 32);
	pkt[n++] = ibDw;
	pkt[n++] = 0;
	pkt[n++] = 0;
	pkt[n++] = kSDMA_OP_FENCE | (3u << 16);
	pkt[n++] = (uint32_t)f;
	pkt[n++] = (uint32_t)(f >> 32);
	pkt[n++] = seq;
	const uint32_t total = (n + 15u) & ~15u;
	for (uint32_t i = 0; i < total; i++)
		WBAR0_32(dev, base + kRingOff + (uint64_t)((wpDw + i) % (kRingBytes / 4)) * 4, i < 16 ? pkt[i] : 0);
	const uint32_t nwp = ((wpDw + total) % (kRingBytes / 4)) * 4;
	WBAR0_64(dev, base + kWptrOff, nwp);
	amdgpu_hdp_flush(dev);
	WREG32(dev, gc0(dev, R::SDMA0_QUEUE0_RB_WPTR), nwp);
	WREG32(dev, gc0(dev, R::SDMA0_QUEUE0_RB_WPTR_HI), 0);
	for (waitedUs = 0; waitedUs < timeoutUs; waitedUs += 10) {
		if (RBAR0_32(dev, base + kS8FenceOff) == seq) return true;
		IODelay(10);
	}
	return false;
}

uint32_t copyIb(DeviceContext &dev, uint64_t at, uint64_t srcMc, uint64_t dstMc, uint64_t bytes) {
	const uint32_t ib[8] = { kSDMA_OP_COPY | (kSDMA_SUBOP_COPY_LINEAR << 8), (uint32_t)(bytes - 1), 0,
	                         (uint32_t)srcMc, (uint32_t)(srcMc >> 32), (uint32_t)dstMc, (uint32_t)(dstMc >> 32), 0 };
	bar0_memcpy_to_vram(dev, slot(dev) + at, ib, sizeof(ib));
	return 8;
}

// Compares up to 4096 dwords spread over [0, len) of two VRAM ranges (b == ~0: against a constant).
uint32_t sampleCompare(DeviceContext &dev, uint64_t a, uint64_t b, uint64_t len, uint32_t constant) {
	const uint64_t n = len / 4 < 4096 ? len / 4 : 4096;
	const uint64_t step = n ? (len / 4) / n : 1;
	uint32_t bad = 0;
	for (uint64_t i = 0; i < n; i++) {
		const uint64_t o = i * step * 4;
		const uint32_t want = b == ~0ull ? constant : RBAR0_32(dev, b + o);
		if (RBAR0_32(dev, a + o) != want) bad++;
	}
	return bad;
}
} // namespace

kern_return_t navi33_screen_test(DeviceContext &dev, uint64_t fbOff, uint64_t fbLen, N33ScreenReport &rep,
                                 uint32_t holdMs, uint32_t timeoutUs) {
	rep = N33ScreenReport{};
	rep.fbOff = fbOff; rep.fbLen = fbLen;
	rep.stage = "screen";
	if (fbOff == ~0ull || fbLen < 4096 || fbOff + fbLen > dev.vramLimit) { rep.stage = "no-console-fb"; return kIOReturnNotFound; }
	fbLen &= ~3ull;
	rep.fbLen = fbLen;
	if (fbLen >= (1ull << 30)) { rep.stage = "fb-too-large"; return kIOReturnNoSpace; }

	// Backup: first 1 MiB-aligned spot that misses both the screen and the bring-up span.
	auto overlaps = [](uint64_t a, uint64_t al, uint64_t b, uint64_t bl) { return a < b + bl && b < a + al; };
	const uint64_t cands[2] = { (dev.vramBase + kSpan + kOneMiB - 1) & ~(kOneMiB - 1),
	                            (fbOff + fbLen + kOneMiB - 1) & ~(kOneMiB - 1) };
	rep.backupOff = ~0ull;
	for (uint64_t c : cands)
		if (c + fbLen <= dev.vramLimit && !overlaps(c, fbLen, fbOff, fbLen) && !overlaps(c, fbLen, dev.vramBase, kSpan)) {
			rep.backupOff = c; break;
		}
	if (rep.backupOff == ~0ull) { rep.stage = "no-room-for-backup"; return kIOReturnNoSpace; }

	const uint64_t fbMc = dev.vramMC(fbOff), bkMc = dev.vramMC(rep.backupOff);
	rep.bandLen = (fbLen / 16) & ~3ull;

	// 1. screen -> backup
	copyIb(dev, kS8IbOff, fbMc, bkMc, fbLen);
	if (!sdmaSubmit(dev, kS8IbOff, 8, 8, timeoutUs, rep.backupUs)) { rep.stage = "backup-copy-timeout"; return kIOReturnTimeout; }
	rep.backupMismatches = sampleCompare(dev, rep.backupOff, fbOff, fbLen, 0);
	if (rep.backupMismatches > 8) {   // the console may scroll a line meanwhile; a real failure misses everywhere
		rep.stage = "backup-copy-wrong-data"; return kIOReturnIOError;
	}

	// 2. white band over the top 1/16 of the screen
	{
		const uint32_t ib[8] = { kSDMA_OP_CONST_FILL | kFILLSIZE_DWORD, (uint32_t)fbMc, (uint32_t)(fbMc >> 32),
		                         kBandColor, (uint32_t)(rep.bandLen - 1), 0, 0, 0 };
		bar0_memcpy_to_vram(dev, slot(dev) + kS8IbOff + 0x100, ib, sizeof(ib));
	}
	if (!sdmaSubmit(dev, kS8IbOff + 0x100, 8, 9, timeoutUs, rep.bandUs)) { rep.stage = "band-fill-timeout"; return kIOReturnTimeout; }
	rep.bandMismatches = sampleCompare(dev, fbOff, ~0ull, rep.bandLen, kBandColor);
	IOSleep(holdMs);

	// 3. band <- backup
	copyIb(dev, kS8IbOff + 0x200, bkMc, fbMc, rep.bandLen);
	const bool restored = sdmaSubmit(dev, kS8IbOff + 0x200, 8, 10, timeoutUs, rep.restoreUs);
	rep.restoreMismatches = sampleCompare(dev, fbOff, rep.backupOff, rep.bandLen, 0);
	if (!restored) { rep.stage = "restore-timeout"; return kIOReturnTimeout; }
	rep.passed = rep.bandMismatches == 0 && rep.restoreMismatches <= 8;
	rep.stage = rep.passed ? "screen-ok" : (rep.bandMismatches ? "band-wrong-data" : "restore-wrong-data");
	UCODE_LOG("navi33-gfx: screen test %s — FB vram+%#llx (%llu KiB), backup vram+%#llx in %u us (%u bad), band %llu KiB in %u us (%u bad), restore %u us (%u bad)",
	          rep.stage, (unsigned long long)fbOff, (unsigned long long)(fbLen >> 10), (unsigned long long)rep.backupOff,
	          rep.backupUs, rep.backupMismatches, (unsigned long long)(rep.bandLen >> 10), rep.bandUs, rep.bandMismatches,
	          rep.restoreUs, rep.restoreMismatches);
	return rep.passed ? kIOReturnSuccess : kIOReturnIOError;
}

} // namespace amdgpu
