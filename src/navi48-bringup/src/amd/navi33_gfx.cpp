//
//  navi33_gfx.cpp — see navi33_gfx.h. Ported from Linux master gfxhub_v3_0.c and
//  sdma_v6_0.c; every offset/field below is quoted from gc_11_0_0_offset.h /
//  gc_11_0_0_sh_mask.h / gc_11_0_0_default.h.
//
#include "navi33_gfx.h"
#include "amdgpu_log.h"

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
		if (RREG32(dev, gc0(dev, R::GCVM_INVALIDATE_ENG17_ACK)) & 1u) { rep.tlbFlushAcked = true; break; }
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

} // namespace amdgpu
