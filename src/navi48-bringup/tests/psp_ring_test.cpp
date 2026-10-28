//
//  psp_ring_test.cpp — host-side check of navi33 stage 2 (navi33-psp=2): the REAL
//  psp_v14_0.cpp ring + TMR code driven against a simulated PSP sOS.
//
//  A background thread plays the PSP: it answers ring creation in C2PMSG_64 and, for
//  every frame published through C2PMSG_67, reads the command out of "VRAM", records
//  it, writes resp.status = 0 and the fence value — the same handshake psp_ring_cmd_submit
//  waits for. So the asserted SETUP_TMR bytes are the ones the kext would really send.
//
//  What it pins down:
//   * MP0 13.0.7 (RX 7600): SETUP_TMR IS sent (upstream boot_time_tmr = false), with
//     first LOAD_TOC with the SOS package's TOC, then buf_phy_addr = the TMR's MC address,
//     buf_size = the size LOAD_TOC returned (4 MiB without a TOC), flags = virt_phy_addr (0x2),
//     and system_phy_addr = VRAM physical address = (mc - vram_start) + FB_OFFSET<<24,
//     as upstream psp_prep_tmr_cmd_buf / amdgpu_gmc_vram_pa.
//   * The TMR is allocated top-down below the top 32 MiB of VRAM, 1 MiB aligned (PSP_TMR_ALIGNMENT).
//   * MP0 14.0.3 (Navi 48): no SETUP_TMR is sent — unchanged behaviour.
//   * FB_LOCATION is read through the MMHUB 3.0.2 table (0x086c).
//
#include "amdgpu_psp.h"
#include "amdgpu_regs.h"
#include "amdgpu_mmhub.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <cstdarg>
#include <vector>

using namespace amdgpu;

// The kext's log sink (n48log.cpp); here, plain stdout.
namespace amdgpu { void n48_logf(const char *fmt, ...) { va_list a; va_start(a, fmt); std::vprintf(fmt, a); va_end(a); } }

namespace {

int g_fail = 0;
void check(bool ok, const char *what) {
	std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok) ++g_fail;
}

constexpr uint32_t kMp0Base   = 0x16000;   // dword bases, arbitrary but distinct
constexpr uint32_t kMmhubBase = 0x1A000;
constexpr uint32_t kNbioBase2 = 0x0D20;
constexpr uint64_t kMcBase    = 0x8000000000ull;

uint32_t u32at(const std::vector<uint8_t> &b, size_t at) { uint32_t v; std::memcpy(&v, &b[at], 4); return v; }

struct Rig {
	std::vector<uint32_t> regs = std::vector<uint32_t>(0x100000 / 4, 0);
	std::vector<uint8_t>  vram = std::vector<uint8_t>(64u << 20, 0);
	DeviceContext dev;
	std::atomic<bool> stop { false };
	std::vector<std::vector<uint8_t>> cmds;   // every command the "PSP" received
	std::thread psp;

	volatile uint32_t &reg(uint32_t base, uint32_t off) { return reinterpret_cast<volatile uint32_t &>(regs[base + off]); }

	Rig(IPVersion mp0, uint32_t fbOffset) {
		dev.rmmio = regs.data(); dev.rmmioSize = regs.size() * 4;
		dev.bar0 = vram.data(); dev.bar0Size = vram.size();
		dev.vramBase = 8u << 20; dev.vramLimit = vram.size();
		dev.vramMcBase = kMcBase; dev.vramSizeBytes = 8ull << 30;
		dev.ip.setBase(IPBlock::MP0, 0, kMp0Base);
		dev.ip.setBase(IPBlock::MMHUB, 0, kMmhubBase);
		dev.ip.setBase(IPBlock::NBIO, 2, kNbioBase2);
		dev.ip.version[(int)IPBlock::MP0] = mp0;
		setMmhubRegs(mmhubRegsForVersion(IPVersion{3, 0, 2}));
		reg(kMmhubBase, 0x086c) = 0x00008000;          // FB_LOCATION_BASE (3.0.2 offset)
		reg(kMmhubBase, 0x0857) = fbOffset;            // FB_OFFSET
		reg(kMp0Base, 0x91) = 0x027c7f84;              // SOS alive (stage 1 result)
		reg(kMp0Base, 0x80) = 0x80000000;              // C2PMSG_64: sOS ready for ring creation
		psp = std::thread([this] { run(); });
	}
	~Rig() { stop = true; psp.join(); }

