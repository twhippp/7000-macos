//
//  psp_gen_test.cpp — host-side check of n48::PspLoader's two bootloader sequences
//  (navi33 stage 1: PSP 13.0.7). Runs the REAL psp.cpp against a simulated MP0
//  bootloader and VRAM, so the order of PSP_BL__ commands and the image staged for
//  each one are asserted rather than read off the source.
//
//  What it pins down:
//   * V13 (Navi 33) follows psp_v13_0.c through psp_hw_start: KDB, SPL staged with
//     the KDB image (upstream passes &psp->kdb), SYS, SOC, INTF, DBG, RAS, SPDM,
//     then SOS; IPKEYMGR is never sent (no v13 callback).
//   * V14 (Navi 48, the proven path) is unchanged: KDB, SPL, SYS, SOC, INTF, HAD,
//     RAS, IPKEYMGR, SOS, each with its own image.
//   * preflight reads FB_LOCATION from the selected MMHUB table — 0x086c on an
//     RX 7600's MMHUB 3.0.2, not the 4.1.0 offset 0x0554 — and V13 refuses when no
//     table is selected.
//
//  Build (host clang++, no IOKit): see tools/conductor/suites.sh `psp_gen`.
//
#include "psp.hpp"
#include "ipdiscovery.hpp"
#include "amdgpu_mmhub.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

using namespace n48;

namespace {

int g_fail = 0;
void check(bool ok, const char *what) {
	std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok) ++g_fail;
}

// Simulated hardware: a register file keyed by (hwId, dword) and 64 MiB of VRAM.
// Writing a command to C2PMSG_35 records (command, first byte of fw_pri) and the
// bootloader reports ready again; the SOS command also raises C2PMSG_81.
struct FakeHw final : HwAccess {
	std::map<std::pair<uint16_t, uint32_t>, uint32_t> regs;
	std::vector<uint8_t> vram = std::vector<uint8_t>(64u << 20, 0);
	std::vector<std::pair<uint32_t, uint8_t>> sent;   // (cmd, marker byte staged in fw_pri)
	uint64_t fwPri = 0;
	uint32_t mcBase = 0;

	uint32_t regReadIp(uint16_t hw, uint8_t, uint8_t, uint32_t dw) override {
		auto it = regs.find({hw, dw});
		return it == regs.end() ? 0xFFFFFFFFu : it->second;
	}
	bool regWriteIp(uint16_t hw, uint8_t, uint8_t, uint32_t dw, uint32_t v) override {
		regs[{hw, dw}] = v;
		if (hw == IpDiscovery::HwMp0 && dw == 0x0063) {            // C2PMSG_35: command
			sent.push_back({v, vram[fwPri]});
			if (v == 0x00020000) regs[{hw, 0x0091}] = 0x5050AAAA;    // SOS: sign of life
			regs[{hw, 0x0063}] = 0x80000000;                          // bootloader ready again
		}
		return true;
	}
	bool vramWrite(uint64_t off, const void *src, size_t len) override {
		if (off + len > vram.size()) return false;
		std::memcpy(vram.data() + off, src, len); return true;
	}
	bool vramMemset(uint64_t off, uint8_t v, size_t len) override {
		if (off + len > vram.size()) return false;
		std::memset(vram.data() + off, v, len); return true;
	}
};

// Builds a psp sos container (header v2.0) holding one 64-byte image per fw_type,
// each image's first byte = its fw_type, so the staged image is identifiable.
std::vector<uint8_t> makeContainer(const std::vector<uint32_t> &types) {
	const uint32_t hdr = 32, descOff = hdr + 4, n = (uint32_t)types.size();
	const uint32_t ucodeOff = descOff + n * 16, imgSize = 64;
	std::vector<uint8_t> f(ucodeOff + n * imgSize, 0);
	auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&f[at], &v, 4); };
	auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&f[at], &v, 2); };
	put32(0, (uint32_t)f.size()); put32(4, ucodeOff);
	put16(8, 2); put16(10, 0);                         // header v2.0
	put16(12, 13); put16(14, 0);
	put32(16, 0x00370000); put32(20, n * imgSize); put32(24, ucodeOff);
	put32(hdr, n);
	for (uint32_t i = 0; i < n; i++) {
		size_t d = descOff + i * 16;
		put32(d, types[i]); put32(d + 4, 0x100 + types[i]); put32(d + 8, i * imgSize); put32(d + 12, imgSize);
		f[ucodeOff + i * imgSize] = (uint8_t)types[i];
	}
	return f;
}

// fw_type numbers (psp.cpp PspFwType)
enum : uint8_t { SOS = 1, SYS = 2, KDB = 3, SPL = 5, SOC = 7, INTF = 8, DBG = 9, RAS = 10, IPKEY = 11, SPDM = 12 };

void primeMp0(FakeHw &hw) {
	hw.regs[{IpDiscovery::HwMp0, 0x0063}] = 0x80000000;   // bootloader ready
	hw.regs[{IpDiscovery::HwMp0, 0x0091}] = 0;            // SOS down
}

} // namespace

