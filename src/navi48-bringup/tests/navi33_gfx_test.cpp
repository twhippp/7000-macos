//
//  navi33_gfx_test.cpp — host check of navi33 stage 4 (navi33-psp=4): the REAL
//  navi33_gfx.cpp against a simulated GFXHUB + SDMA0.
//
//  What it pins down (Linux gfxhub_v3_0_gart_enable + sdma_v6_0_gfx_resume_instance +
//  sdma_v6_0_ring_test_ring, GC 11.0.2):
//   * nothing is written when the GC FB location disagrees with vramMcBase
//   * system aperture == the framebuffer, L1 TLB on with SYSTEM_ACCESS_MODE 3, CONTEXT0 on
//   * the GART PD base is a PHYSICAL address (FB_OFFSET + offset) with VALID set
//   * SDMA0 ring base / rptr / wptr-poll addresses are the MC addresses of our VRAM slot
//   * the ring holds WRITE_LINEAR(0xDEADBEEF) padded to 16 dwords and wptr = 64 bytes
//   * a simulated engine that follows those registers makes the test pass; a halted one
//     makes it time out
//
#include "amdgpu_regs.h"
#include "navi33_gfx.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using namespace amdgpu;

namespace amdgpu { void n48_logf(const char *, ...) {} }

namespace {

int g_fail = 0;
void check(bool ok, const char *what) {
	std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok) ++g_fail;
}

constexpr uint32_t kGc0 = 0x1260, kGc1 = 0xA000;
constexpr uint64_t kMcBase = 0x8000000000ull, kFbOffset = 0x0ull;

struct Rig {
	std::vector<uint32_t> regs = std::vector<uint32_t>(0x100000 / 4, 0);
	std::vector<uint8_t>  vram = std::vector<uint8_t>(64u << 20, 0);
	std::vector<uint8_t>  db   = std::vector<uint8_t>(8192, 0);   // BAR2 doorbells
	DeviceContext dev;
	std::atomic<bool> stop { false };
	bool engineAlive = true;
	bool cpAlive = true;
	bool kiqSim = false, kiqMapped = false;
	uint64_t kiqDone = 0, gfxDone = 0, gfxRing = 0;
	bool ibSeen = false, ibAligned = false;
	uint64_t cpDone = 0;
	std::thread sdma;

	static uint64_t mc(uint32_t lo, uint32_t hi) { return (((uint64_t)hi << 32) | lo) - kMcBase; }
	volatile uint32_t &r(uint32_t base, uint32_t off) { return reinterpret_cast<volatile uint32_t &>(regs[base + off]); }

	explicit Rig(bool alive, uint32_t fbBase = (uint32_t)(kMcBase >> 24)) : engineAlive(alive) {
		dev.rmmio = regs.data(); dev.rmmioSize = regs.size() * 4;
		dev.bar0 = vram.data(); dev.bar0Size = vram.size();
		dev.vramBase = 8u << 20; dev.vramLimit = vram.size();
		dev.vramMcBase = kMcBase; dev.vramSizeBytes = 8ull << 30;
		dev.ip.setBase(IPBlock::GC, 0, kGc0);
		dev.ip.setBase(IPBlock::GC, 1, kGc1);
		r(kGc0, 0x1678) = fbBase;                               // FB_LOCATION_BASE
		r(kGc0, 0x1679) = (uint32_t)(kMcBase >> 24) + 0x1FF;    // TOP: 8 GiB
		r(kGc0, 0x15a7) = (uint32_t)(kFbOffset >> 24);
		r(kGc1, 0x589a) = 0x1;                                  // F32_CNTL: halted
		sdma = std::thread([this] { run(); });
	}
	~Rig() { stop = true; sdma.join(); }

