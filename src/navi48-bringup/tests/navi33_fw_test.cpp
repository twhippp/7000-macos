//
//  navi33_fw_test.cpp — host check of navi33 stages 2 and 3 (navi33-psp=3): the REAL
//  psp_v14_0.cpp + navi33_fw.cpp driven against a simulated PSP, with synthetic firmware
//  files built from the real header structs.
//
//  What it pins down (Linux psp_hw_start + psp_load_non_psp_fw for MP0 13.0.7, GC 11.0.2):
//   * order on the ring: LOAD_TOC, LOAD_IP_FW(SMU) BEFORE SETUP_TMR, SETUP_TMR, then
//     SDMA TH0/TH1, RS64 PFP/ME/MEC, their 2/2/4 stacks, MES sched + KIQ, IMU I/D,
//     RLC lists/IRAM/DRAM/P/V/G, AUTOLOAD_RLC right after RLC_G
//   * every payload is the slice Linux amdgpu_ucode_init_single_fw takes
//   * LOAD_IP_FW is refused before SETUP_TMR unless asked for (the SMU case)
//   * a non-zero PSP status is recorded and the load carries on (bare-metal Linux)
//   * an incomplete firmware set sends nothing
//
#include "amdgpu_psp.h"
#include "amdgpu_regs.h"
#include "amdgpu_mmhub.h"
#include "amdgpu_ucode_psp.h"
#include "navi33_fw.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
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

constexpr uint32_t kMp0Base = 0x16000, kMmhubBase = 0x1A000, kNbioBase2 = 0x0D20;
constexpr uint64_t kMcBase = 0x8000000000ull;

uint32_t u32at(const uint8_t *b, size_t at) { uint32_t v; std::memcpy(&v, b + at, 4); return v; }

struct Cmd { uint32_t id, type, size; std::vector<uint8_t> payload; };

struct Rig {
	std::vector<uint32_t> regs = std::vector<uint32_t>(0x100000 / 4, 0);
	std::vector<uint8_t>  vram = std::vector<uint8_t>(64u << 20, 0);
	DeviceContext dev;
	std::atomic<bool> stop { false };
	std::vector<Cmd> cmds;
	uint32_t rejectType = 0;   // LOAD_IP_FW of this fw_type answers TEE_ERROR_BAD_PARAMETERS
	std::thread psp;

	volatile uint32_t &reg(uint32_t base, uint32_t off) { return reinterpret_cast<volatile uint32_t &>(regs[base + off]); }

	Rig() {
		dev.rmmio = regs.data(); dev.rmmioSize = regs.size() * 4;
		dev.bar0 = vram.data(); dev.bar0Size = vram.size();
		dev.vramBase = 8u << 20; dev.vramLimit = vram.size();
		dev.vramMcBase = kMcBase; dev.vramSizeBytes = 8ull << 30;
		dev.ip.setBase(IPBlock::MP0, 0, kMp0Base);
		dev.ip.setBase(IPBlock::MMHUB, 0, kMmhubBase);
		dev.ip.setBase(IPBlock::NBIO, 2, kNbioBase2);
		dev.ip.version[(int)IPBlock::MP0] = IPVersion{13, 0, 7};
		setMmhubRegs(mmhubRegsForVersion(IPVersion{3, 0, 2}));
		reg(kMmhubBase, 0x086c) = 0x00008000;
		reg(kMp0Base, 0x91) = 0x027c7f84;
		reg(kMp0Base, 0x80) = 0x80000000;
		psp = std::thread([this] { run(); });
	}
	~Rig() { stop = true; psp.join(); }

