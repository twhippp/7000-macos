//
//  asic_profile_test.cpp — host-side check of the per-ASIC profile and the
//  version-selected MMHUB register tables (build 0.0.621, the Navi 33 bring-up).
//
//  Covers src/amd/asic_profile.cpp and src/amd/amdgpu_mmhub.cpp. Neither pulls in
//  IOKit: amdgpu_ip.h is self-contained (stdint.h only), so this needs no MacKernelSDK,
//  no firmware blobs and no kext — just a host clang++.
//
//  What is worth asserting here, beyond "the code runs":
//
//   * The RX 7600 (0x7480) is the only Navi 33 id armed, and its profile reports
//     modulesAvailable == false. That flag is the only thing stopping start() from
//     running the gfx12 firmware ladder on a gfx1102 part, so it is asserted
//     explicitly rather than left to inspection.
//   * MMHUB 4.1.0 must still resolve to the table the proven Navi48 path reads.
//     Linking amdgpu_mmhub.cpp also compiles its 37 static_asserts against the
//     MMHUBRegs constants in amdgpu_ip.h, which is the real Navi48 regression
//     guard: if upstream renames or moves one, this suite fails to build.
//   * Every other MMHUB version must return nullptr. A wrong-but-plausible table
//     is how vram_start turns into a garbage address and the fault lands somewhere
//     unrelated, so "no table" has to be the failure mode, not "closest guess".
//   * 4.1.0 and 3.0.0 must actually differ, which catches a copy-paste of one
//     table into the other.
//   * version_string() must survive a multi-digit major (PSP/SMU are 13.x, and
//     '0' + 13 is '=') and six versions in one log line (a single shared static
//     buffer makes all six arguments print the last value).
//
//  Registered in tools/conductor/suites.sh as `asic_profile`.
//

#include "asic_profile.h"
#include "amdgpu_mmhub.h"

#include <cstdio>
#include <cstring>

using namespace amdgpu;

namespace {

int g_fail = 0;

void check(bool ok, const char *what) {
	if (!ok) {
		std::printf("  FAIL  %s\n", what);
		++g_fail;
	}
}

PciIdentity ident(uint16_t vendor, uint16_t device) {
	PciIdentity id {};
	id.vendor = vendor;
	id.device = device;
	id.revision = 0xcf;
	id.subsystemVendor = 0x1eae;
	id.subsystem = device;
	return id;
}

} // namespace

