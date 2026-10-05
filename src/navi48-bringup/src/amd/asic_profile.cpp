#include <stdio.h>

#include "asic_profile.h"

namespace amdgpu {

namespace {

// Navi33 (gfx1102) device ids, Linux amdgpu_devlist.h "GC 11.0.2, DCN 3.2.1".
// 0x7480 is the RX 7600 / 7600 XT bucket and is the only id armed for bring-up;
// the rest are recognised so the log can say "Navi33, not armed" instead of
// silently ignoring the card.
constexpr uint16_t kNavi33Rx7600    = 0x7480;  // RX 7600 / 7600 XT / 7600S / 7600M XT / 7700S / PRO W7600 / 7650 GRE
constexpr uint16_t kNavi33SteamDeck = 0x7481;
constexpr uint16_t kNavi33Rx7600m   = 0x7483;
constexpr uint16_t kNavi33ProW7500  = 0x7489;
constexpr uint16_t kNavi33ProW7500m = 0x748B;
constexpr uint16_t kNavi33Rx7300    = 0x7499;  // RX 7300 / 7400 / PRO W7400

constexpr uint16_t kNavi48Rx9070    = 0x7550;
constexpr uint16_t kNavi48R9700     = 0x7551;

constexpr uint16_t kVendorAmd = 0x1002;

constexpr AsicProfile kProfileNavi33 = {
    GpuGeneration::Navi33,
    "Navi 33 (gfx1102, RDNA3)",
    "navi33bringup",
    /* expectGfx    */ { 11, 0, 2 },
    /* expectPsp    */ { 13, 0, 7 },
    /* expectSmu    */ { 13, 0, 7 },
    /* expectSdma   */ {  6, 0, 2 },
    /* expectMmhub  */ {  3, 0, 0 },
    /* expectNbio   */ {  7, 4, 0 },
    /* fwGfxPrefix */ "gc_11_0_2",
    /* fwPspPrefix */ "psp_13_0_7",
    /* fwSmuName   */ "smu_13_0_7.bin",
    /* fwSdmaName  */ "sdma_6_0_2.bin",
    /* modules     */ false,
};

constexpr AsicProfile kProfileNavi48 = {
    GpuGeneration::Navi48,
    "Navi 48 (gfx1201, RDNA4)",
    "navi48bringup",
    /* expectGfx    */ { 12, 0, 1 },
    /* expectPsp    */ { 14, 0, 3 },
    /* expectSmu    */ { 14, 0, 3 },
    /* expectSdma   */ {  7, 0, 1 },
    /* expectMmhub  */ {  4, 1, 0 },
    /* expectNbio   */ {  7, 11, 0 },
    /* fwGfxPrefix */ "gc_12_0_1",
    /* fwPspPrefix */ "psp_14_0_3",
    /* fwSmuName   */ "smu_14_0_3.bin",
    /* fwSdmaName  */ "sdma_7_0_1.bin",
    /* modules     */ true,
};

const AsicProfile *g_active = nullptr;

} // namespace

bool navi33KnownDeviceId(uint16_t device) {
    switch (device) {
    case kNavi33Rx7600:
    case kNavi33SteamDeck:
    case kNavi33Rx7600m:
    case kNavi33ProW7500:
    case kNavi33ProW7500m:
    case kNavi33Rx7300:
        return true;
    default:
        return false;
    }
}

const AsicProfile *profileForPci(const PciIdentity &id) {
    if (id.vendor != kVendorAmd) return nullptr;
    switch (id.device) {
    case kNavi48Rx9070:
    case kNavi48R9700:
        return &kProfileNavi48;
    case kNavi33Rx7600:
        return &kProfileNavi33;
    default:
        return nullptr;
    }
}

void setActiveProfile(const AsicProfile *p) { g_active = p; }
const AsicProfile *activeProfile() { return g_active; }

const char *generation_name(GpuGeneration g) {
    switch (g) {
    case GpuGeneration::Navi33: return "navi33";
    case GpuGeneration::Navi48: return "navi48";
    default:                    return "unknown";
    }
}

const char *version_string(const IPVersion &v) {
	// Rotating pool, not one static buffer: a single log line passes six
	// versions at once, and a single shared buffer would make all six arguments
	// point at the same string and print the last one six times.
	static char pool[8][16];
	static uint32_t next = 0;
	char *buf = pool[next];
	next = (next + 1) % (uint32_t)(sizeof(pool) / sizeof(pool[0]));
	// snprintf, not '0' + major: PSP and SMU are 13.x, and 48 + 13 is '='.
	snprintf(buf, sizeof(pool[0]), "%u.%u.%u", v.major, v.minor, v.rev);
	return buf;
}

} // namespace amdgpu