	void run() {
		uint32_t lastWptr = 0;
		while (!stop) {
			volatile uint32_t &c64 = reg(kMp0Base, 0x80);
			if (c64 != 0x80000000u && (c64 & 0x80000000u) == 0 && c64 != 0) c64 = 0x80000000u;
			const uint32_t wptr = reg(kMp0Base, 0x83);
			if (wptr != lastWptr) {
				const uint64_t ringMc = ((uint64_t)reg(kMp0Base, 0x86) << 32) | reg(kMp0Base, 0x85);
				const uint64_t frame = ringMc - kMcBase + (uint64_t)(lastWptr / 16) * 64;
				uint32_t f[16]; std::memcpy(f, &vram[frame], 64);
				const uint64_t cmdOff = (((uint64_t)f[1] << 32) | f[0]) - kMcBase;
				const uint64_t fenceOff = (((uint64_t)f[4] << 32) | f[3]) - kMcBase;
				const uint8_t *c = &vram[cmdOff];
				const size_t u = kPSPGfxCmdUnionOffset;
				Cmd k { u32at(c, 8), 0, 0, {} };
				uint32_t status = 0;
				if (k.id == 6) {   // LOAD_IP_FW: fw_phy_addr lo/hi, fw_size, fw_type
					const uint64_t fwOff = (((uint64_t)u32at(c, u + 4) << 32) | u32at(c, u)) - kMcBase;
					k.size = u32at(c, u + 8); k.type = u32at(c, u + 12);
					if (fwOff + k.size <= vram.size()) k.payload.assign(&vram[fwOff], &vram[fwOff] + k.size);
					if (k.type == rejectType) status = 0xFFFF0006u;
				}
				cmds.push_back(k);
				std::memcpy(&vram[cmdOff + kPSPGfxRespStatusOffset], &status, 4);
				if (k.id == 0x20) { const uint32_t ts = 0x1300000; std::memcpy(&vram[cmdOff + kPSPGfxRespTmrSizeOffset], &ts, 4); }
				std::memcpy(&vram[fenceOff], &f[5], 4);
				lastWptr = wptr;
			}
			std::this_thread::sleep_for(std::chrono::microseconds(100));
		}
	}
};

// ---- synthetic firmware: each byte of a payload region is a distinct marker ----
struct File { std::vector<uint8_t> b; N33Blob blob() const { return N33Blob{ b.data(), b.size() }; } };

void fill(File &f, uint32_t off, uint32_t size, uint8_t mark) { std::memset(&f.b[off], mark, size); }
void common(File &f, uint16_t maj, uint16_t mnr, uint32_t usize) {
	auto *h = reinterpret_cast<common_firmware_header *>(f.b.data());
	h->size_bytes = (uint32_t)f.b.size(); h->header_size_bytes = 256;
	h->header_version_major = maj; h->header_version_minor = mnr;
	h->ucode_size_bytes = usize; h->ucode_array_offset_bytes = 256;
}

File smuFile() { File f; f.b.assign(4096, 0); common(f, 2, 1, 1024); fill(f, 256, 1024, 0x18); return f; }

File sdmaFile() {
	File f; f.b.assign(4096, 0); common(f, 2, 0, 3000);
	uint32_t *w = reinterpret_cast<uint32_t *>(f.b.data() + 32);
	w[1] = 512;            // ctx_ucode_size_bytes
	w[4] = 2048;           // ctl_ucode_offset
	w[5] = 256;            // ctl_ucode_size_bytes
	fill(f, 256, 512, 0x47); fill(f, 2048, 256, 0x48);
	return f;
}

File cpFile(uint8_t mark) {
	File f; f.b.assign(8192, 0); common(f, 2, 0, 0);
	auto *h = reinterpret_cast<gfx_firmware_header_v2_0 *>(f.b.data());
	h->ucode_size_bytes = 1000; h->data_size_bytes = 600; h->data_offset_bytes = 4096;
	fill(f, 256, 1000, mark); fill(f, 4096, 600, mark + 1);
	return f;
}

File mesFile(uint8_t mark) {
	File f; f.b.assign(8192, 0); common(f, 1, 0, 0);
	auto *h = reinterpret_cast<mes_firmware_header_v1_0 *>(f.b.data());
	h->mes_ucode_size_bytes = 900; h->mes_ucode_offset_bytes = 256;
	h->mes_ucode_data_size_bytes = 700; h->mes_ucode_data_offset_bytes = 4096;
	fill(f, 256, 900, mark); fill(f, 4096, 700, mark + 1);
	return f;
}

File imuFile() {
	File f; f.b.assign(4096, 0); common(f, 1, 0, 0);
	auto *h = reinterpret_cast<imu_firmware_header_v1_0 *>(f.b.data());
	h->imu_iram_ucode_size_bytes = 400; h->imu_dram_ucode_size_bytes = 300;
	fill(f, 256, 400, 0x44); fill(f, 656, 300, 0x45);
	return f;
}

