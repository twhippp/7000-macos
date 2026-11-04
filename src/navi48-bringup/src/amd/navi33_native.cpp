//
//  navi33_native.cpp - see navi33_native.h.
//
//  Memory: every BO lives in BAR0-visible VRAM, [vramBase + 32 MiB, bar0Size) minus the console FB, in 64 KiB
//  blocks (so every BO is CPU-mappable; a GTT request is placed there too and reported as VRAM). Page tables
//  come from an 8 MiB arena of that pool, taken at open. The client's tree hangs off GCVM_CONTEXT8.
//  Submission: the kernel gfx queue (stage 9), IBs with vmid 8, the user fence and then the kernel seqno by
//  RELEASE_MEM. TLB flushes go through the compute queue (navi33_flush_vmid).
//
#include <string.h>
#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include "navi33_native.h"
#include "navi33_gfx.h"
#include "native_s1c_pure.h"
#include "native_hostimport_pure.h"
#include "amdgpu_log.h"

#define N33N_LOG(fmt, ...) AMDGPU_LOG("navi33-native", fmt, ##__VA_ARGS__)

namespace amdgpu {
namespace {
using namespace n48native;
using namespace n48native::s1c;

constexpr uint32_t kVmid = 8;
constexpr uint32_t kBuild = 33012;                 // reported by Hello / QueryInfo
// The ABI minor this engine reports (Hello, QueryInfo.reserved[2]): 9, not the header's current minor. 1.9's host import
// (21) is here (n33n_bo_import_host); what 1.10..1.12 added is not on gfx11 yet: the per-session VRAM budget in QueryInfo
// (1.10), the multi-display scanout (22..26). A client that sees 9 does not try them (Mesa's budget and scanx paths are gated on it).
constexpr uint32_t kAbiMinor = 9u;
constexpr uint64_t kBlk = 64ull << 10;
constexpr uint32_t kMaxBlk = 4096;                 // 256 MiB of BAR0 / 64 KiB
constexpr uint64_t kPtArena = 8ull << 20;
constexpr uint32_t kPtPages = (uint32_t)(kPtArena / kPage);
constexpr uint64_t kSeqOff = 0x1B000;              // in the kernel slot: the native seqno (8 bytes)
// gfx11 PTE (gmc_v11 / amdgpu_vm.h): no IS_PTE bit; MTYPE at 50:48
constexpr uint64_t kV = 1ull << 0, kSys = 1ull << 1, kSnoop = 1ull << 2, kX = 1ull << 4, kR = 1ull << 5, kW = 1ull << 6;
constexpr uint64_t kMtUC = 3ull << 48, kAddr = 0x0000FFFFFFFFF000ull;
// GCVM_CONTEXT8 (gc_11_0_0_offset.h, BASE_IDX 0)
constexpr uint32_t kCtxBaseLo = 0x16f3 + 2 * kVmid, kCtxBaseHi = 0x16f4 + 2 * kVmid;
// regGB_ADDR_CONFIG (gc_11_0_0_offset.h): 0x13de, BASE_IDX 0. navi33_gc_reg adds GC seg 0 (0x1260),
// giving the absolute 0x263e that Mesa reads (READ_MMR_REG dword_offset). Not 0x263e here: that
// would be added to the base a second time.
constexpr uint32_t kGbAddrConfig = 0x13de;

// kind 0: VRAM blocks [blk, blk + nblk). kind 1 (host import, ABI 1.9): the caller's wired pages hpages[0 .. hpageCount), held by hmd (prepare()d).
struct Bo { uint8_t used, kind; uint32_t blk, nblk; uint64_t off, size; IOMemoryDescriptor *hmd; uint64_t *hpages; uint32_t hpageCount; };
constexpr uint8_t kBoVram = 0, kBoHost = 1;

DeviceContext *gDev;
bool gAvail;
uint64_t gFbOff;                                    // GCMC_VM_FB_OFFSET: VRAM offset -> PA
uint8_t gBlkState[kMaxBlk];                         // 0 free, 1 used, 2 reserved
uint32_t gBlkFirst, gBlkEnd;
IOLock *gLock;
volatile UInt32 gOpen;

struct Session {
	bool hello;
	uint8_t boUsed[N48N_MAX_BOS];
	Bo bo[N48N_MAX_BOS];
	uint8_t ctxUsed[N48N_MAX_CTX + 1];
	VaEnt maps[kMaxMaps];
	uint32_t ptBlk;                                 // first block of the PT arena
	uint8_t ptUsed[kPtPages];
	uint64_t rootPa;
	uint64_t emitted;
	uint64_t vramUsed;
	uint64_t importedBytes;
};
Session gS;
uint32_t gSubmit[3 + 4 * N48N_MAX_IBS + 16];

// ---- scanout, copy-to-console variant ----
// Navi 33's display engine (DCN 3.2) is not driven here. The boot console the GOP left lit stays the scanout
// surface; a Present COPIES the registered buffer into it on the gfx queue (CP DMA in an IB at VMID 0, after a
// WAIT on the seqno so every earlier submit has finished), then a RELEASE_MEM writes back L2 and bumps the seqno.
// Acquire saves the console into a backup in the pool; Release (and the close of the client) copies it back.
// Nothing here writes a display register, so a fault costs a picture or a GPU hang, never the display state.
struct Console { uint64_t off, len; uint32_t w, h, row; bool ok; };
Console gCon;
uint64_t gT0;
constexpr uint32_t kDmaChunk = 32736;     // gfx11 CP DMA: 32767 bytes per packet, 32-aligned (radv_cp_dma.c)
constexpr uint32_t kDmaDw = 7;            // PACKET3(DMA_DATA, 5)
enum : uint32_t { kIbSave = N48N_SCAN_MAX_SLOTS, kIbRestore = N48N_SCAN_MAX_SLOTS + 1, kIbCount = N48N_SCAN_MAX_SLOTS + 2 };
struct ScanSlot { bool used; uint32_t handle; uint64_t mc; uint32_t presents; uint64_t lastSeq; };
struct Scan {
	bool acquired;
	uint32_t backupBlk, backupNblk;       // the saved console
	uint32_t ibBlk, ibNblk;               // kIbCount IBs of ibBytes each
	uint64_t ibBytes;                     // one IB, rounded up to 256 bytes
	uint32_t ibDw;                        // dwords of one copy IB
	ScanSlot slot[N48N_SCAN_MAX_SLOTS];
	uint32_t front;
	uint64_t presents, refused, lastPresentSeq;
};
Scan gScan;

inline uint64_t pa_of(uint64_t vramOff) { return gFbOff + vramOff; }
inline uint64_t off_of(uint64_t pa) { return pa - gFbOff; }
inline uint64_t slot_off() { return navi33_slot_off(*gDev); }
inline uint64_t retired() { const uint64_t r = RBAR0_64(*gDev, slot_off() + kSeqOff); return r > gS.emitted ? gS.emitted : r; }
uint64_t now_ns() { uint64_t t; absolutetime_to_nanoseconds(mach_absolute_time(), &t); return t; }

// ---- the block pool ----
bool blk_alloc(uint32_t n, uint32_t alignBlk, uint32_t *first) {
	if (alignBlk == 0) alignBlk = 1;
	for (uint32_t i = (gBlkFirst + alignBlk - 1) / alignBlk * alignBlk; i + n <= gBlkEnd; i += alignBlk) {
		uint32_t k = 0;
		while (k < n && gBlkState[i + k] == 0) k++;
		if (k == n) { for (k = 0; k < n; k++) gBlkState[i + k] = 1; *first = i; return true; }
	}
	return false;
}
void blk_free(uint32_t first, uint32_t n) { for (uint32_t k = 0; k < n; k++) if (gBlkState[first + k] == 1) gBlkState[first + k] = 0; }
uint64_t pool_free_bytes() { uint64_t f = 0; for (uint32_t i = gBlkFirst; i < gBlkEnd; i++) f += gBlkState[i] == 0 ? kBlk : 0; return f; }

// ---- the page-table tree ----
uint64_t pt_alloc() {
	for (uint32_t i = 0; i < kPtPages; i++)
		if (!gS.ptUsed[i]) {
			gS.ptUsed[i] = 1;
			const uint64_t off = (uint64_t)gS.ptBlk * kBlk + (uint64_t)i * kPage;
			bar0_memset_vram(*gDev, off, 0u, kPage);
			return pa_of(off);
		}
	return 0;
}
inline uint64_t pt_rd(uint64_t tblPa, uint32_t i) { return RBAR0_64(*gDev, off_of(tblPa) + i * 8ull); }
inline void pt_wr(uint64_t tblPa, uint32_t i, uint64_t v) { WBAR0_64(*gDev, off_of(tblPa) + i * 8ull, v); }
// The PTB for `va`, creating the chain if `create`. 0 when absent / out of table pages.
uint64_t pt_ptb(uint64_t va, bool create) {
	uint64_t tbl = gS.rootPa;
	const uint32_t idx[3] = { idx_pdb2(va), idx_pdb1(va), idx_pdb0(va) };
	for (uint32_t l = 0; l < 3; l++) {
		uint64_t e = pt_rd(tbl, idx[l]);
		if (!(e & kV)) {
			if (!create) return 0;
			const uint64_t pa = pt_alloc();
			if (!pa) return 0;
			e = pa | kV;
			pt_wr(tbl, idx[l], e);
		}
		tbl = e & kAddr;
	}
	return tbl;
}
uint64_t leaf(uint32_t vmflags) {
	return kV | ((vmflags & N48N_VM_PAGE_READABLE) ? kR : 0) | ((vmflags & N48N_VM_PAGE_WRITEABLE) ? kW : 0) |
	       ((vmflags & N48N_VM_PAGE_EXECUTABLE) ? kX : 0) | (((vmflags & N48N_VM_MTYPE_MASK) == N48N_VM_MTYPE_UC) ? kMtUC : 0);
}
uint32_t pt_map(uint64_t va, uint64_t pages, uint64_t firstPa, uint64_t flags) {
	for (uint64_t p = 0; p < pages; p += 512 - idx_ptb(va + p * kPage))
		if (!pt_ptb(va + p * kPage, true)) return kNoMemory;
	for (uint64_t p = 0; p < pages; p++) pt_wr(pt_ptb(va + p * kPage, false), idx_ptb(va + p * kPage), ((firstPa + p * kPage) & kAddr) | flags);
	return kOk;
}
void pt_unmap(uint64_t va, uint64_t pages) {
	for (uint64_t p = 0; p < pages; p++) {
		const uint64_t ptb = pt_ptb(va + p * kPage, false);
		if (ptb) pt_wr(ptb, idx_ptb(va + p * kPage), 0);
	}
}
uint32_t flush() { amdgpu_hdp_flush(*gDev); return navi33_flush_vmid(*gDev, kVmid) ? kOk : kTimeout; }
bool idle_wait(uint64_t ns = 2000000000ull) {
	const uint64_t t0 = now_ns();
	while (retired() < gS.emitted) { if (now_ns() - t0 > ns) return false; IODelay(20); }
	return true;
}
// 0.0.675: at most kMaxInflight submits between the emitted and the retired seqno. Every emit is at most kMaxEmitDw dwords and the
// gfx ring holds 2048, so kMaxInflight * kMaxEmitDw (+ the copy IBs' 16) always fits: navi33_gfx_emit can skip its rptr check
// (that register reads 0 on this card). Waits up to 2 s for room; false = the GPU stopped retiring (a real hang).
constexpr uint64_t kMaxInflight = 4;
bool room_wait() {
	const uint64_t t0 = now_ns();
	while (gS.emitted - retired() >= kMaxInflight) { if (now_ns() - t0 > 2000000000ull) return false; IODelay(20); }
	return true;
}
bool hello() { return __atomic_load_n(&gOpen, __ATOMIC_ACQUIRE) && gS.hello; }

// Unwire a host import's pages (only once the GPU can no longer touch them: after idle and the unmap).
void host_release(Bo &b) {
	if (b.hpages) IOFree(b.hpages, (vm_size_t)b.hpageCount * sizeof(uint64_t));
	if (b.hmd) { b.hmd->complete(); b.hmd->release(); }
	b.hpages = nullptr; b.hmd = nullptr; b.hpageCount = 0;
}
// Map `pages` 4 KiB pages at `va` to the scattered list pagePa (a host import). Tables first, then the leaves: a failure leaves no leaf written.
uint32_t pt_map_list(uint64_t va, uint64_t pages, const uint64_t *pagePa, uint64_t flags) {
	for (uint64_t p = 0; p < pages; p += 512 - idx_ptb(va + p * kPage))
		if (!pt_ptb(va + p * kPage, true)) return kNoMemory;
	for (uint64_t p = 0; p < pages; p++) pt_wr(pt_ptb(va + p * kPage, false), idx_ptb(va + p * kPage), (pagePa[p] & kAddr) | flags);
	return kOk;
}

void release_all() {
	for (uint32_t h = 1; h < N48N_MAX_BOS; h++) {
		if (!gS.boUsed[h]) continue;
		if (gS.bo[h].kind == kBoHost) host_release(gS.bo[h]); else blk_free(gS.bo[h].blk, gS.bo[h].nblk);
	}
	blk_free(gS.ptBlk, (uint32_t)(kPtArena / kBlk));
}
} // namespace

void n33n_attach(DeviceContext &dev, uint64_t bootFbOff, uint64_t bootFbLen, uint32_t conW, uint32_t conH, uint32_t conRow) {
	gDev = &dev;
	gCon = Console{};
	if (bootFbOff != ~0ull && bootFbLen && conW && conH && conRow && (conRow & 3u) == 0 && conRow / 4u >= conW &&
	    (uint64_t)conRow * conH == bootFbLen && bootFbOff + bootFbLen <= dev.bar0Size && (bootFbOff & 255u) == 0)
		gCon = Console{ bootFbOff, bootFbLen, conW, conH, conRow, true };
	gT0 = now_ns();
	gFbOff = (uint64_t)RREG32(dev, navi33_gc_reg(dev, 0x15a7 /*GCMC_VM_FB_OFFSET*/, 0)) << 24;
	gBlkFirst = (uint32_t)((dev.vramBase + (32ull << 20) + kBlk - 1) / kBlk);
	gBlkEnd = (uint32_t)(dev.bar0Size / kBlk);
	if (gBlkEnd > kMaxBlk) gBlkEnd = kMaxBlk;
	memset(gBlkState, 0, sizeof(gBlkState));
	if (bootFbOff != ~0ull && bootFbLen)
		for (uint64_t b = bootFbOff / kBlk; b <= (bootFbOff + bootFbLen - 1) / kBlk && b < kMaxBlk; b++) gBlkState[b] = 2;
	if (!gLock) gLock = IOLockAlloc();
	gAvail = gLock != nullptr && gBlkEnd > gBlkFirst + (uint32_t)(kPtArena / kBlk) && navi33_queues_ready();
	N33N_LOG("attach: pool vram+[%#llx, %#llx) %s, fb offset %#llx, console %ux%u row %u at vram+%#llx%s", (unsigned long long)gBlkFirst * kBlk,
	         (unsigned long long)gBlkEnd * kBlk, gAvail ? "available" : "NOT available", (unsigned long long)gFbOff,
	         conW, conH, conRow, (unsigned long long)bootFbOff, gCon.ok ? "" : " (no scanout: geometry unusable)");
}
bool n33n_available() { return gAvail; }
bool n33n_is_open() { return __atomic_load_n(&gOpen, __ATOMIC_ACQUIRE) != 0; }
bool n33n_hello_done() { return hello(); }
bool n33n_scan_acquired() { return __atomic_load_n(&gScan.acquired, __ATOMIC_ACQUIRE); }   // 0.0.670: the display pipe's "a native client owns the plane"

IOReturn n33n_open() {
	if (!gAvail) return kIOReturnNotReady;
	if (!OSCompareAndSwap(0, 1, &gOpen)) return kIOReturnExclusiveAccess;
	IOLockLock(gLock);
	memset(&gS, 0, sizeof(gS));
	IOReturn rc = kIOReturnSuccess;
	if (!blk_alloc((uint32_t)(kPtArena / kBlk), 1, &gS.ptBlk)) rc = kIOReturnNoMemory;
	else if (!(gS.rootPa = pt_alloc())) rc = kIOReturnNoMemory;
	if (rc == kIOReturnSuccess) {
		WBAR0_64(*gDev, slot_off() + kSeqOff, 0);
		const uint64_t pd = gS.rootPa | kV;
		WREG32(*gDev, navi33_gc_reg(*gDev, kCtxBaseLo, 0), (uint32_t)pd);
		WREG32(*gDev, navi33_gc_reg(*gDev, kCtxBaseHi, 0), (uint32_t)(pd >> 32));
		if (flush() != kOk) rc = kIOReturnTimeout;
	}
	if (rc != kIOReturnSuccess) { release_all(); __atomic_store_n(&gOpen, 0u, __ATOMIC_RELEASE); }
	IOLockUnlock(gLock);
	N33N_LOG("open: %s, root pa %#llx", rc == kIOReturnSuccess ? "ok" : "refused", (unsigned long long)gS.rootPa);
	return rc;
}

namespace { bool scan_end_locked(); }   // below, with the scanout

void n33n_close(const char *how) {
	if (!n33n_is_open()) return;
	IOLockLock(gLock);
	(void)scan_end_locked();                          // the console goes back before anything is freed
	const bool idle = idle_wait();
	if (idle) release_all();                          // a busy GPU keeps its memory (leaked until reboot)
	gS.hello = false;
	__atomic_store_n(&gOpen, 0u, __ATOMIC_RELEASE);
	IOLockUnlock(gLock);
	N33N_LOG("close (%s): %s", how, idle ? "released" : "GPU busy, memory kept");
}

IOReturn n33n_hello(uint64_t clientAbi, uint64_t flags, uint64_t out[4]) {
	if (!n33n_is_open()) return kIOReturnNotReady;
	if ((flags & ~(uint64_t)N48N_HELLO_F_MINOR) != 0) return kIOReturnBadArgument;
	if (clientAbi != N48N_ABI_VERSION) return kIOReturnUnsupported;
	IOLockLock(gLock);
	gS.hello = true;
	out[0] = (flags & N48N_HELLO_F_MINOR) ? (uint64_t)N48N_ABI_VERSION | ((uint64_t)kAbiMinor << 16) : (uint64_t)N48N_ABI_VERSION;
	out[1] = kBuild; out[2] = N48N_MAX_IBS; out[3] = kVmid;
	IOLockUnlock(gLock);
	return kIOReturnSuccess;
}

IOReturn n33n_query_info(n48n_info *o) {
	if (!hello()) return kIOReturnNotReady;
	IOLockLock(gLock);
	memset(o, 0, sizeof(*o));
	o->abi_version = N48N_ABI_VERSION; o->kext_build = kBuild;
	o->flags = N48N_INFO_S1B_POSITIVE | N48N_INFO_NATIVE_BOOT;
	o->vmid = kVmid;
	o->gb_addr_config = RREG32(*gDev, navi33_gc_reg(*gDev, kGbAddrConfig, 0));
	o->gc_version = (11u << 16) | (0u << 8) | 2u;
	o->ring_size_dw = 2048; o->max_ibs = N48N_MAX_IBS;
	o->vram_vis_total = (uint64_t)(gBlkEnd - gBlkFirst) * kBlk; o->vram_vis_free = pool_free_bytes();
	o->gtt_cap = kGttCap; o->gtt_max_bo = kGttMaxBo;
	o->pt_cap = kPtArena;
	for (uint32_t i = 0; i < kPtPages; i++) o->pt_used += gS.ptUsed[i] ? kPage : 0;
	o->reserved[2] = kAbiMinor;
	o->seq_emitted = gS.emitted; o->seq_retired = retired();
	o->va_low_first = kVaFirst; o->va_low_last = 0x00007FFFFFFFFFFFull;
	o->va_high_first = 0xFFFF800000000000ull; o->va_high_last = 0xFFFFFFFFFFFFFFFFull;
	o->max_bos = N48N_MAX_BOS; o->fence_slots = N48N_FENCE_SLOTS;
	IOLockUnlock(gLock);
	return kIOReturnSuccess;
}

IOReturn n33n_read_regs(uint64_t off, uint64_t count, uint64_t instance, uint32_t *out) {
	if (!hello()) return kIOReturnNotReady;
	const uint64_t first = navi33_gc_reg(*gDev, kGbAddrConfig, 0);
	// 0.0.667: besides Mesa's GB_ADDR_CONFIG window, the GRBM / CP status block (gc 0x0d00..0x0fff) and the gfx/compute HQD block
	// (gc 0x1e00..0x1fff), BASE_IDX 0: plain status registers, read-only diagnostics (n48nub regs).
	auto in = [&](uint32_t lo, uint32_t hi) { const uint64_t a = navi33_gc_reg(*gDev, lo, 0), b = navi33_gc_reg(*gDev, hi, 0); return off >= a && off + count <= b; };
	if (count < 1 || count > 16 || instance != 0xFFFFFFFFull) return kIOReturnBadArgument;
	if (!(off >= first && off + count <= first + 4) && !in(0x0d00, 0x1000) && !in(0x1e00, 0x2000)) return kIOReturnBadArgument;
	for (uint64_t i = 0; i < count; i++) out[i] = RREG32(*gDev, (uint32_t)(off + i));
	return kIOReturnSuccess;
}

IOReturn n33n_bo_create(const n48n_gem_create_in *in, uint64_t out[4]) {
	if (!hello()) return kIOReturnNotReady;
	const Place p = place_decide(in->bo_size, in->alignment, in->domains, in->domain_flags);
	if (p.rc != kOk) return (IOReturn)p.rc;
	IOLockLock(gLock);
	IOReturn rc = kIOReturnSuccess;
	do {
		const uint32_t h = lowest_free(gS.boUsed, 1, N48N_MAX_BOS - 1);
		if (h == 0) { rc = kIOReturnNoResources; break; }
		Bo b {};
		b.nblk = (uint32_t)((p.size + kBlk - 1) / kBlk);
		const uint32_t al = p.align > kBlk ? (uint32_t)(p.align / kBlk) : 1u;
		if (!blk_alloc(b.nblk, al, &b.blk)) { rc = kIOReturnNoMemory; break; }
		b.used = 1; b.off = (uint64_t)b.blk * kBlk; b.size = p.size;
		bar0_memset_vram(*gDev, b.off, 0u, (uint64_t)b.nblk * kBlk);
		amdgpu_hdp_flush(*gDev);
		gS.bo[h] = b; gS.boUsed[h] = 1; gS.vramUsed += (uint64_t)b.nblk * kBlk;
		out[0] = h; out[1] = p.size; out[2] = N48N_GEM_DOMAIN_VRAM; out[3] = N48N_PLACED_CPU_MAPPABLE | N48N_PLACED_ZEROED;
	} while (false);
	IOLockUnlock(gLock);
	return rc;
}

IOReturn n33n_bo_free(uint64_t handle) {
	if (!hello()) return kIOReturnNotReady;
	IOLockLock(gLock);
	IOReturn rc = kIOReturnSuccess;
	if (handle > 0xFFFFFFFFull || !id_live(gS.boUsed, N48N_MAX_BOS - 1, (uint32_t)handle)) rc = kIOReturnNotFound;
	else {
		const uint32_t h = (uint32_t)handle;
		const bool idle = idle_wait();
		bool unmapped = false;
		for (uint32_t i = 0; i < kMaxMaps; i++)
			if (va_ent_used(gS.maps[i]) && gS.maps[i].handle == h) { if (idle) pt_unmap(gS.maps[i].start, gS.maps[i].size / kPage); gS.maps[i] = VaEnt{0, 0, 0, 0, 0}; unmapped = true; }
		if (unmapped && idle) (void)flush();
		for (uint32_t i = 0; i < N48N_SCAN_MAX_SLOTS; i++)
			if (gScan.slot[i].used && gScan.slot[i].handle == h) gScan.slot[i] = ScanSlot{};   // a freed BO is no longer presentable
		if (idle && gS.bo[h].kind == kBoHost) { gS.importedBytes = import_after_free(gS.importedBytes, gS.bo[h].size); host_release(gS.bo[h]); }
		else if (idle) { blk_free(gS.bo[h].blk, gS.bo[h].nblk); gS.vramUsed -= (uint64_t)gS.bo[h].nblk * kBlk; }
		else rc = kIOReturnTimeout;                     // the memory is leaked (a host import stays wired): the GPU may still use it
		gS.boUsed[h] = 0; gS.bo[h] = Bo{};
	}
	IOLockUnlock(gLock);
	return rc;
}

IOReturn n33n_gem_va(const n48n_gem_va *in) {
	if (!hello()) return kIOReturnNotReady;
	const VaReq q = gemva_check(in->operation, in->handle, in->_pad, in->flags, in->va_address, in->offset_in_bo, in->map_size,
	                            in->vm_timeline_point, in->vm_timeline_syncobj_out, in->num_syncobj_handles, in->input_fence_syncobj_handles);
	if (q.rc != kOk) { N33N_LOG("gem_va REFUSED by the argument check rc %#x: op %u handle %u va %#llx size %llu KiB", (unsigned)q.rc, (unsigned)in->operation, (unsigned)in->handle, (unsigned long long)in->va_address, (unsigned long long)(in->map_size >> 10)); return (IOReturn)q.rc; }
	IOLockLock(gLock);
	IOReturn rc = kIOReturnSuccess;
	do {
		if (!id_live(gS.boUsed, N48N_MAX_BOS - 1, in->handle)) { rc = kIOReturnNotFound; break; }
		const Bo &b = gS.bo[in->handle];
		const bool host = b.kind == kBoHost;
		if (!range_in_bo(in->offset_in_bo, in->map_size, host ? b.size : (uint64_t)b.nblk * kBlk)) { rc = kIOReturnBadArgument; break; }
		if (in->operation == N48N_VA_OP_MAP) {
			if (va_overlap(gS.maps, kMaxMaps, q.stripped, in->map_size) >= 0) { rc = kIOReturnBadArgument; break; }
			const int slot = va_insert(gS.maps, kMaxMaps, VaEnt{ q.stripped, in->map_size, in->offset_in_bo, in->handle, in->flags });
			if (slot < 0) { rc = kIOReturnNoResources; break; }
			const uint32_t r = host ? pt_map_list(q.stripped, in->map_size / kPage, b.hpages + in->offset_in_bo / kPage, leaf(in->flags) | kSys | kSnoop)
			                        : pt_map(q.stripped, in->map_size / kPage, pa_of(b.off + in->offset_in_bo), leaf(in->flags));
			if (r != kOk) { pt_unmap(q.stripped, in->map_size / kPage); gS.maps[slot] = VaEnt{0, 0, 0, 0, 0}; rc = (IOReturn)r; break; }
			rc = (IOReturn)flush();
		} else {
			const int hit = va_find_exact(gS.maps, kMaxMaps, in->handle, q.stripped, in->map_size);
			if (hit < 0) { rc = kIOReturnBadArgument; break; }
			if (!idle_wait()) { rc = kIOReturnTimeout; break; }
			pt_unmap(q.stripped, in->map_size / kPage);
			gS.maps[hit] = VaEnt{0, 0, 0, 0, 0};
			rc = (IOReturn)flush();
		}
	} while (false);
	IOLockUnlock(gLock);
	if (rc != kIOReturnSuccess) {   // 0.0.677: a refused map was silent; WindowServer's NoCopy imports may die here (after a good BoImportHost)
		static uint32_t n = 0;
		if (n++ < 64) N33N_LOG("gem_va REFUSED rc %#x: op %u handle %u va %#llx size %llu KiB off %#llx flags %#x", (unsigned)rc, (unsigned)in->operation, (unsigned)in->handle,
		                       (unsigned long long)in->va_address, (unsigned long long)(in->map_size >> 10), (unsigned long long)in->offset_in_bo, (unsigned)in->flags);
	}
	return rc;
}

IOReturn n33n_ctx(const n48n_ctx *in, n48n_ctx *out) {
	if (!hello()) return kIOReturnNotReady;
	IOLockLock(gLock);
	memset(out, 0, sizeof(*out));
	IOReturn rc = kIOReturnSuccess;
	switch (in->op) {
	case N48N_CTX_OP_ALLOC: {
		const uint32_t id = lowest_free(gS.ctxUsed, 1, N48N_MAX_CTX);
		if (id == 0) rc = kIOReturnNoResources; else { gS.ctxUsed[id] = 1; out->op = id; }
		break;
	}
	case N48N_CTX_OP_FREE:
		if (!id_live(gS.ctxUsed, N48N_MAX_CTX, in->ctx_id)) rc = kIOReturnNotFound; else gS.ctxUsed[in->ctx_id] = 0;
		break;
	case N48N_CTX_OP_QUERY_STATE2:
		if (!id_live(gS.ctxUsed, N48N_MAX_CTX, in->ctx_id)) rc = kIOReturnNotFound;
		break;
	default: rc = kIOReturnBadArgument;
	}
	IOLockUnlock(gLock);
	return rc;
}

static uint32_t release_mem(uint32_t *o, uint64_t mc, uint64_t data) {
	o[0] = (3u << 30) | ((6u & 0x3FFF) << 16) | (0x49u << 8);   // PACKET3(RELEASE_MEM, 6)
	o[1] = navi33_release_mem_dw1();
	o[2] = 2u << 29;                                              // DATA_SEL 2: 64-bit, INT_SEL 0
	o[3] = (uint32_t)mc & 0xFFFFFFF8u; o[4] = (uint32_t)(mc >> 32);
	o[5] = (uint32_t)data; o[6] = (uint32_t)(data >> 32); o[7] = 0;
	return 8;
}

IOReturn n33n_submit(const uint8_t *in, uint32_t size, uint64_t *seqOut) {
	if (!hello()) return kIOReturnNotReady;
	CsView v;
	const uint32_t prc = cs_parse(in, size, &v);
	if (prc != kOk) return (IOReturn)prc;
	IOLockLock(gLock);
	IOReturn rc = kIOReturnSuccess;
	do {
		if (!id_live(gS.ctxUsed, N48N_MAX_CTX, v.ctx)) { rc = kIOReturnNotFound; break; }
		const bool hasFence = (v.flags & N48N_CS_HAS_FENCE) != 0u;
		if (hasFence && (!id_live(gS.boUsed, N48N_MAX_BOS - 1, v.fenceHandle) || !cs_fence_in_bo(v.fenceOffset, gS.bo[v.fenceHandle].size))) {
			rc = hasFence && !id_live(gS.boUsed, N48N_MAX_BOS - 1, v.fenceHandle) ? kIOReturnNotFound : kIOReturnBadArgument; break;
		}
		uint32_t k = 0;
		gSubmit[k++] = (3u << 30) | (1u << 16) | (0x28u << 8);    // PACKET3(CONTEXT_CONTROL, 1)
		gSubmit[k++] = 0x80000000u;
		gSubmit[k++] = 0;
		bool bad = false;
		for (uint32_t i = 0; i < v.nIbs; i++) {
			uint64_t va; uint32_t by;
			cs_ib(in, i, &va, &by);
			const uint64_t st = va_strip(va);
			if (st < kVaFirst || !va_exec_covered(gS.maps, kMaxMaps, st, by)) { bad = true; break; }
			gSubmit[k++] = (3u << 30) | (2u << 16) | (0x3Fu << 8);   // PACKET3(INDIRECT_BUFFER, 2)
			gSubmit[k++] = (uint32_t)va & 0xFFFFFFFCu;
			gSubmit[k++] = (uint32_t)(va >> 32);
			gSubmit[k++] = (by / 4u) | (kVmid << 24);
		}
		if (bad) { rc = kIOReturnBadArgument; break; }
		const uint64_t seq = gS.emitted + 1;
		if (hasFence) k += release_mem(gSubmit + k, gDev->vramMC(gS.bo[v.fenceHandle].off + v.fenceOffset), seq);
		k += release_mem(gSubmit + k, gDev->vramMC(slot_off() + kSeqOff), seq);
		amdgpu_hdp_flush(*gDev);                              // the CPU's writes through BAR0 reach VRAM first
		static_assert(sizeof(gSubmit) / 4 * kMaxInflight <= 2048 - 64, "kMaxInflight full submits fit in the gfx ring");
		if (!room_wait()) { rc = kIOReturnTimeout; break; }
		if (!navi33_gfx_emit(*gDev, gSubmit, k, true)) { rc = kIOReturnTimeout; break; }
		gS.emitted = seq;
		*seqOut = seq;
	} while (false);
	IOLockUnlock(gLock);
	return rc;
}

IOReturn n33n_wait(uint64_t target, uint64_t timeoutNs, uint64_t out[3]) {
	if (!hello()) return kIOReturnNotReady;
	uint64_t res = 0;
	const uint32_t r = wait_resolve(target, gS.emitted, &res);
	if (r != kOk) return (IOReturn)r;
	const uint64_t to = wait_timeout_ns(timeoutNs), t0 = now_ns();
	for (;;) {
		const uint64_t em = gS.emitted, re = retired();
		if (res == 0 || re >= res) { out[0] = 0; out[1] = re; out[2] = em; return kIOReturnSuccess; }
		const uint64_t el = now_ns() - t0;
		if (el >= to) { out[0] = 1; out[1] = re; out[2] = em; return kIOReturnSuccess; }
		if (el < 5000000ull) IODelay(2); else IOSleep(1);
	}
}

namespace {
uint64_t frame_now() { return (now_ns() - gT0) / 16666667ull; }   // no OTG read on Navi 33: a 60 Hz clock, for pacing only
uint64_t ib_off(uint32_t which) { return (uint64_t)gScan.ibBlk * kBlk + (uint64_t)which * gScan.ibBytes; }

// One copy IB: DMA_DATA packets moving `len` bytes from srcMc to dstMc through L2 (as RADV's CP DMA does on gfx11);
// CP_SYNC on the last so the ME waits for every DMA before the RELEASE_MEM that follows the IB.
void build_copy_ib(uint32_t which, uint64_t srcMc, uint64_t dstMc, uint64_t len) {
	const uint64_t base = ib_off(which);
	uint32_t pk[kDmaDw];
	uint32_t dw = 0;
	for (uint64_t done = 0; done < len; done += kDmaChunk) {
		const uint32_t n = (uint32_t)((len - done) < kDmaChunk ? (len - done) : kDmaChunk);
		const bool last = done + n >= len;
		pk[0] = (3u << 30) | (5u << 16) | (0x50u << 8);                       // PACKET3(DMA_DATA, 5)
		pk[1] = (3u << 20) /*DST_SEL L2*/ | (3u << 29) /*SRC_SEL L2*/ | (last ? 1u << 31 : 0u) /*CP_SYNC*/;   // ENGINE ME
		pk[2] = (uint32_t)(srcMc + done); pk[3] = (uint32_t)((srcMc + done) >> 32);
		pk[4] = (uint32_t)(dstMc + done); pk[5] = (uint32_t)((dstMc + done) >> 32);
		pk[6] = n;                                                             // BYTE_COUNT
		bar0_memcpy_to_vram(*gDev, base + (uint64_t)dw * 4, pk, sizeof(pk));
		dw += kDmaDw;
	}
}

// Ring: wait until every earlier submit has retired, run IB `which` (VMID 0), then bump the seqno. Caller holds gLock.
bool run_copy_ib(uint32_t which, uint64_t *seqOut) {
	uint32_t k = 0;
	gSubmit[k++] = (3u << 30) | (5u << 16) | (0x3Cu << 8);                     // PACKET3(WAIT_REG_MEM, 5)
	gSubmit[k++] = 5u /*>=*/ | (1u << 4) /*memory*/;                            // operation: wait; engine: ME
	const uint64_t seqMc = gDev->vramMC(slot_off() + kSeqOff);
	gSubmit[k++] = (uint32_t)seqMc & 0xFFFFFFFCu; gSubmit[k++] = (uint32_t)(seqMc >> 32);
	gSubmit[k++] = (uint32_t)gS.emitted; gSubmit[k++] = 0xFFFFFFFFu; gSubmit[k++] = 4u;   // ref, mask, poll interval
	const uint64_t ibMc = gDev->vramMC(ib_off(which));
	gSubmit[k++] = (3u << 30) | (2u << 16) | (0x3Fu << 8);                     // PACKET3(INDIRECT_BUFFER, 2), as n33n_submit
	gSubmit[k++] = (uint32_t)ibMc & 0xFFFFFFFCu; gSubmit[k++] = (uint32_t)(ibMc >> 32);
	gSubmit[k++] = gScan.ibDw | (0u << 24);                                     // VMID 0: the IB and its addresses are MC
	const uint64_t seq = gS.emitted + 1;
	k += release_mem(gSubmit + k, seqMc, seq);
	amdgpu_hdp_flush(*gDev);
	if (!room_wait() || !navi33_gfx_emit(*gDev, gSubmit, k, true)) return false;
	gS.emitted = seq;
	if (seqOut) *seqOut = seq;
	return true;
}

// Puts the console back and frees the scanout memory. Caller holds gLock. True when the restore retired.
bool scan_end_locked() {
	if (!gScan.acquired) return true;
	bool ok = run_copy_ib(kIbRestore, nullptr) && idle_wait();
	if (ok) { blk_free(gScan.backupBlk, gScan.backupNblk); blk_free(gScan.ibBlk, gScan.ibNblk); }   // else leaked: the GPU may still read them
	gScan = Scan{};
	N33N_LOG("scanout: released, console restore %s", ok ? "retired" : "DID NOT retire");
	return ok;
}
} // namespace

IOReturn n33n_scan_query(n48n_scan_query *o) {
	if (!hello()) return kIOReturnNotReady;
	IOLockLock(gLock);
	memset(o, 0, sizeof(*o));
	if (gCon.ok) {
		o->h_active = gCon.w; o->v_active = gCon.h; o->pitch_px = gCon.row / 4u;
		o->hubp_format = N48N_SCAN_FMT_ARGB8888;
		o->flags = N48N_SCANQ_LIT | N48N_SCANQ_NATIVE | N48N_SCANQ_GEOM_OK | (gScan.acquired ? N48N_SCANQ_ACQUIRED : 0u);
		o->frame_count = frame_now();
		o->console_mc = o->plane_mc = o->earliest_mc = gDev->vramMC(gCon.off);   // never flipped: the console stays the surface
		o->acquired = gScan.acquired ? 1u : 0u;
		o->plane_w = gCon.w; o->plane_h = gCon.h;
	}
	IOLockUnlock(gLock);
	return kIOReturnSuccess;
}

IOReturn n33n_scan_acquire(uint64_t flags, uint64_t out[2]) {
	if (!hello()) return kIOReturnNotReady;
	if (flags != 0ull) return kIOReturnBadArgument;
	if (!gCon.ok) return kIOReturnUnsupported;
	IOLockLock(gLock);
	IOReturn rc = kIOReturnSuccess;
	do {
		if (gScan.acquired) { rc = kIOReturnBusy; break; }
		Scan sc {};
		sc.ibDw = (uint32_t)((gCon.len + kDmaChunk - 1) / kDmaChunk) * kDmaDw;
		sc.ibBytes = ((uint64_t)sc.ibDw * 4 + 255) & ~255ull;
		sc.backupNblk = (uint32_t)((gCon.len + kBlk - 1) / kBlk);
		sc.ibNblk = (uint32_t)((sc.ibBytes * kIbCount + kBlk - 1) / kBlk);
		if (!blk_alloc(sc.backupNblk, 1, &sc.backupBlk)) { rc = kIOReturnNoMemory; break; }
		if (!blk_alloc(sc.ibNblk, 1, &sc.ibBlk)) { blk_free(sc.backupBlk, sc.backupNblk); rc = kIOReturnNoMemory; break; }
		sc.front = N48N_SCAN_NO_SLOT;
		gScan = sc;
		const uint64_t conMc = gDev->vramMC(gCon.off), bakMc = gDev->vramMC((uint64_t)sc.backupBlk * kBlk);
		build_copy_ib(kIbSave, conMc, bakMc, gCon.len);
		build_copy_ib(kIbRestore, bakMc, conMc, gCon.len);
		if (!run_copy_ib(kIbSave, nullptr) || !idle_wait()) {
			// the save did not retire: keep the memory (the GPU may still write it), give up the acquisition
			N33N_LOG("scanout: acquire - the console save did not retire");
			gScan = Scan{};
			rc = kIOReturnTimeout; break;
		}
		gScan.acquired = true;
		out[0] = conMc; out[1] = frame_now();
		N33N_LOG("scanout: acquired %ux%u row %u, copy IB %u dwords", gCon.w, gCon.h, gCon.row, sc.ibDw);
	} while (false);
	IOLockUnlock(gLock);
	return rc;
}

IOReturn n33n_scan_register(const n48n_scan_reg *in, uint64_t out[2]) {
	if (!hello()) return kIOReturnNotReady;
	if (in->reserved0 != 0u) return kIOReturnBadArgument;
	IOLockLock(gLock);
	IOReturn rc = kIOReturnSuccess;
	do {
		if (!gScan.acquired) { rc = kIOReturnNotReady; break; }
		if (!id_live(gS.boUsed, N48N_MAX_BOS - 1, in->handle)) { rc = kIOReturnNotFound; break; }
		if (in->format != N48N_SCAN_FMT_ARGB8888 || in->width != gCon.w || in->height != gCon.h || in->pitch_bytes != gCon.row ||
		    (in->offset & 255ull) != 0) { rc = kIOReturnBadArgument; break; }
		const Bo &b = gS.bo[in->handle];
		if (in->offset > (uint64_t)b.nblk * kBlk || gCon.len > (uint64_t)b.nblk * kBlk - in->offset) { rc = kIOReturnBadArgument; break; }
		uint32_t id = N48N_SCAN_NO_SLOT;
		for (uint32_t i = 0; i < N48N_SCAN_MAX_SLOTS && id == N48N_SCAN_NO_SLOT; i++) if (!gScan.slot[i].used) id = i;
		if (id == N48N_SCAN_NO_SLOT) { rc = kIOReturnNoResources; break; }
		const uint64_t mc = gDev->vramMC(b.off + in->offset);
		build_copy_ib(id, mc, gDev->vramMC(gCon.off), gCon.len);
		gScan.slot[id] = ScanSlot{ true, in->handle, mc, 0, 0 };
		out[0] = id; out[1] = mc;
	} while (false);
	IOLockUnlock(gLock);
	return rc;
}

IOReturn n33n_scan_present(uint64_t slot, uint64_t flags, uint64_t out[3]) {
	if (!hello()) return kIOReturnNotReady;
	IOLockLock(gLock);
	IOReturn rc = kIOReturnSuccess;
	do {
		if (!gScan.acquired) { rc = kIOReturnNotReady; break; }
		if (flags != 0ull || slot >= N48N_SCAN_MAX_SLOTS || !gScan.slot[slot].used) { gScan.refused++; rc = kIOReturnBadArgument; break; }
		uint64_t seq = 0;
		if (!run_copy_ib((uint32_t)slot, &seq)) { gScan.refused++; rc = kIOReturnTimeout; break; }
		ScanSlot &sl = gScan.slot[slot];
		sl.presents++; sl.lastSeq = seq;
		gScan.front = (uint32_t)slot; gScan.presents++; gScan.lastPresentSeq = seq;
		out[0] = seq; out[1] = frame_now() + 1; out[2] = 0;
	} while (false);
	IOLockUnlock(gLock);
	return rc;
}

IOReturn n33n_scan_status(n48n_scan_status *o) {
	if (!hello()) return kIOReturnNotReady;
	IOLockLock(gLock);
	memset(o, 0, sizeof(*o));
	const uint64_t re = retired();
	o->acquired = gScan.acquired ? 1u : 0u;
	o->front_slot = gScan.acquired ? gScan.front : N48N_SCAN_NO_SLOT;
	o->pending_slot = N48N_SCAN_NO_SLOT;
	if (gScan.acquired && gScan.lastPresentSeq > re) o->pending_slot = gScan.front;   // the copy has not retired yet
	o->frame_count = frame_now();
	if (gCon.ok) o->console_mc = o->plane_mc = o->earliest_mc = gDev->vramMC(gCon.off);
	o->presents = gScan.presents;
	o->latched = gScan.presents - (gScan.lastPresentSeq > re ? 1u : 0u);   // a present "latches" when its copy retired
	o->refused = gScan.refused;
	for (uint32_t i = 0; i < N48N_SCAN_MAX_SLOTS; i++) {
		const ScanSlot &sl = gScan.slot[i];
		if (!sl.used) continue;
		o->slot[i].mc = sl.mc; o->slot[i].used = 1u; o->slot[i].presents = sl.presents;
		o->slot[i].latches = sl.presents - (sl.lastSeq > re ? 1u : 0u);
		o->slot[i].flags = sl.lastSeq > re ? N48N_SCANSLOT_PENDING : N48N_SCANSLOT_REUSABLE;
	}
	IOLockUnlock(gLock);
	return kIOReturnSuccess;
}

IOReturn n33n_scan_release(uint64_t out[2]) {
	if (!hello()) return kIOReturnNotReady;
	IOLockLock(gLock);
	const bool ok = scan_end_locked();
	out[0] = ok ? 1u : 0u; out[1] = gCon.ok ? gDev->vramMC(gCon.off) : 0;
	IOLockUnlock(gLock);
	return kIOReturnSuccess;
}

IOReturn n33n_memory_for_handle(uint32_t handle, IOOptionBits *options, IOMemoryDescriptor **memory) {
	if (!hello()) return kIOReturnNotReady;
	IOLockLock(gLock);
	IOReturn rc = kIOReturnSuccess;
	if (!id_live(gS.boUsed, N48N_MAX_BOS - 1, handle)) rc = kIOReturnNotFound;
	else {
		const Bo &b = gS.bo[handle];
		if (b.kind == kBoHost) { IOLockUnlock(gLock); return kIOReturnUnsupported; }   // the caller already maps its own pages
		IOMemoryDescriptor *md = IOMemoryDescriptor::withPhysicalAddress((IOPhysicalAddress)(gDev->bar0Phys + b.off),
		                                                                 (IOByteCount)((uint64_t)b.nblk * kBlk), kIODirectionInOut);
		if (!md) rc = kIOReturnNoMemory;
		else { *memory = md; if (options) *options |= kIOMapWriteCombineCache; }
	}
	IOLockUnlock(gLock);
	return rc;
}

// ---- BoImportHost (ABI 1.9) on gfx11 -----------------------------------------------------------------------------------------------
// The caller's page-aligned range is wired (prepare) and its physical pages become a SYSTEM|SNOOPED BO, mapped at gpuVa when one is
// given (same contract as n1c_bo_import_host: import_check's limits, 64 MiB per BO, the per-client cap). A page inside this GPU's BAR0
// is refused. Nothing is mapped before every page is known; a failure unwires everything.
namespace {
struct N33Seg {
	IOMemoryDescriptor *md;
	uint64_t phys(uint64_t off, uint64_t *len) {
		IOByteCount seg = 0;
		const addr64_t pa = md->getPhysicalSegment((IOByteCount)off, &seg, kIOMemoryMapperNone);
		*len = pa != 0 ? (uint64_t)seg : 0ull;
		return (uint64_t)pa;
	}
};
}
static IOReturn n33n_bo_import_host_impl(task_t task, uint64_t hostVa, uint64_t size, uint64_t flags, uint64_t gpuVa, uint64_t out[4], const char **why);
// 0.0.676: every refused host import is logged with its reason (WindowServer's 8 MB NoCopy buffers were refused with no kernel line).
IOReturn n33n_bo_import_host(task_t task, uint64_t hostVa, uint64_t size, uint64_t flags, uint64_t gpuVa, uint64_t out[4]) {
	const char *why = "?";
	const IOReturn rc = n33n_bo_import_host_impl(task, hostVa, size, flags, gpuVa, out, &why);
	if (rc != kIOReturnSuccess) {
		static uint32_t n = 0;
		if (n++ < 64) N33N_LOG("host import REFUSED rc %#x (%s): va %#llx size %llu KiB flags %#llx gpuVa %#llx imported %llu KiB", (unsigned)rc, why,
		                       (unsigned long long)hostVa, (unsigned long long)(size >> 10), (unsigned long long)flags, (unsigned long long)gpuVa,
		                       (unsigned long long)(__atomic_load_n(&gS.importedBytes, __ATOMIC_RELAXED) >> 10));
	}
	return rc;
}
static IOReturn n33n_bo_import_host_impl(task_t task, uint64_t hostVa, uint64_t size, uint64_t flags, uint64_t gpuVa, uint64_t out[4], const char **why) {
	*why = "not open";
	if (!hello()) return kIOReturnNotReady;
	*why = "args"; if (task == nullptr || out == nullptr) return kIOReturnBadArgument;
	const ImportChk pre = import_check(hostVa, size, flags, gpuVa, __atomic_load_n(&gS.importedBytes, __ATOMIC_RELAXED));
	if (pre.rc != kOk) { *why = "import_check (align / size / flags / va / cap)"; return (IOReturn)pre.rc; }
	IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange((mach_vm_address_t)hostVa, (mach_vm_size_t)size, kIODirectionInOut, task);
	if (md == nullptr) { *why = "withAddressRange"; return kIOReturnNoMemory; }
	if (const IOReturn pr = md->prepare(); pr != kIOReturnSuccess) { md->release(); *why = pr == kIOReturnVMError ? "prepare: VM error (unmapped or not wireable)" : "prepare (wire) failed"; N33N_LOG("host import: prepare rc %#x", (unsigned)pr); return kIOReturnBadArgument; }   // not (fully) mapped in the caller
	const vm_size_t listBytes = (vm_size_t)(pre.pages * sizeof(uint64_t));
	uint64_t *pages = static_cast<uint64_t *>(IOMalloc(listBytes));
	if (pages == nullptr) { md->complete(); md->release(); *why = "page list alloc"; return kIOReturnNoMemory; }
	N33Seg seg { md };
	uint32_t crc = collect_pages(seg, size, pages, kImportMaxPages);
	if (crc == kOk)
		for (uint64_t i = 0; i < pre.pages; i++)
			if (pages[i] >= gDev->bar0Phys && pages[i] < gDev->bar0Phys + gDev->bar0Size) { crc = kBadArg; break; }   // device memory: never
	if (crc != kOk) {
		IOFree(pages, listBytes); md->complete(); md->release();
		N33N_LOG("import refused (%#x): %#llx + %llu KiB", crc, (unsigned long long)hostVa, (unsigned long long)(size >> 10));
		*why = "page collect / BAR0 page"; return (IOReturn)crc;
	}
	IOLockLock(gLock);
	IOReturn rc = kIOReturnSuccess;
	bool taken = false;
	do {
		if (!gS.hello) { rc = kIOReturnNotReady; *why = "closed"; break; }
		const ImportChk c = import_check(hostVa, size, flags, gpuVa, gS.importedBytes);   // authoritative, under the lock
		if (c.rc != kOk) { rc = (IOReturn)c.rc; *why = "import_check under lock"; break; }
		const uint32_t h = lowest_free(gS.boUsed, 1, N48N_MAX_BOS - 1);
		if (h == 0) { rc = kIOReturnNoResources; *why = "no free BO handle"; break; }
		int slot = -1;
		if (c.gpuStripped != 0ull) {
			if (va_overlap(gS.maps, kMaxMaps, c.gpuStripped, size) >= 0) { rc = kIOReturnBadArgument; *why = "GPU VA overlap"; break; }
			slot = va_insert(gS.maps, kMaxMaps, VaEnt{ c.gpuStripped, size, 0, h, (uint32_t)flags });
			if (slot < 0) { rc = kIOReturnNoResources; *why = "no free VA slot"; break; }
			const uint32_t r = pt_map_list(c.gpuStripped, c.pages, pages, leaf((uint32_t)flags) | kSys | kSnoop);
			if (r != kOk) { pt_unmap(c.gpuStripped, c.pages); gS.maps[slot] = VaEnt{0, 0, 0, 0, 0}; rc = (IOReturn)r; *why = "page-table map"; break; }
		}
		Bo b {};
		b.used = 1; b.kind = kBoHost; b.size = size; b.hmd = md; b.hpages = pages; b.hpageCount = (uint32_t)c.pages;
		gS.bo[h] = b; gS.boUsed[h] = 1;
		gS.importedBytes += size;
		taken = true;
		if (slot >= 0) rc = (IOReturn)flush();
		out[0] = h; out[1] = size; out[2] = slot >= 0 ? va_canonicalize(c.gpuStripped) : 0ull; out[3] = N48N_PLACED_HOST_IMPORT;
	} while (false);
	IOLockUnlock(gLock);
	if (!taken) { IOFree(pages, listBytes); md->complete(); md->release(); }
	return rc;
}

} // namespace amdgpu