int main() {
	std::printf("asic_profile\n");

	// ---- PCI identity: only the RX 7600 is armed for bring-up -------------
	const AsicProfile *n33 = profileForPci(ident(0x1002, 0x7480));
	check(n33 != nullptr, "0x7480 (RX 7600) must have a profile");
	check(n33 != nullptr && n33->gen == GpuGeneration::Navi33, "0x7480 must be Navi33");
	check(n33 != nullptr && !n33->modulesAvailable,
	      "Navi33 modulesAvailable must be false: the ladder must stop after discovery");

	const AsicProfile *n48 = profileForPci(ident(0x1002, 0x7550));
	check(n48 != nullptr && n48->gen == GpuGeneration::Navi48, "0x7550 (RX 9070) must stay Navi48");
	check(n48 != nullptr && n48->modulesAvailable, "Navi48 modulesAvailable must stay true");
	check(profileForPci(ident(0x1002, 0x7551)) != nullptr, "0x7551 (R9700) must stay Navi48");
	check(profileForPci(ident(0x1002, 0x73df)) == nullptr, "an unknown AMD id must not get a profile");
	check(profileForPci(ident(0x8086, 0x7480)) == nullptr, "0x7480 from a non-AMD vendor must not match");

	// Recognised but not armed: these should log "seen but not supported".
	check(navi33KnownDeviceId(0x7480), "0x7480 is a known Navi33 id");
	check(navi33KnownDeviceId(0x7499), "0x7499 is a known Navi33 id");
	check(!navi33KnownDeviceId(0x7550), "0x7550 must not report as Navi33");
	check(profileForPci(ident(0x1002, 0x7499)) == nullptr,
	      "a known-but-unarmed Navi33 id must not get a profile");

	// ---- MMHUB table selection follows the discovered version -------------
	// IPVersion is {major, minor, rev}, so 3.0.0 / 3.0.1 / 3.0.2 are {3,0,0},
	// {3,0,1} and {3,0,2} — they share minor == 0 and differ only in rev. A
	// genuine 3.1.0 is {3,1,0}. Selecting on `minor` alone conflates the patch
	// level with a different register map, which is the bug this section exists
	// to pin down: it compiled, and handed an untranscribed map plausible
	// offsets, which is how vram_start turns into a garbage address.
	check(mmhubRegsForVersion(IPVersion{3, 0, 0}) == &kMmhub3_0_0, "MMHUB 3.0.0 -> 3.0.0 table");
	check(mmhubRegsForVersion(IPVersion{3, 0, 1}) == &kMmhub3_0_0, "MMHUB 3.0.1 -> 3.0.0 table");
	check(mmhubRegsForVersion(IPVersion{3, 0, 2}) == &kMmhub3_0_0, "MMHUB 3.0.2 -> 3.0.0 table");
	check(mmhubRegsForVersion(IPVersion{4, 1, 0}) == &kMmhub4_1_0, "MMHUB 4.1.0 -> 4.1.0 table");

	check(mmhubRegsForVersion(IPVersion{3, 1, 0}) == nullptr, "MMHUB 3.1.0 must have no table");
	check(mmhubRegsForVersion(IPVersion{3, 2, 0}) == nullptr, "MMHUB 3.2.0 must have no table");
	check(mmhubRegsForVersion(IPVersion{4, 0, 0}) == nullptr, "MMHUB 4.0.0 must have no table");
	check(mmhubRegsForVersion(IPVersion{4, 2, 0}) == nullptr, "MMHUB 4.2.0 must have no table");
	check(mmhubRegsForVersion(IPVersion{5, 0, 0}) == nullptr, "MMHUB 5.x must have no table");
	check(mmhubRegsForVersion(IPVersion{0, 0, 0}) == nullptr, "absent MMHUB must have no table");

	// The two facts the whole port hinges on.
	check(kMmhub4_1_0.FB_LOCATION_BASE == 0x0554, "4.1.0 FB_LOCATION_BASE is 0x0554");
	check(kMmhub3_0_0.FB_LOCATION_BASE == 0x08ec, "3.0.0 FB_LOCATION_BASE is 0x08ec");
	check(kMmhub4_1_0.FB_LOCATION_BASE != kMmhub3_0_0.FB_LOCATION_BASE,
	      "the two tables must differ (catches one copied into the other)");

	// Unselected by default, so a caller running before discovery fails loudly
	// instead of reading 4.1.0 offsets off a 3.0.0 part.
	check(!mmhubRegsResolved(), "MMHUB table must start unresolved");
	check(mmhubRegs() == nullptr, "MMHUB table must start null");
	setMmhubRegs(mmhubRegsForVersion(IPVersion{3, 0, 0}));
	check(mmhubRegsResolved() && mmhubRegs() == &kMmhub3_0_0, "setMmhubRegs must take effect");
	setMmhubRegs(nullptr);
	check(!mmhubRegsResolved(), "MMHUB table must be clearable");

	// ---- version_string ---------------------------------------------------
	check(std::strcmp(version_string(IPVersion{11, 0, 2}), "11.0.2") == 0, "11.0.2 formats");
	check(std::strcmp(version_string(IPVersion{13, 0, 7}), "13.0.7") == 0, "13.0.7 formats (no '=' bug)");
	check(std::strcmp(version_string(IPVersion{14, 0, 3}), "14.0.3") == 0, "14.0.3 formats");

	// One log line passes six versions at once; a single shared static buffer
	// would make all six arguments print the last value.
	{
		const char *a = version_string(IPVersion{11, 0, 2});
		const char *b = version_string(IPVersion{13, 0, 7});
		const char *c = version_string(IPVersion{3, 0, 0});
		check(std::strcmp(a, "11.0.2") == 0, "first version survives a later call");
		check(std::strcmp(b, "13.0.7") == 0, "second version survives a later call");
		check(std::strcmp(c, "3.0.0") == 0, "third version survives a later call");
		check(a != b && b != c, "concurrent version strings must be distinct buffers");
	}

	if (g_fail == 0) {
		std::printf("asic_profile: OK\n");
		return 0;
	}
	std::printf("asic_profile: FAILED %d check(s)\n", g_fail);
	return 1;
}