File rlcFile() {
	File f; f.b.assign(16384, 0); common(f, 2, 3, 800);
	auto *h = reinterpret_cast<rlc_firmware_header_v2_3 *>(f.b.data());
	auto &v21 = h->v2_2.v2_1;
	v21.save_restore_list_gpm_size_bytes = 100; v21.save_restore_list_gpm_offset_bytes = 2048;
	v21.save_restore_list_srm_size_bytes = 120; v21.save_restore_list_srm_offset_bytes = 3072;
	h->v2_2.rlc_iram_ucode_size_bytes = 200; h->v2_2.rlc_iram_ucode_offset_bytes = 4096;
	h->v2_2.rlc_dram_ucode_size_bytes = 220; h->v2_2.rlc_dram_ucode_offset_bytes = 6144;
	h->rlcp_ucode_size_bytes = 240; h->rlcp_ucode_offset_bytes = 8192;
	h->rlcv_ucode_size_bytes = 260; h->rlcv_ucode_offset_bytes = 10240;
	fill(f, 256, 800, 0x08); fill(f, 2048, 100, 0x14); fill(f, 3072, 120, 0x15);
	fill(f, 4096, 200, 0x1A); fill(f, 6144, 220, 0x30); fill(f, 8192, 240, 0x19); fill(f, 10240, 260, 0x07);
	return f;
}

struct Files {
	File smu = smuFile(), sdma = sdmaFile(), pfp = cpFile(0x57), me = cpFile(0x60), mec = cpFile(0x70),
	     mes = mesFile(0x21), mes1 = mesFile(0x51), imu = imuFile(), rlc = rlcFile();
	N33FwSet set() const {
		return N33FwSet{ smu.blob(), sdma.blob(), pfp.blob(), me.blob(), mec.blob(), mes.blob(), mes1.blob(), imu.blob(), rlc.blob() };
	}
};

// Expected (fw_type, size, marker) in Linux order.
struct Want { uint32_t type, size; uint8_t mark; };
const Want kWant[] = {
	{71, 512, 0x47}, {72, 256, 0x48},
	{87, 1000, 0x57}, {88, 1000, 0x60}, {89, 1000, 0x70},
	{90, 600, 0x58}, {91, 600, 0x58}, {92, 600, 0x61}, {93, 600, 0x61},
	{94, 600, 0x71}, {95, 600, 0x71}, {96, 600, 0x71}, {97, 600, 0x71},
	{33, 900, 0x21}, {34, 700, 0x22}, {81, 900, 0x51}, {82, 700, 0x52},
	{68, 400, 0x44}, {69, 300, 0x45},
	{20, 100, 0x14}, {21, 120, 0x15}, {26, 200, 0x1A}, {48, 220, 0x30}, {25, 240, 0x19}, {7, 260, 0x07},
	{8, 800, 0x08},
};
constexpr uint32_t kWantN = sizeof(kWant) / sizeof(kWant[0]);

bool payloadIs(const std::vector<uint8_t> &p, uint32_t size, uint8_t mark) {
	if (p.size() != size) return false;
	for (uint8_t c : p) if (c != mark) return false;
	return true;
}

// stage 2 exactly as Navi48Bringup::stagePSPRingNavi33 orders it
bool stage2(Rig &rig, PSPContext &psp, const Files &fs, bool tolerate) {
	if (psp_init(rig.dev, psp) || psp_adopt_sos_alive(rig.dev, psp) || psp_ring_create(rig.dev, psp)) return false;
	static const uint8_t toc[2304] = { 1 };
	psp.toc.start_addr = toc; psp.toc.size_bytes = sizeof(toc);
	if (psp_tmr_init(rig.dev, psp)) return false;
	N33Payload smu {};
	if (!navi33_smu_payload(fs.smu.blob(), smu)) return false;
	uint64_t mc = 0;
	if (psp_fw_buf_stage(rig.dev, psp, smu.src, smu.size, &mc)) return false;
	if (psp_load_ip_fw(rig.dev, psp, mc, smu.size, smu.fw_type, true)) return false;
	psp.tolerateRespErrors = tolerate;
	const kern_return_t r = psp_tmr_load(rig.dev, psp);
	return r == kIOReturnSuccess || psp.tmrRejected;
}

} // namespace

