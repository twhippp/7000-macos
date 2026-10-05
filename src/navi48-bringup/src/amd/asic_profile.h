//
//  asic_profile.h — per-ASIC identity and expectations.
//
//  The kext originally supported exactly one ASIC (Navi48 / gfx1201) with every
//  identity value hardcoded: the probe() device check, the kIP_* version
//  constants in amdgpu_ip.h, and the firmware names in fw_table.cpp. Navi33
//  (gfx1102) has a different GC, PSP, SMU, SDMA, MES and MMHUB generation, so
//  those values cannot stay hardcoded.
//
//  A profile carries the identity match, the IP versions we expect to harvest
//  from on-die discovery, and the firmware blob names. The expected versions are
//  a cross-check only: the authoritative version always comes from discovery at
//  runtime, because that is what upstream Linux does (amdgpu_discovery.c
//  switches on harvested IP_VERSION(), not on CHIP_* names).
//
//  Reference: drivers/gpu/drm/amd/amdgpu/amdgpu_devlist.h and
//  amdgpu_discovery.c, Linux master.
//
#pragma once

#include <stdint.h>

#include "amdgpu_ip.h"

namespace amdgpu {

enum class GpuGeneration : uint8_t {
    Unknown = 0,
    Navi33,   // gfx1102, RDNA3, GC 11.0.2 / DCN 3.2.1 / MMHUB 3.0.x
    Navi48,   // gfx1201, RDNA4, GC 12.0.1 / DCN 4.1.0 / MMHUB 4.1.0
};

struct PciIdentity {
    uint16_t vendor;
    uint16_t device;
    uint16_t subsystem;
    uint16_t subsystemVendor;
    uint8_t  revision;
};

struct AsicProfile {
    GpuGeneration gen;
    const char *name;

    // Boot arg that arms detection for this generation. Independent so a
    // Navi48 regression can never be reached by the Navi33 arg.
    const char *bootArg;

    // Expected on-die IP versions. Used to log a loud mismatch, never to
    // override what discovery actually reports.
    IPVersion expectGfx;
    IPVersion expectPsp;
    IPVersion expectSmu;
    IPVersion expectSdma;
    IPVersion expectMmhub;
    IPVersion expectNbio;

    // Firmware blob names as they appear in linux-firmware.
    const char *fwGfxPrefix;   // e.g. "gc_11_0_2"
    const char *fwPspPrefix;   // e.g. "psp_13_0_7"
    const char *fwSmuName;     // e.g. "smu_14_0_3.bin"
    const char *fwSdmaName;    // e.g. "sdma_6_0_2.bin"

    // False until the module family for this generation exists. When false,
    // start() stops right after IP discovery and attaches read-only, so the
    // survey can be read back with `navi48test log`, but no firmware is loaded
    // and no functional register is written.
    bool modulesAvailable;
};

// Returns the profile matching a PCI identity, or nullptr when the device is not
// one we support at all. A known-but-unsupported Navi33 board is reported
// through navi33KnownDeviceId() so we can log "seen but not supported" instead
// of silently ignoring it.
const AsicProfile *profileForPci(const PciIdentity &id);

// True for any Navi33 device id, supported or not.
bool navi33KnownDeviceId(uint16_t device);

// Process-wide selection. Set once in probe(), read everywhere else.
void               setActiveProfile(const AsicProfile *p);
const AsicProfile *activeProfile();

const char *generation_name(GpuGeneration g);

// "11.0.2". The returned pointer is valid only until the 8th following call
// (it is backed by a rotating pool), so it is safe to pass several versions to
// one log line but not to hold on to one.
const char *version_string(const IPVersion &v);

} // namespace amdgpu