	void run() {
		uint64_t done = 0;
		while (!stop) {
			r(kGc0, 0x16ce) = r(kGc0, 0x16bc) ? 1u : 0u;   // ENG17 ACK follows REQ
			r(kGc1, 0x5843) |= 0x22; r(kGc1, 0x5847) |= 0x22;   // PFP/ME IC: invalidate complete + primed
			r(kGc1, 0x297a) |= 0x2;  r(kGc1, 0x2a09) |= 0x2; r(kGc1, 0x290c) |= 0x2;   // CPC IC, GFX DC, MEC DC
			const bool running = engineAlive && (r(kGc1, 0x589a) & 1u) == 0 && (r(kGc0, 0x0080) & 1u);
			if (running) {
				const uint64_t ring = (((uint64_t)r(kGc0, 0x0082) << 40) | ((uint64_t)r(kGc0, 0x0081) << 8)) - kMcBase;
				const uint64_t poll = (((uint64_t)r(kGc0, 0x00b2) << 32) | r(kGc0, 0x00b3)) - kMcBase;
				uint64_t wptr; std::memcpy(&wptr, &vram[poll], 8);
				auto exec = [&](uint64_t at) -> uint64_t {   // returns bytes consumed
					uint32_t p[6]; std::memcpy(p, &vram[at], 24);
					if ((p[0] & 0xFF) == 2 && ((p[0] >> 8) & 0xFF) == 0) {
						const uint64_t dst = (((uint64_t)p[2] << 32) | p[1]) - kMcBase;
						std::memcpy(&vram[dst], &p[4], 4);
						return 20;
					}
					if ((p[0] & 0xFF) == 4) {   // INDIRECT: base, base_hi, length
						ibSeen = true;
						ibAligned = (p[1] & 0x1F) == 0 && ((at - ring) / 4 + 6) % 8 == 0;
						const uint64_t ib = (((uint64_t)p[2] << 32) | p[1]) - kMcBase;
						uint64_t d = 0;
						while (d < (uint64_t)p[3] * 4) {
							uint32_t q[7]; std::memcpy(q, &vram[ib + d], 28);
							if ((q[0] & 0xFF) == 2) { const uint64_t dst = (((uint64_t)q[2] << 32) | q[1]) - kMcBase; std::memcpy(&vram[dst], &q[4], 4); d += 20; }
							else if ((q[0] & 0xFF) == 1) {   // COPY_LINEAR: count-1, swap, src, dst
								const uint64_t src = (((uint64_t)q[4] << 32) | q[3]) - kMcBase;
								const uint64_t dst = (((uint64_t)q[6] << 32) | q[5]) - kMcBase;
								std::memmove(&vram[dst], &vram[src], (size_t)q[1] + 1);
								d += 28;
							}
							else if ((q[0] & 0xFF) == 11) {   // CONSTANT_FILL: dst, dst_hi, data, count-1
								const uint64_t dst = (((uint64_t)q[2] << 32) | q[1]) - kMcBase;
								for (uint64_t b = 0; b + 4 <= (uint64_t)q[4] + 1; b += 4) std::memcpy(&vram[dst + b], &q[3], 4);
								d += 20;
							}
							else d += 4;
						}
						return 24;
					}
					if ((p[0] & 0xFF) == 5) {   // FENCE: addr, addr_hi, seq
						if (((p[0] >> 16) & 0x7) == 3) {
							const uint64_t dst = (((uint64_t)p[2] << 32) | p[1]) - kMcBase;
							std::memcpy(&vram[dst], &p[3], 4);
						}
						return 16;
					}
					return 4;   // NOP
				};
				while (done < wptr) done += exec(ring + done);
				const uint64_t rp = (((uint64_t)r(kGc0, 0x0088) << 32) | r(kGc0, 0x0089)) - kMcBase;
				std::memcpy(&vram[rp], &done, 8);
				r(kGc0, 0x0083) = (uint32_t)done;
			}
			runCp();
			if (kiqSim) runKiq();
			std::this_thread::sleep_for(std::chrono::microseconds(50));
		}
	}

