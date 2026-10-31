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
	DeviceContext dev;
	std::atomic<bool> stop { false };
	bool engineAlive = true;
	bool cpAlive = true;
	uint64_t cpDone = 0;
	std::thread sdma;

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
			const bool running = engineAlive && (r(kGc1, 0x589a) & 1u) == 0 && (r(kGc0, 0x0080) & 1u);
			if (running) {
				const uint64_t ring = (((uint64_t)r(kGc0, 0x0082) << 40) | ((uint64_t)r(kGc0, 0x0081) << 8)) - kMcBase;
				const uint64_t poll = (((uint64_t)r(kGc0, 0x00b2) << 32) | r(kGc0, 0x00b3)) - kMcBase;
				uint64_t wptr; std::memcpy(&wptr, &vram[poll], 8);
				while (done < wptr) {
					uint32_t p[5]; std::memcpy(p, &vram[ring + done], 20);
					if ((p[0] & 0xFF) == 2 && ((p[0] >> 8) & 0xFF) == 0) {
						const uint64_t dst = (((uint64_t)p[2] << 32) | p[1]) - kMcBase;
						std::memcpy(&vram[dst], &p[4], 4);
						done += 20;
					} else done += 4;   // NOP
				}
				const uint64_t rp = (((uint64_t)r(kGc0, 0x0088) << 32) | r(kGc0, 0x0089)) - kMcBase;
				std::memcpy(&vram[rp], &done, 8);
				r(kGc0, 0x0083) = (uint32_t)done;
			}
			runCp();
			std::this_thread::sleep_for(std::chrono::microseconds(50));
		}
	}

	// gfx CP ring 0: PFP/ME unhalted (CP_ME_CNTL bits 26/28 clear) + RB_ACTIVE.
	void runCp() {
		const uint32_t me = r(kGc1, 0x0803);
		if (!cpAlive || (me & ((1u << 26) | (1u << 28))) || !r(kGc0, 0x1f40)) return;
		const uint64_t ring = ((((uint64_t)r(kGc0, 0x1e51)) << 32 | r(kGc0, 0x1de0)) << 8) - kMcBase;
		const uint64_t wptr = r(kGc0, 0x1df4);
		while (cpDone < wptr) {
			uint32_t h; std::memcpy(&h, &vram[ring + cpDone * 4], 4);
			const uint32_t op = (h >> 8) & 0xFF, n = (h >> 16) & 0x3FFF;
			if (op == 0x10 && n == 0x3FFF) { cpDone += 1; continue; }
			if (op == 0x79) {
				uint32_t a[2]; std::memcpy(a, &vram[ring + (cpDone + 1) * 4], 8);
				regs[0xC000 + a[0]] = a[1];
			}
			cpDone += n + 2;
		}
		r(kGc0, 0x0f60) = (uint32_t)cpDone;
		const uint64_t rp = (((uint64_t)r(kGc0, 0x1de4) << 32) | r(kGc0, 0x1de3)) - kMcBase;
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
		N33CpFw fw { 0x1000, 0x1, 0x2000, 0x1, 0x3000, 0x1 };
		N33CpReport cp {};
		check(navi33_cp_gfx_ring_test(k.dev, fw, cp) == kIOReturnSuccess && cp.passed, "CP gfx ring writes SCRATCH_REG0");
		check(k.r(kGc0, 0x1e44) == ((1u << 30) | (0x1000 >> 2)) && k.r(kGc0, 0x1e59) == 0, "PFP program counter start");
		check(k.r(kGc1, 0x2900) == ((0x3000 >> 2) | (1u << 30)), "MEC program counter start");
		check((k.r(kGc0, 0x1de1) & 0x3F) == 9 && ((k.r(kGc0, 0x1de1) >> 8) & 0x3F) == 7 && (k.r(kGc0, 0x1de1) & (1u << 29)),
		      "RB0_CNTL: bufsz 9, blksz 7, KMD queue");
		const uint64_t rb0 = ((((uint64_t)k.r(kGc0, 0x1e51)) << 32) | k.r(kGc0, 0x1de0)) << 8;
		check(rb0 == kMcBase + k.dev.vramBase + kN33GfxVramOffset + 0x4000, "RB0 base is the slot's MC address");
		check(k.r(kGc0, 0x1df4) == 256 && cp.rbRptr == 256, "wptr/rptr = 256 dwords");
		check((k.r(kGc1, 0x0803) & ((1u << 26) | (1u << 28))) == 0, "PFP/ME unhalted");
		check((k.r(kGc0, 0x1e8d) & (1u << 30)) == 0 && k.r(kGc0, 0x1df1) == 0, "no doorbell, VMID 0");
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