	uint64_t vramOff(uint64_t mc) const { return mc - kMcBase; }

	void run() {
		uint32_t lastWptr = 0;
		while (!stop) {
			volatile uint32_t &c64 = reg(kMp0Base, 0x80);
			if (c64 != 0x80000000u && (c64 & 0x80000000u) == 0 && c64 != 0) c64 = 0x80000000u;  // ring create ack
			const uint32_t wptr = reg(kMp0Base, 0x83);
			if (wptr != lastWptr) {
				const uint64_t ringMc = ((uint64_t)reg(kMp0Base, 0x86) << 32) | reg(kMp0Base, 0x85);
				const uint64_t frame = vramOff(ringMc) + (uint64_t)(lastWptr / 16) * 64;
				uint32_t f[16]; std::memcpy(f, &vram[frame], 64);
				// psp_gfx_rb_frame: cmd_buf_addr_lo/hi, cmd_buf_size, fence_addr_lo/hi, fence_value
				const uint64_t cmdMc   = ((uint64_t)f[1] << 32) | f[0];
				const uint64_t fenceMc = ((uint64_t)f[4] << 32) | f[3];
				const uint32_t fenceVal = f[5];
				if (vramOff(cmdMc) + kPSPGfxCmdRespSize > vram.size() || vramOff(fenceMc) + 4 > vram.size()) {
					std::printf("  FAIL  frame points outside VRAM (cmd mc %#llx, fence mc %#llx)\n",
					            (unsigned long long)cmdMc, (unsigned long long)fenceMc);
					++g_fail; lastWptr = wptr; continue;
				}
				std::vector<uint8_t> c(&vram[vramOff(cmdMc)], &vram[vramOff(cmdMc)] + kPSPGfxCmdRespSize);
				cmds.push_back(c);
				std::memset(&vram[vramOff(cmdMc) + kPSPGfxRespStatusOffset], 0, 4);   // resp.status = 0
				if (u32at(c, 8) == 0x20) {   // LOAD_TOC: answer with the TMR size an RDNA3 PSP asks for
					const uint32_t ts = 0x1300000;
					std::memcpy(&vram[vramOff(cmdMc) + kPSPGfxRespTmrSizeOffset], &ts, 4);
				}
				std::memcpy(&vram[vramOff(fenceMc)], &fenceVal, 4);
				lastWptr = wptr;
			}
			std::this_thread::sleep_for(std::chrono::microseconds(200));
		}
	}
};

uint32_t u32(const std::vector<uint8_t> &b, size_t at) { uint32_t v; std::memcpy(&v, &b[at], 4); return v; }

} // namespace