int main() {
	std::printf("psp_gen\n");
	const auto fw = makeContainer({KDB, SPL, SYS, SOC, INTF, DBG, RAS, IPKEY, SPDM, SOS});

	// ---- V13 (Navi 33): MMHUB 3.0.2 table, FB base only at the 3.0.2 offset ----
	{
		FakeHw hw; primeMp0(hw); hw.fwPri = 32u << 20;
		amdgpu::setMmhubRegs(amdgpu::mmhubRegsForVersion(amdgpu::IPVersion{3, 0, 2}));
		hw.regs[{IpDiscovery::HwMmhub, 0x086c}] = 0x00008000;  // FB_LOCATION_BASE (3.0.2)
		hw.regs[{IpDiscovery::HwMmhub, 0x086d}] = 0x000081FF;  // FB_LOCATION_TOP: 8 GiB
		hw.regs[{IpDiscovery::HwMmhub, 0x0857}] = 0x00008000;  // FB_OFFSET
		PspLoader psp(hw); psp.setGeneration(PspGen::V13);
		check(psp.parseContainer(fw.data(), fw.size()), "V13: container parses");
		check(psp.setFwPriOffset(hw.fwPri), "V13: fw_pri offset accepted");
		check(psp.preflight(), "V13: preflight passes reading FB_LOCATION at 0x086c (MMHUB 3.0.2)");
		check(psp.mcBase() == (0x8000ull << 24), "V13: MC base comes from the 3.0.2 register");
		check(psp.runChain(), "V13: chain reaches SOS alive");

		const std::vector<std::pair<uint32_t, uint8_t>> want = {
			{0x00080000, KDB}, {0x10000000, KDB /* SPL cmd, KDB image */}, {0x00010000, SYS},
			{0x000B0000, SOC}, {0x000D0000, INTF}, {0x000C0000, DBG}, {0x000E0000, RAS},
			{0x20000000, SPDM}, {0x00020000, SOS},
		};
		check(hw.sent == want, "V13: exact psp_v13_0 command order and staged images");
		bool ipkey = false;
		for (auto &s : hw.sent) ipkey = ipkey || s.first == 0x000F0000;
		check(!ipkey, "V13: IPKEYMGR never sent");
		if (hw.sent != want)
			for (auto &s : hw.sent) std::printf("        sent cmd 0x%08x image type %u\n", s.first, s.second);
	}

	// ---- V13 with no MMHUB table selected: must refuse, send nothing ----
	{
		FakeHw hw; primeMp0(hw); hw.fwPri = 32u << 20;
		amdgpu::setMmhubRegs(nullptr);
		hw.regs[{IpDiscovery::HwMmhub, 0x0554}] = 0x00008000;  // a 4.1.0-offset value must NOT be used
		hw.regs[{IpDiscovery::HwMmhub, 0x0555}] = 0x000081FF;
		PspLoader psp(hw); psp.setGeneration(PspGen::V13);
		psp.parseContainer(fw.data(), fw.size()); psp.setFwPriOffset(hw.fwPri);
		check(!psp.preflight(), "V13: refuses without an MMHUB table (no guessing)");
		check(hw.sent.empty(), "V13: nothing sent to the PSP after a refusal");
	}

	// ---- V14 (Navi 48): unchanged sequence, 4.1.0 offsets ----
	{
		FakeHw hw; primeMp0(hw); hw.fwPri = 32u << 20;
		amdgpu::setMmhubRegs(amdgpu::mmhubRegsForVersion(amdgpu::IPVersion{4, 1, 0}));
		hw.regs[{IpDiscovery::HwMmhub, 0x0554}] = 0x00008000;
		hw.regs[{IpDiscovery::HwMmhub, 0x0555}] = 0x000083FF;
		hw.regs[{IpDiscovery::HwMmhub, 0x04c7}] = 0x00008000;
		PspLoader psp(hw);   // default generation
		check(psp.generation() == PspGen::V14, "V14 is the default generation");
		psp.parseContainer(fw.data(), fw.size()); psp.setFwPriOffset(hw.fwPri);
		check(psp.preflight(), "V14: preflight passes on 4.1.0 offsets");
		check(psp.runChain(), "V14: chain reaches SOS alive");
		const std::vector<std::pair<uint32_t, uint8_t>> want = {
			{0x00080000, KDB}, {0x10000000, SPL}, {0x00010000, SYS}, {0x000B0000, SOC},
			{0x000D0000, INTF}, {0x000C0000, DBG}, {0x000E0000, RAS}, {0x000F0000, IPKEY},
			{0x00020000, SOS},
		};
		check(hw.sent == want, "V14: proven Navi 48 order unchanged (own SPL image, IPKEYMGR, no SPDM)");
	}

	std::printf(g_fail ? "psp_gen: %d FAILED\n" : "psp_gen: OK\n", g_fail);
	return g_fail ? 1 : 0;
}