int main() {
	std::printf("navi33_fw\n");
	const Files fs;

	// ---- pure list ----
	{
		N33Payload out[kN33MaxPayloads]; uint32_t bad = 99;
		const uint32_t n = navi33_gfx_payloads(fs.set(), out, &bad);
		check(bad == 0 && n == kWantN, "complete set: 26 payloads, nothing flagged");
		bool order = n == kWantN;
		for (uint32_t i = 0; order && i < n; i++) {
			std::vector<uint8_t> p(out[i].src, out[i].src + out[i].size);
			order = out[i].fw_type == kWant[i].type && payloadIs(p, kWant[i].size, kWant[i].mark);
			if (!order) std::printf("        #%u got type %u size %u, want type %u size %u\n",
			                        i, out[i].fw_type, out[i].size, kWant[i].type, kWant[i].size);
		}
		check(order, "Linux order and slices (SDMA TH0/TH1, RS64 + 2/2/4 stacks, MES sched+KIQ, IMU, RLC ... RLC_G last)");
		N33Payload smu {};
		check(navi33_smu_payload(fs.smu.blob(), smu) && smu.fw_type == 18 && smu.size == 1024 && smu.src[0] == 0x18,
		      "SMU payload = common header slice, fw_type 18");
		N33FwSet broken = fs.set(); broken.mes1 = N33Blob{ nullptr, 0 };
		navi33_gfx_payloads(broken, out, &bad);
		check(bad == kN33Mes1, "missing mes1 flagged");
	}

	// ---- LOAD_IP_FW guard ----
	{
		Rig rig; PSPContext psp {};
		psp_init(rig.dev, psp); psp_adopt_sos_alive(rig.dev, psp); psp_ring_create(rig.dev, psp);
		check(psp_load_ip_fw(rig.dev, psp, kMcBase + (24u << 20), 64, 18) == kIOReturnNotReady && rig.cmds.empty(),
		      "LOAD_IP_FW refused before SETUP_TMR unless beforeTmr");
	}

	// ---- full stage 2 + 3 ----
	{
		Rig rig; PSPContext psp {};
		check(stage2(rig, psp, fs, false), "stage 2: TOC, SMU, SETUP_TMR all accepted");
		N33LoadReport rep {};
		check(navi33_load_gfx_fw(rig.dev, psp, fs.set(), rep) == kIOReturnSuccess, "stage 3: load returns success");
		check(rep.total == kWantN && rep.sent == kWantN && rep.accepted == kWantN && rep.firstBadType == 0,
		      "stage 3: every payload sent and accepted");
		check(rep.autoloadSent && rep.autoloadResp == 0, "AUTOLOAD_RLC sent and accepted");

		const auto &c = rig.cmds;
		bool seq = c.size() == 3 + kWantN + 1;
		seq = seq && c[0].id == 0x20 && c[1].id == 6 && c[1].type == 18 && c[2].id == 5;
		check(seq, "ring order: LOAD_TOC, LOAD_IP_FW(SMU), SETUP_TMR");
		check(c.size() > 1 && payloadIs(c[1].payload, 1024, 0x18), "SMU bytes as the PSP reads them");
		bool body = seq;
		for (uint32_t i = 0; body && i < kWantN; i++) {
			const Cmd &k = c[3 + i];
			body = k.id == 6 && k.type == kWant[i].type && payloadIs(k.payload, kWant[i].size, kWant[i].mark);
			if (!body) std::printf("        ring #%u: id %#x type %u size %u\n", 3 + i, k.id, k.type, k.size);
		}
		check(body, "26 LOAD_IP_FW in Linux order, staged bytes intact in VRAM");
		check(seq && c.back().id == 0x21 && c[c.size() - 2].type == 8, "AUTOLOAD_RLC immediately after RLC_G, nothing after");
	}

	// ---- a rejected payload: recorded, load continues ----
	{
		Rig rig; rig.rejectType = 72; PSPContext psp {};
		stage2(rig, psp, fs, true);
		N33LoadReport rep {};
		const kern_return_t r = navi33_load_gfx_fw(rig.dev, psp, fs.set(), rep);
		check(r == kIOReturnSuccess && rep.sent == kWantN && rep.accepted == kWantN - 1,
		      "rejected SDMA TH1: the other 25 still sent");
		check(rep.firstBadType == 72 && rep.firstBadResp == 0xFFFF0006u, "first rejection recorded (type, status)");
		check(rep.autoloadSent, "autoload still started");
	}

	// ---- incomplete set: nothing sent ----
	{
		Rig rig; PSPContext psp {};
		stage2(rig, psp, fs, false);
		const size_t before = rig.cmds.size();
		N33FwSet broken = fs.set(); broken.rlc = N33Blob{ nullptr, 0 };
		N33LoadReport rep {};
		check(navi33_load_gfx_fw(rig.dev, psp, broken, rep) == kIOReturnNotFound && rig.cmds.size() == before,
		      "missing RLC: refuses, sends nothing (no autoload with a partial set)");
	}

	std::printf(g_fail ? "navi33_fw: %d FAILED\n" : "navi33_fw: OK\n", g_fail);
	return g_fail ? 1 : 0;
}
