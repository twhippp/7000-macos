//
//  navi33_fw.cpp — see navi33_fw.h. Slices follow Linux amdgpu_ucode_init_single_fw
//  (PSP load type) case by case; the order follows psp_load_non_psp_fw's walk of
//  adev->firmware.ucode[] in AMDGPU_UCODE_ID enum order.
//
#include "navi33_fw.h"
#include "amdgpu_ucode_extract.h"
#include "amdgpu_ucode_psp.h"
#include "amdgpu_log.h"

namespace amdgpu {

namespace {

// amdgpu_ucode.h sdma_firmware_header_v2_0 (SOC21 SDMA: ctx = thread 0, ctl = thread 1).
struct sdma_firmware_header_v2_0_n33 {
	common_firmware_header header;
	uint32_t ucode_feature_version;
	uint32_t ctx_ucode_size_bytes;
	uint32_t ctx_jt_offset;
	uint32_t ctx_jt_size;
	uint32_t ctl_ucode_offset;
	uint32_t ctl_ucode_size_bytes;
	uint32_t ctl_jt_offset;
	uint32_t ctl_jt_size;
} __attribute__((packed));
static_assert(sizeof(sdma_firmware_header_v2_0_n33) == 32 + 32, "sdma v2.0 header is 64 bytes");

bool inFile(const N33Blob &b, uint64_t off, uint64_t sz) {
	return b.data && sz != 0 && off < b.size && off + sz <= b.size;
}

bool add(N33Payload out[], uint32_t &n, const N33Blob &b, uint32_t type, uint64_t off, uint64_t sz) {
	if (n >= kN33MaxPayloads || !inFile(b, off, sz)) return false;
	out[n++] = N33Payload{ type, b.data + off, static_cast<uint32_t>(sz) };
	return true;
}

const common_firmware_header *hdr(const N33Blob &b, size_t need) {
	if (!b.data || b.size < need) return nullptr;
	return reinterpret_cast<const common_firmware_header *>(b.data);
}

} // namespace

bool navi33_smu_payload(const N33Blob &smu, N33Payload &out) {
	// AMDGPU_UCODE_ID_SMC takes init_single_fw's default branch: common header slice.
	const common_firmware_header *h = hdr(smu, sizeof(common_firmware_header));
	if (!h || !inFile(smu, h->ucode_array_offset_bytes, h->ucode_size_bytes)) return false;
	out = N33Payload{ PSPGfxFwType::SMU, smu.data + h->ucode_array_offset_bytes, h->ucode_size_bytes };
	return true;
}

uint32_t navi33_gfx_payloads(const N33FwSet &fw, N33Payload out[kN33MaxPayloads], uint32_t *badMask) {
	uint32_t n = 0, bad = 0;
	N33Payload tmp;
	if (!navi33_smu_payload(fw.smu, tmp)) bad |= kN33Smu;

	// SDMA_UCODE_TH0 / TH1 (sdma v2.0).
	{
		const common_firmware_header *c = hdr(fw.sdma, sizeof(sdma_firmware_header_v2_0_n33));
		const auto *h = reinterpret_cast<const sdma_firmware_header_v2_0_n33 *>(fw.sdma.data);
		if (!c || c->header_version_major != 2 ||
		    !add(out, n, fw.sdma, PSPGfxFwType::SDMA_UCODE_TH0, c->ucode_array_offset_bytes, h->ctx_ucode_size_bytes) ||
		    !add(out, n, fw.sdma, PSPGfxFwType::SDMA_UCODE_TH1, h->ctl_ucode_offset, h->ctl_ucode_size_bytes))
			bad |= kN33Sdma;
	}

	// CP RS64 (gfx_firmware_header_v2_0): the three ucodes, then the stacks, enum order.
	struct Cp { const N33Blob *b; uint32_t bit, ucode; uint32_t stacks[4]; uint32_t nStacks; };
	const Cp cps[3] = {
		{ &fw.pfp, kN33Pfp, PSPGfxFwType::RS64_PFP, { PSPGfxFwType::RS64_PFP_P0, PSPGfxFwType::RS64_PFP_P1 }, 2 },
		{ &fw.me,  kN33Me,  PSPGfxFwType::RS64_ME,  { PSPGfxFwType::RS64_ME_P0,  PSPGfxFwType::RS64_ME_P1 },  2 },
		{ &fw.mec, kN33Mec, PSPGfxFwType::RS64_MEC, { PSPGfxFwType::RS64_MEC_P0, PSPGfxFwType::RS64_MEC_P1,
		                                              PSPGfxFwType::RS64_MEC_P2, PSPGfxFwType::RS64_MEC_P3 }, 4 },
	};
	bool cpOk[3];
	for (int i = 0; i < 3; i++) {
		const common_firmware_header *c = hdr(*cps[i].b, sizeof(gfx_firmware_header_v2_0));
		cpOk[i] = c && c->header_version_major == 2;
		if (!cpOk[i]) { bad |= cps[i].bit; continue; }
		const auto *h = reinterpret_cast<const gfx_firmware_header_v2_0 *>(cps[i].b->data);
		if (!add(out, n, *cps[i].b, cps[i].ucode, c->ucode_array_offset_bytes, h->ucode_size_bytes)) {
			bad |= cps[i].bit; cpOk[i] = false;
		}
	}
	for (int i = 0; i < 3; i++) {
		if (!cpOk[i]) continue;
		const auto *h = reinterpret_cast<const gfx_firmware_header_v2_0 *>(cps[i].b->data);
		for (uint32_t s = 0; s < cps[i].nStacks; s++)
			if (!add(out, n, *cps[i].b, cps[i].stacks[s], h->data_offset_bytes, h->data_size_bytes)) bad |= cps[i].bit;
	}

	// MES: scheduler pipe from mes_2 (CP_MES, MES_STACK), KIQ pipe from mes1 (CP_MES_KIQ, MES_KIQ_STACK).
	{
		UcodePayload p[kMaxUcodePayloadsPerFile];
		uint32_t k = fw.mes.data ? amdgpu_ucode_extract(kHostFwFile_MES_UNI, fw.mes.data, fw.mes.size, p) : 0;
		if (k < 2 || !add(out, n, fw.mes, PSPGfxFwType::CP_MES, p[0].offset_bytes, p[0].size_bytes) ||
		    !add(out, n, fw.mes, PSPGfxFwType::MES_STACK, p[1].offset_bytes, p[1].size_bytes))
			bad |= kN33Mes;
		k = fw.mes1.data ? amdgpu_ucode_extract(kHostFwFile_MES_UNI, fw.mes1.data, fw.mes1.size, p) : 0;
		if (k < 2 || !add(out, n, fw.mes1, PSPGfxFwType::CP_MES_KIQ, p[0].offset_bytes, p[0].size_bytes) ||
		    !add(out, n, fw.mes1, PSPGfxFwType::MES_KIQ_STACK, p[1].offset_bytes, p[1].size_bytes))
			bad |= kN33Mes1;
	}

	// IMU_I, IMU_D.
	{
		UcodePayload p[kMaxUcodePayloadsPerFile];
		uint32_t k = fw.imu.data ? amdgpu_ucode_extract(kHostFwFile_IMU, fw.imu.data, fw.imu.size, p) : 0;
		if (k != 2) bad |= kN33Imu;
		for (uint32_t i = 0; i < k; i++) if (!add(out, n, fw.imu, p[i].fw_type, p[i].offset_bytes, p[i].size_bytes)) bad |= kN33Imu;
	}

	// RLC sub-binaries, RLC_G last (the extractor emits enum order).
	{
		UcodePayload p[kMaxUcodePayloadsPerFile];
		uint32_t k = fw.rlc.data ? amdgpu_ucode_extract(kHostFwFile_RLC, fw.rlc.data, fw.rlc.size, p) : 0;
		if (k == 0 || p[k - 1].fw_type != PSPGfxFwType::RLC_G) bad |= kN33Rlc;
		for (uint32_t i = 0; i < k; i++) if (!add(out, n, fw.rlc, p[i].fw_type, p[i].offset_bytes, p[i].size_bytes)) bad |= kN33Rlc;
	}

	if (badMask) *badMask = bad;
	return n;
}

kern_return_t navi33_load_gfx_fw(DeviceContext &dev, PSPContext &psp, const N33FwSet &fw, N33LoadReport &rep) {
	rep = N33LoadReport{};
	N33Payload list[kN33MaxPayloads];
	uint32_t bad = 0;
	rep.total = navi33_gfx_payloads(fw, list, &bad);
	if (bad & ~kN33Smu) {
		UCODE_LOG("navi33-fw: firmware set incomplete (mask %#x) — nothing sent", bad);
		rep.kr = kIOReturnNotFound;
		return rep.kr;
	}
	if (!psp.tmrSetUp) { rep.kr = kIOReturnNotReady; return rep.kr; }

	for (uint32_t i = 0; i < rep.total; i++) {
		const N33Payload &p = list[i];
		uint64_t mc = 0;
		kern_return_t r = psp_fw_buf_stage(dev, psp, p.src, p.size, &mc);
		if (r != kIOReturnSuccess) {
			UCODE_LOG("navi33-fw: staging type %u (%u B) failed kr=%#x", p.fw_type, p.size, r);
			rep.kr = r; return r;
		}
		rep.stagedBytes += p.size;
		rep.lastType = p.fw_type;
		r = psp_load_ip_fw(dev, psp, mc, p.size, p.fw_type);
		if (psp.lastTimedOut) {
			UCODE_LOG("navi33-fw: type %u timed out — stopping", p.fw_type);
			rep.kr = kIOReturnTimeout; return rep.kr;
		}
		rep.sent++;
		if (r == kIOReturnSuccess) rep.accepted++;
		else if (rep.firstBadType == 0) { rep.firstBadType = p.fw_type; rep.firstBadResp = psp.lastRespStatus; }
		if (r != kIOReturnSuccess && r != kIOReturnError) { rep.kr = r; return r; }   // not a PSP status: stop

		if (p.fw_type == PSPGfxFwType::RLC_G) {
			kern_return_t a = psp_rlc_autoload_start(dev, psp);
			rep.autoloadSent = !psp.lastTimedOut;
			rep.autoloadResp = psp.lastRespStatus;
			if (psp.lastTimedOut) { rep.kr = kIOReturnTimeout; return rep.kr; }
			(void)a;
		}
	}
	rep.kr = kIOReturnSuccess;
	UCODE_LOG("navi33-fw: %u/%u accepted (%u sent), first bad type %u status %#x, autoload sent %d status %#x",
	          rep.accepted, rep.total, rep.sent, rep.firstBadType, rep.firstBadResp, rep.autoloadSent, rep.autoloadResp);
	return rep.kr;
}

bool navi33_wait_autoload(DeviceContext &dev, uint32_t timeoutMs, uint32_t *cpStat, uint32_t *bootload) {
	constexpr uint32_t kCpStat = 0x0f40, kBootloadStatus = 0x4e82, kBootloadComplete = 0x80000000u;
	if (!dev.ip.isResolved(IPBlock::GC, 0) || !dev.ip.isResolved(IPBlock::GC, 1)) {
		if (cpStat) *cpStat = 0xFFFFFFFFu;
		if (bootload) *bootload = 0xFFFFFFFFu;
		return false;
	}
	const uint32_t rCp = SOC15_REG_OFFSET_BIDX(dev, IPBlock::GC, 0, kCpStat);
	const uint32_t rBl = SOC15_REG_OFFSET_BIDX(dev, IPBlock::GC, 1, kBootloadStatus);
	uint32_t cp = 0, bl = 0;
	for (uint32_t ms = 0; ; ms++) {
		cp = RREG32(dev, rCp);
		bl = RREG32(dev, rBl);
		if (cp == 0 && (bl & kBootloadComplete)) break;
		if (ms >= timeoutMs) break;
		IOSleep(1);
	}
	if (cpStat) *cpStat = cp;
	if (bootload) *bootload = bl;
	return cp == 0 && (bl & kBootloadComplete);
}

} // namespace amdgpu
