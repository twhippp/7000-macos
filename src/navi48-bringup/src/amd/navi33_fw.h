//
//  navi33_fw.h — RX 7600 (Navi 33: GC 11.0.2, SDMA 6.0.3, SMU/MP0 13.0.7) firmware
//  loading through the PSP, after SETUP_TMR. Port of Linux psp_load_non_psp_fw for a
//  PMFW-centralised, autoload-capable part:
//
//    SMU            loaded BEFORE SETUP_TMR (psp_hw_start), so not in the list below
//    SDMA TH0, TH1  sdma_6_0_3.bin (sdma_firmware_header_v2_0)
//    RS64 PFP, ME, MEC ucode, then PFP P0/P1, ME P0/P1, MEC P0..P3 stacks
//                   (AMDGPU_UCODE_ID enum order; gfx_v11_0 registers 2/2/4 stacks)
//    CP_MES + MES_STACK        gc_11_0_2_mes_2.bin (scheduler pipe)
//    CP_MES_KIQ + MES_KIQ_STACK gc_11_0_2_mes1.bin (KIQ pipe)
//    IMU_I, IMU_D
//    RLC restore lists, IRAM, DRAM, RLC_P, RLC_V, RLC_G   (rlc v2.3)
//    AUTOLOAD_RLC   right after RLC_G
//
//  Then gfx_v11_0_hw_init waits for CP_STAT == 0 and RLC_RLCS_BOOTLOAD_STATUS.
//  BOOTLOAD_COMPLETE — read-only, done here too.
//
#pragma once
#include "amdgpu_regs.h"
#include "amdgpu_psp.h"

namespace amdgpu {

struct N33Blob { const uint8_t *data; size_t size; };
struct N33FwSet { N33Blob smu, sdma, pfp, me, mec, mes, mes1, imu, rlc; };
struct N33Payload { uint32_t fw_type; const uint8_t *src; uint32_t size; };

constexpr uint32_t kN33MaxPayloads = 48;

// Bit per blob in N33FwSet order, set when the blob is missing or its header is not understood.
enum : uint32_t {
	kN33Smu = 1u << 0, kN33Sdma = 1u << 1, kN33Pfp = 1u << 2, kN33Me = 1u << 3, kN33Mec = 1u << 4,
	kN33Mes = 1u << 5, kN33Mes1 = 1u << 6, kN33Imu = 1u << 7, kN33Rlc = 1u << 8,
};

// Pure: the SMU payload (common header slice), false if the blob is unusable.
bool navi33_smu_payload(const N33Blob &smu, N33Payload &out);

// Pure: the ordered LOAD_IP_FW list after SETUP_TMR (SMU excluded). Returns the count;
// *badMask gets the blobs that were missing or not understood (0 = complete set).
uint32_t navi33_gfx_payloads(const N33FwSet &fw, N33Payload out[kN33MaxPayloads], uint32_t *badMask);

struct N33LoadReport {
	uint32_t total;          // payloads in the list
	uint32_t sent;           // LOAD_IP_FW commands the PSP answered (any status)
	uint32_t accepted;       // ... with status 0
	uint32_t firstBadType;   // fw_type of the first non-zero status (0 = none)
	uint32_t firstBadResp;   // its status
	uint32_t lastType;       // last fw_type submitted
	kern_return_t kr;        // stop reason (success, timeout, staging failure, ...)
	bool     autoloadSent;
	uint32_t autoloadResp;
	uint64_t stagedBytes;
};

// Stage and submit every payload; non-zero PSP status is recorded and the load carries
// on (Linux psp_cmd_submit_buf only warns on bare metal); a fence timeout or a staging
// failure stops. Sends AUTOLOAD_RLC right after RLC_G.
kern_return_t navi33_load_gfx_fw(DeviceContext &dev, PSPContext &psp, const N33FwSet &fw,
                                 N33LoadReport &rep);

// Read-only: gfx_v11_0_wait_for_rlc_autoload_complete. Polls CP_STAT (GC 0x0f40, idx 0)
// and RLC_RLCS_BOOTLOAD_STATUS (GC 0x4e82, idx 1) up to timeoutMs. True when complete.
bool navi33_wait_autoload(DeviceContext &dev, uint32_t timeoutMs, uint32_t *cpStat, uint32_t *bootload);

} // namespace amdgpu