	// The gfx queue programmed through its HQD registers (doorbell 0x116).
	void runKiq() {
		if (!(r(kGc0, 0x1e80) & 1u)) return;
		const uint64_t gfxRingNow = ((((uint64_t)r(kGc0, 0x1e87)) << 32 | r(kGc0, 0x1e86)) << 8) - kMcBase;
		uint64_t gw; std::memcpy(&gw, &db[0x116 * 4], 8);
		while (gfxDone < gw) {
			uint32_t h; std::memcpy(&h, &vram[gfxRingNow + gfxDone * 4], 4);
			const uint32_t op = (h >> 8) & 0xFF, n = (h >> 16) & 0x3FFF;
			if (op == 0x10 && n == 0x3FFF) { gfxDone += 1; continue; }
			uint32_t a[2]; std::memcpy(a, &vram[gfxRingNow + (gfxDone + 1) * 4], 8);
			if (op == 0x79) regs[0xC000 + a[0]] = a[1];
			gfxDone += n + 2;
		}
		r(kGc0, 0x1e88) = (uint32_t)gfxDone;
	}

	// The CP's compute queue (me 1, pipe 0, queue 0), programmed through the HQD registers.
	void runCp() {
		// compute queue: MEC not halted (CP_MEC_RS64_CNTL bit 30) + CP_HQD_ACTIVE; wptr from the
		// 64-bit doorbell (BAR2 dword 6) when mapped, else the poll slot.
		if (!cpAlive || (r(kGc1, 0x2904) & (1u << 30)) || !(r(kGc0, 0x1fab) & 1u)) return;
		const uint64_t ring = ((((uint64_t)r(kGc0, 0x1fb2)) << 32 | r(kGc0, 0x1fb1)) << 8) - kMcBase;
		uint64_t wptr;
		if (dev.bar2) std::memcpy(&wptr, &db[6 * 4], 8);
		else { const uint64_t p = mc(r(kGc0, 0x1fb6), r(kGc0, 0x1fb7)); std::memcpy(&wptr, &vram[p], 8); }
		while (cpDone < wptr) {
			uint32_t h; std::memcpy(&h, &vram[ring + (cpDone % 1024) * 4], 4);
			const uint32_t op = (h >> 8) & 0xFF, n = (h >> 16) & 0x3FFF;
			if (op == 0x10 && n == 0x3FFF) { cpDone += 1; continue; }
			if (op == 0x79) {
				uint32_t a[2]; std::memcpy(a, &vram[ring + ((cpDone + 1) % 1024) * 4], 8);
				regs[0xC000 + a[0]] = a[1];
			}
			if (op == 0x3F) {   // INDIRECT_BUFFER -> WRITE_DATA(dst_sel 5); compute needs IB_VALID
				uint32_t a[3]; std::memcpy(a, &vram[ring + ((cpDone + 1) % 1024) * 4], 12);
				const uint64_t ib = (((uint64_t)a[1] << 32) | a[0]) - kMcBase;
				uint32_t q[5]; std::memcpy(q, &vram[ib], 20);
				if ((a[2] & (1u << 23)) && ((q[0] >> 8) & 0xFF) == 0x37 && ((q[1] >> 8) & 0xF) == 5 && (a[2] & 0xFFFFF) == 5) {
					const uint64_t dst = (((uint64_t)q[3] << 32) | q[2]) - kMcBase;
					std::memcpy(&vram[dst], &q[4], 4);
				}
			}
			if (op == 0x3C) {   // WAIT_REG_MEM write-wait-write: the hub acks
				uint32_t a[4]; std::memcpy(a, &vram[ring + ((cpDone + 1) % 1024) * 4], 16);
				if (a[0] & 0x40) { regs[a[1]] = a[3]; r(kGc0, 0x16ce) = 1; }
			}
			if (op == 0x37) {   // WRITE_DATA to a register (dst_sel 0)
				uint32_t a[4]; std::memcpy(a, &vram[ring + ((cpDone + 1) % 1024) * 4], 16);
				if ((a[0] & 0xF00) == 0) regs[a[1]] = a[3];
			}
			if (op == 0x76) {   // SET_SH_REG: offset from 0x2c00, then values
				uint32_t a[8]; std::memcpy(a, &vram[ring + ((cpDone + 1) % 1024) * 4], 4 * (n + 1));
				for (uint32_t i = 0; i < n; i++) regs[kGc0 + 0x2c00 - kGc0 + a[0] + i] = a[1 + i];
			}
			if (op == 0x15) {   // DISPATCH_DIRECT: run "the shader" (shaders/navi33_gradient.s) in C
				uint32_t a[4]; std::memcpy(a, &vram[ring + ((cpDone + 1) % 1024) * 4], 16);
				auto shr = [&](uint32_t reg) { return regs[0x2c00 + (kGc0 + reg - 0x2c00)]; };
				const uint64_t pgm = (((uint64_t)shr(0x1bad) << 40) | ((uint64_t)shr(0x1bac) << 8)) - kMcBase;
				uint32_t first; std::memcpy(&first, &vram[pgm], 4);
				const uint64_t dst = mc(shr(0x1be0), shr(0x1be1));
				const uint32_t pitch = shr(0x1be2), seed = shr(0x1be3), tpg = shr(0x1ba7);
				if (first == 0x360200FFu && tpg == 32 && (a[3] & 0x8001) == 0x8001)
					for (uint32_t y = 0; y < a[1]; y++)
						for (uint32_t x = 0; x < a[0] * 32; x++) {
							const uint32_t xs = x + seed;
							const uint32_t c = 0xFF000000u | ((xs & 0xFF) << 16) | ((y & 0xFF) << 8) | ((y + xs) & 0xFF);
							std::memcpy(&vram[dst + ((uint64_t)y * pitch + x) * 4], &c, 4);
						}
			}
			if (op == 0x49) {   // RELEASE_MEM: dw1 event, dw2 sel, addr lo/hi, data lo/hi
				uint32_t a[6]; std::memcpy(a, &vram[ring + ((cpDone + 1) % 1024) * 4], 24);
				if ((a[0] & 0x3F) == 0x14 && (a[1] >> 29) == 1) {
					const uint64_t dst = (((uint64_t)a[3] << 32) | a[2]) - kMcBase;
					std::memcpy(&vram[dst], &a[4], 4);
				}
			}
			cpDone += n + 2;
		}
		r(kGc0, 0x1fb3) = (uint32_t)cpDone;
		const uint64_t rp = mc(r(kGc0, 0x1fb4), r(kGc0, 0x1fb5));
		std::memcpy(&vram[rp], &cpDone, 8);
	}
};

} // namespace