int main() {
	std::printf("psp_ring\n");

	static const uint8_t kToc[2304] = { 0x54, 0x4f, 0x43 };   // stand-in for the sos.bin TOC sub-binary
	const uint64_t kVram = 8ull << 30;

	// ---- RX 7600: MP0 13.0.7 with the SOS package's TOC (psp_13_0_7_sos.bin has one) ----
	{
		Rig rig(IPVersion{13, 0, 7}, 0);
		PSPContext psp {};
		check(psp_init(rig.dev, psp) == kIOReturnSuccess, "13.0.7: psp_init");
		check(psp_adopt_sos_alive(rig.dev, psp) == kIOReturnSuccess, "13.0.7: adopts the SOS stage 1 booted");
		check(psp_ring_create(rig.dev, psp) == kIOReturnSuccess, "13.0.7: GPCOM ring created");
		check(psp.ringBusAddr == kMcBase + (8u << 20) + 0x100000, "13.0.7: ring at bring-up base + 1 MiB (MC)");
		psp.toc.start_addr = kToc; psp.toc.size_bytes = sizeof(kToc);
		check(psp_setup_tmr(rig.dev, psp) == kIOReturnSuccess, "13.0.7: LOAD_TOC + SETUP_TMR accepted");
		check(rig.cmds.size() == 2, "13.0.7: two ring commands (LOAD_TOC, SETUP_TMR)");
		if (rig.cmds.size() == 2) {
			const size_t u = kPSPGfxCmdUnionOffset;
			const auto &t = rig.cmds[0];
			check(u32(t, 8) == 0x20, "13.0.7: first command is LOAD_TOC (0x20)");
			check(u32(t, u + 8) == sizeof(kToc), "13.0.7: LOAD_TOC size = TOC sub-binary size");
			check(psp.tocTmrSize == 0x1300000, "13.0.7: PSP's requested TMR size recorded");

			const auto &c = rig.cmds[1];
			const uint64_t tmrOff = (kVram - (32ull << 20) - 0x1300000) & ~((16ull << 20) - 1);
			const uint64_t tmrMc = kMcBase + tmrOff;
			check(u32(c, 8) == 5, "13.0.7: cmd_id = GFX_CMD_ID_SETUP_TMR (5)");
			check(u32(c, u + 0) == (uint32_t)tmrMc && u32(c, u + 4) == (uint32_t)(tmrMc >> 32),
			      "13.0.7: buf_phy_addr = TMR MC address near the top of VRAM");
			check(u32(c, u + 8) == 0x1300000, "13.0.7: buf_size = the size LOAD_TOC returned");
			check(u32(c, u + 12) == 0x2, "13.0.7: flags = virt_phy_addr");
			const uint64_t pa = ((uint64_t)u32(c, u + 20) << 32) | u32(c, u + 16);
			check(pa == tmrOff, "13.0.7: system_phy_addr = VRAM physical (not MC)");
			check((tmrOff & 0xFFFFF) == 0, "13.0.7: TMR 1 MiB aligned (PSP_TMR_ALIGNMENT)");
			check(tmrOff + 0x1300000 <= kVram - (32ull << 20), "13.0.7: top 32 MiB left alone");
			check(psp.tmrRespStatus == 0 && psp.tmrSetUp, "13.0.7: accepted and recorded");
		}
	}

	// ---- no TOC: PSP_TMR_SIZE, still top-down; FB_OFFSET follows amdgpu_gmc_vram_pa ----
	{
		Rig rig(IPVersion{13, 0, 7}, 0x10);
		PSPContext psp {};
		psp_init(rig.dev, psp); psp_adopt_sos_alive(rig.dev, psp); psp_ring_create(rig.dev, psp);
		check(psp_setup_tmr(rig.dev, psp) == kIOReturnSuccess, "no TOC: SETUP_TMR accepted");
		check(rig.cmds.size() == 1, "no TOC: only SETUP_TMR sent");
		if (!rig.cmds.empty()) {
			const size_t u = kPSPGfxCmdUnionOffset;
			const uint64_t tmrOff = (kVram - (32ull << 20) - (4ull << 20)) & ~((16ull << 20) - 1);
			check(u32(rig.cmds[0], u + 8) == 4u << 20, "no TOC: buf_size = 4 MiB");
			const uint64_t pa = ((uint64_t)u32(rig.cmds[0], u + 20) << 32) | u32(rig.cmds[0], u + 16);
			check(pa == tmrOff + (0x10ull << 24), "FB_OFFSET!=0: system_phy_addr adds FB_OFFSET<<24");
		}
	}

	// ---- Navi 48: MP0 14.0.3 keeps skipping SETUP_TMR ----
	{
		Rig rig(IPVersion{14, 0, 3}, 0);
		PSPContext psp {};
		psp_init(rig.dev, psp); psp_adopt_sos_alive(rig.dev, psp);
		check(psp_ring_create(rig.dev, psp) == kIOReturnSuccess, "14.0.3: ring created");
		check(psp_setup_tmr(rig.dev, psp) == kIOReturnSuccess, "14.0.3: setup_tmr returns success");
		check(rig.cmds.empty(), "14.0.3: no SETUP_TMR sent (boot_time_tmr, unchanged)");
	}

	std::printf(g_fail ? "psp_ring: %d FAILED\n" : "psp_ring: OK\n", g_fail);
	return g_fail ? 1 : 0;
}