int main() {
	std::printf("navi33_gfx_test\n");
	{
		Rig k(true, 0x1234);
		N33GfxReport rep {};
		const kern_return_t kr = navi33_gfxhub_enable(k.dev, rep);
		check(kr != kIOReturnSuccess && std::strcmp(rep.stage, "fb-mismatch") == 0, "FB mismatch refuses");
		check(k.r(kGc0, 0x167f) == 0 && k.r(kGc0, 0x1688) == 0, "  ...and writes no hub register");
	}
	{
		Rig k(true);
		N33GfxReport rep {};
		check(navi33_gfxhub_enable(k.dev, rep) == kIOReturnSuccess, "gfxhub enable");
		check(rep.tlbFlushAcked, "TLB flush acked");
		check(k.r(kGc0, 0x167d) == (uint32_t)(kMcBase >> 18), "system aperture low = FB start");
		check(k.r(kGc0, 0x167e) == (uint32_t)((kMcBase + (0x200ull << 24) - 1) >> 18), "system aperture high = FB end");
		check((k.r(kGc0, 0x167f) & 0x19) == 0x19 && ((k.r(kGc0, 0x167f) >> 11) & 7) == 3, "L1 TLB on, access mode 3, MTYPE UC");
		check((k.r(kGc0, 0x1688) & 7) == 1, "CONTEXT0 enabled, depth 0");
		const uint64_t pd = ((uint64_t)k.r(kGc0, 0x16f4) << 32) | k.r(kGc0, 0x16f3);
		check(pd == (kFbOffset + k.dev.vramBase + kN33GfxVramOffset) + 1, "PD base = physical address | VALID");
		check(k.r(kGc0, 0x167b) == 0xFFFFFF && k.r(kGc0, 0x167a) == 0, "AGP aperture disabled");

		check(navi33_sdma_ring_test(k.dev, rep) == kIOReturnSuccess && rep.passed, "SDMA0 WRITE_LINEAR lands");
		const uint64_t ring = (((uint64_t)k.r(kGc0, 0x0082) << 40) | ((uint64_t)k.r(kGc0, 0x0081) << 8));
		check(ring == kMcBase + k.dev.vramBase + kN33GfxVramOffset + 0x2000, "ring base is the slot's MC address");
		check(((k.r(kGc0, 0x0080) >> 1) & 0x1F) == 10 && (k.r(kGc0, 0x0080) & (1u << 23)) && (k.r(kGc0, 0x0080) & (1u << 12)),
		      "RB_CNTL: size 10, PRIV, RPTR writeback");
		check(k.r(kGc0, 0x0085) == 64, "wptr = 16 dwords");
		check((k.r(kGc1, 0x589a) & 1) == 0, "F32 unhalted");
		check((k.r(kGc0, 0x0092) & (1u << 28)) == 0, "doorbell off");
		check(rep.rptrWriteback == 64, "rptr write-back seen");

		k.r(kGc1, 0x0803) = (1u << 26) | (1u << 28);   // CP_ME_CNTL: PFP/ME halted
		N33CpFw fw {};
		fw.pfpLo = 0x1000; fw.pfpHi = 1; fw.meLo = 0x2000; fw.meHi = 1; fw.mecLo = 0x3000; fw.mecHi = 1;
		fw.pfpIc = 0x81FC100000ull; fw.pfpDc[0] = 0x81FC200000ull; fw.pfpDc[1] = 0x81FC210000ull;
		fw.meIc  = 0x81FC300000ull; fw.meDc[0]  = 0x81FC400000ull; fw.meDc[1]  = 0x81FC410000ull;
		fw.mecIc = 0x81FC500000ull;
		for (int i = 0; i < 4; i++) fw.mecDc[i] = 0x81FC600000ull + 0x10000ull * i;
		N33CpReport cp {};
		k.dev.bar2 = k.db.data(); k.dev.bar2Size = k.db.size();
		check(navi33_cp_gfx_ring_test(k.dev, fw, cp) == kIOReturnSuccess && cp.passed, "CP compute queue writes SCRATCH_REG0");
		uint64_t dbv; std::memcpy(&dbv, &k.db[6 * 4], 8);
		check(cp.doorbellUsed && dbv == 256, "compute doorbell (index 6) rung with wptr 256");
		const uint32_t dbc = k.r(kGc0, 0x1fb8);
		check(((dbc >> 2) & 0x3FFFFFF) == 6 && (dbc & (1u << 30)), "HQD doorbell control: offset 6, enabled");
		check(k.r(kGc0, 0x1dfc) == 0 && k.r(kGc0, 0x1dfd) == (0x8Au * 2) << 2, "MEC doorbell range");
		check((k.r(kGc0, 0x1fba) & 0x3F) == 9 && (k.r(kGc0, 0x1fba) & 0xC0000000u) == 0xC0000000u, "PQ_CONTROL: size 9, PRIV + KMD queue");
		const uint64_t pq = ((((uint64_t)k.r(kGc0, 0x1fb2)) << 32) | k.r(kGc0, 0x1fb1)) << 8;
		check(pq == kMcBase + k.dev.vramBase + kN33GfxVramOffset + 0x4000, "PQ base is the slot's MC address");
		const uint64_t mqdA = (((uint64_t)k.r(kGc0, 0x1faa)) << 32) | k.r(kGc0, 0x1fa9);
		check(mqdA == kMcBase + k.dev.vramBase + kN33GfxVramOffset + 0xB000, "MQD base register = the MQD in VRAM");
		uint32_t mq[512]; std::memcpy(mq, &k.vram[k.dev.vramBase + kN33GfxVramOffset + 0xB000], sizeof(mq));
		check(mq[0] == 0xC0310800u && mq[130] == 1 && mq[145] == k.r(kGc0, 0x1fba) && mq[143] == dbc, "MQD in memory mirrors the HQD registers");
		check(cp.rbRptr == 256 && cp.hqdActive == 1, "HQD rptr 256, queue active");
		check((k.r(kGc1, 0x0803) & ((1u << 26) | (1u << 28))) == 0, "PFP/ME unhalted");
		check(k.r(kGc0, 0x1fac) == 0, "VMID 0");

		N33IbReport ib {};
		check(navi33_ib_tests(k.dev, ib) == kIOReturnSuccess && ib.sdmaPassed && ib.cpPassed, "IB tests: SDMA and CP both write through an IB");
		check(k.ibSeen && k.ibAligned, "SDMA IB packet 32-byte base, ends on an 8-dword boundary");
		check(k.r(kGc0, 0x0085) == 128 && k.r(kGc0, 0x1fb3) == 512, "wptrs continue: SDMA 128 bytes, CP 512 dwords");

		N33FenceReport fr {};
		check(navi33_fence_copy_tests(k.dev, fr) == kIOReturnSuccess && std::strcmp(fr.stage, "fence-copy-ok") == 0,
		      "stage 7: SDMA 4 KiB copy + FENCE, gfx RELEASE_MEM fence");
		check(fr.copyBadDwords == 0 && fr.sdmaFence == kN33FenceSeq && fr.cpFence == kN33FenceSeq, "  ...copy exact, both fences = seq");
		check(k.r(kGc0, 0x0085) == 192 && k.r(kGc0, 0x1fb3) == 768, "  ...wptrs continue: SDMA 192 bytes, CP 768 dwords");

		// stage 8: a fake 1 MiB "screen" at vram+0 with a pattern
		const uint64_t fbOff = 0, fbLen = 1u << 20;
		for (uint64_t i = 0; i < fbLen / 4; i++) { const uint32_t v = 0x11000000u | (uint32_t)i; std::memcpy(&k.vram[fbOff + i * 4], &v, 4); }
		N33ScreenReport sr {};
		check(navi33_screen_test(k.dev, fbOff, fbLen, sr, 1) == kIOReturnSuccess && std::strcmp(sr.stage, "screen-ok") == 0,
		      "stage 8: backup, white band, restore");
		check(sr.backupOff >= k.dev.vramBase + (32u << 20), "  ...backup lands outside the bring-up span");
		check(std::memcmp(&k.vram[sr.backupOff], &k.vram[fbOff], fbLen) == 0, "  ...screen fully restored == backup");
		uint32_t px; std::memcpy(&px, &k.vram[fbOff + 4 * 5], 4);
		check(px == (0x11000000u | 5), "  ...first pixels are the original ones again");
		N33ScreenReport none {};
		check(navi33_screen_test(k.dev, ~0ull, 0, none, 1) == kIOReturnNotFound, "  ...no console FB -> skipped");
	}
	{
		// stage 9 runs after stages 4-7 (it continues the compute queue for the TLB flush)
		Rig k(true);
		k.kiqSim = true;
		k.dev.bar2 = k.db.data(); k.dev.bar2Size = k.db.size();
		N33GfxReport rep {};
		navi33_gfxhub_enable(k.dev, rep);
		navi33_sdma_ring_test(k.dev, rep);
		N33CpFw fw {};
		N33CpReport cp {};
		navi33_cp_gfx_ring_test(k.dev, fw, cp);
		N33IbReport ib {};
		navi33_ib_tests(k.dev, ib);
		N33FenceReport fr {};
		navi33_fence_copy_tests(k.dev, fr);
		k.r(kGc0, 0x16ce) = 0;
		N33GfxQReport gq {};
		check(navi33_gfx_queue_test(k.dev, gq) == kIOReturnSuccess && std::strcmp(gq.stage, "gfx-queue-ok") == 0,
		      "stage 9: gfx queue from its MQD, gfx ring writes SCRATCH_REG0");
		check(gq.tlbFlushOk && k.r(kGc0, 0x16bc) != 0, "  ...GFXHUB TLB flush through the compute queue acked");
		check(gq.gfxHqdActive == 1 && gq.gfxRptr == 256, "  ...gfx HQD active, rptr 256");
		check(((k.r(kGc0, 0x1e8d) >> 2) & 0x3FFFFFF) == 0x116 && (k.r(kGc0, 0x1e8d) & (1u << 30)), "  ...gfx doorbell 0x116 enabled");
		const uint64_t gb = ((((uint64_t)k.r(kGc0, 0x1e87)) << 32) | k.r(kGc0, 0x1e86)) << 8;
		check(gb == kMcBase + k.dev.vramBase + kN33GfxVramOffset + 0xD000, "  ...gfx HQD base = the gfx ring");
		check(k.r(kGc0, 0x1e7e) == (uint32_t)(kMcBase + k.dev.vramBase + kN33GfxVramOffset + 0xF000), "  ...CP_GFX_MQD_BASE_ADDR = the MQD");
		N33ShaderReport sr {};
		const uint64_t fbOff = 0, fbW = 640, fbH = 64, fbRow = 704 * 4;   // a pitched fake console
		check(navi33_shader_test(k.dev, fbOff, fbRow * fbH, fbRow, fbW, fbH, false, sr) == kIOReturnSuccess &&
		      std::strcmp(sr.stage, "shader-ok") == 0, "stage 10: gradient shader dispatched, pixels match");
		check(!sr.onScreen && sr.targetOff >= k.dev.vramBase + (32u << 20) && sr.pitch == 704 && sr.sampled > 0,
		      "  ...off-screen by default, pitch honoured");
		N33ShaderReport on {};
		check(navi33_shader_test(k.dev, fbOff, fbRow * fbH, fbRow, fbW, fbH, true, on) == kIOReturnSuccess &&
		      on.onScreen && on.targetOff == fbOff, "  ...navi33-draw-screen paints the console itself");
		N33GfxQReport none {};
		DeviceContext d2 = k.dev; d2.bar2 = nullptr;
		check(navi33_gfx_queue_test(d2, none) == kIOReturnNotReady, "  ...no BAR2 -> refuses");
	}
	{
		Rig k(true);
		k.cpAlive = false;
		N33GfxReport rep {};
		navi33_gfxhub_enable(k.dev, rep);
		N33CpFw fw {};
		N33CpReport cp {};
		check(navi33_cp_gfx_ring_test(k.dev, fw, cp, 2000) == kIOReturnTimeout && k.r(kGc1, 0x2040) == 0xCAFEDEADu,
		      "dead CP times out, scratch keeps its seed");
	}
	{
		Rig k(false);
		N33GfxReport rep {};
		navi33_gfxhub_enable(k.dev, rep);
		check(navi33_sdma_ring_test(k.dev, rep, 2000) == kIOReturnTimeout && !rep.passed &&
		      std::strcmp(rep.stage, "sdma-timeout") == 0, "dead engine times out");
		check(rep.testValue == 0xCAFEDEADu, "  ...and the slot keeps its seed");
	}
	std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
	return g_fail ? 1 : 0;
}
