#include "amdgpu_mmhub.h"

namespace amdgpu {

// The Navi48 path is proven on hardware, so the new table-driven selection must
// not be able to change a single byte it sees. kMmhub4_1_0 is transcribed
// independently from mmhub_4_1_0_offset.h; these assertions tie every field of
// it back to the constants the existing code compiles against. If upstream ever
// renames one, the build breaks here instead of silently reading a wrong offset.
static_assert(kMmhub4_1_0.FB_LOCATION_BASE   == MMHUBRegs::MMMC_VM_FB_LOCATION_BASE, "FB_LOCATION_BASE drifted");
static_assert(kMmhub4_1_0.FB_LOCATION_TOP    == MMHUBRegs::MMMC_VM_FB_LOCATION_TOP, "FB_LOCATION_TOP drifted");
static_assert(kMmhub4_1_0.FB_OFFSET          == MMHUBRegs::MMMC_VM_FB_OFFSET, "FB_OFFSET drifted");
static_assert(kMmhub4_1_0.AGP_TOP             == MMHUBRegs::MMMC_VM_AGP_TOP, "AGP_TOP drifted");
static_assert(kMmhub4_1_0.AGP_BOT             == MMHUBRegs::MMMC_VM_AGP_BOT, "AGP_BOT drifted");
static_assert(kMmhub4_1_0.AGP_BASE            == MMHUBRegs::MMMC_VM_AGP_BASE, "AGP_BASE drifted");
static_assert(kMmhub4_1_0.SYSTEM_APERTURE_LOW_ADDR  == MMHUBRegs::MMMC_VM_SYSTEM_APERTURE_LOW_ADDR, "sysap low drifted");
static_assert(kMmhub4_1_0.SYSTEM_APERTURE_HIGH_ADDR == MMHUBRegs::MMMC_VM_SYSTEM_APERTURE_HIGH_ADDR, "sysap high drifted");
static_assert(kMmhub4_1_0.SYSTEM_APERTURE_DEFAULT_ADDR_LSB == MMHUBRegs::MMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB, "sysap def lsb drifted");
static_assert(kMmhub4_1_0.SYSTEM_APERTURE_DEFAULT_ADDR_MSB == MMHUBRegs::MMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB, "sysap def msb drifted");
static_assert(kMmhub4_1_0.MX_L1_TLB_CNTL      == MMHUBRegs::MMMC_VM_MX_L1_TLB_CNTL, "MX_L1_TLB_CNTL drifted");
static_assert(kMmhub4_1_0.CONTEXT0_CNTL       == MMHUBRegs::MMVM_CONTEXT0_CNTL, "CONTEXT0_CNTL drifted");
static_assert(kMmhub4_1_0.CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32 == MMHUBRegs::MMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32, "ctx0 base lo drifted");
static_assert(kMmhub4_1_0.CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32 == MMHUBRegs::MMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32, "ctx0 base hi drifted");
static_assert(kMmhub4_1_0.CONTEXT0_PAGE_TABLE_START_ADDR_LO32 == MMHUBRegs::MMVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32, "ctx0 start lo drifted");
static_assert(kMmhub4_1_0.CONTEXT0_PAGE_TABLE_START_ADDR_HI32 == MMHUBRegs::MMVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32, "ctx0 start hi drifted");
static_assert(kMmhub4_1_0.CONTEXT0_PAGE_TABLE_END_ADDR_LO32 == MMHUBRegs::MMVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32, "ctx0 end lo drifted");
static_assert(kMmhub4_1_0.CONTEXT0_PAGE_TABLE_END_ADDR_HI32 == MMHUBRegs::MMVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32, "ctx0 end hi drifted");
static_assert(kMmhub4_1_0.CONTEXT1_CNTL       == MMHUBRegs::MMVM_CONTEXT1_CNTL, "CONTEXT1_CNTL drifted");
static_assert(kMmhub4_1_0.CONTEXT1_PAGE_TABLE_START_ADDR_LO32 == MMHUBRegs::MMVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32, "ctx1 start lo drifted");
static_assert(kMmhub4_1_0.CONTEXT1_PAGE_TABLE_START_ADDR_HI32 == MMHUBRegs::MMVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32, "ctx1 start hi drifted");
static_assert(kMmhub4_1_0.CONTEXT1_PAGE_TABLE_END_ADDR_LO32 == MMHUBRegs::MMVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32, "ctx1 end lo drifted");
static_assert(kMmhub4_1_0.CONTEXT1_PAGE_TABLE_END_ADDR_HI32 == MMHUBRegs::MMVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32, "ctx1 end hi drifted");
static_assert(kMmhub4_1_0.L2_CNTL             == MMHUBRegs::MMVM_L2_CNTL, "L2_CNTL drifted");
static_assert(kMmhub4_1_0.L2_CNTL2            == MMHUBRegs::MMVM_L2_CNTL2, "L2_CNTL2 drifted");
static_assert(kMmhub4_1_0.L2_CNTL3            == MMHUBRegs::MMVM_L2_CNTL3, "L2_CNTL3 drifted");
static_assert(kMmhub4_1_0.L2_CNTL4            == MMHUBRegs::MMVM_L2_CNTL4, "L2_CNTL4 drifted");
static_assert(kMmhub4_1_0.L2_CNTL5            == MMHUBRegs::MMVM_L2_CNTL5, "L2_CNTL5 drifted");
static_assert(kMmhub4_1_0.L2_PROTECTION_FAULT_CNTL2 == MMHUBRegs::MMVM_L2_PROTECTION_FAULT_CNTL2, "l2 pf cntl2 drifted");
static_assert(kMmhub4_1_0.L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32 == MMHUBRegs::MMVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32, "l2 pf lo drifted");
static_assert(kMmhub4_1_0.L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32 == MMHUBRegs::MMVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32, "l2 pf hi drifted");
static_assert(kMmhub4_1_0.L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32 == MMHUBRegs::MMVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32, "idap lo lo drifted");
static_assert(kMmhub4_1_0.L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32 == MMHUBRegs::MMVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32, "idap lo hi drifted");
static_assert(kMmhub4_1_0.L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32 == MMHUBRegs::MMVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32, "idap hi lo drifted");
static_assert(kMmhub4_1_0.L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32 == MMHUBRegs::MMVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32, "idap hi hi drifted");
static_assert(kMmhub4_1_0.L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32 == MMHUBRegs::MMVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32, "idphys lo drifted");
static_assert(kMmhub4_1_0.L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32 == MMHUBRegs::MMVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32, "idphys hi drifted");
static_assert(kMmhub4_1_0.INVALIDATE_ENG0_SEM == MMHUBRegs::MMVM_INVALIDATE_ENG0_SEM, "inv sem drifted");
static_assert(kMmhub4_1_0.INVALIDATE_ENG0_REQ == MMHUBRegs::MMVM_INVALIDATE_ENG0_REQ, "inv req drifted");
static_assert(kMmhub4_1_0.INVALIDATE_ENG0_ACK == MMHUBRegs::MMVM_INVALIDATE_ENG0_ACK, "inv ack drifted");
static_assert(kMmhub4_1_0.INVALIDATE_ENG0_ADDR_RANGE_LO32 == MMHUBRegs::MMVM_INVALIDATE_ENG0_ADDR_RANGE_LO32, "inv lo drifted");
static_assert(kMmhub4_1_0.INVALIDATE_ENG0_ADDR_RANGE_HI32 == MMHUBRegs::MMVM_INVALIDATE_ENG0_ADDR_RANGE_HI32, "inv hi drifted");

// 3.0.2 spot checks against mmhub_3_0_2_offset.h. The table was generated from
// the header, but these pin the two values every later stage depends on.
static_assert(kMmhub3_0_2.FB_LOCATION_BASE == 0x086c, "3.0.2 FB_LOCATION_BASE drifted");
static_assert(kMmhub3_0_2.FB_OFFSET        == 0x0857, "3.0.2 FB_OFFSET drifted");
static_assert(kMmhub3_0_2.FB_LOCATION_BASE == kMmhub3_0_0.FB_LOCATION_BASE - 0x80,
              "3.0.2 is the 3.0.0 map shifted down by 0x80");

namespace {
const MmhubRegs *g_mmhub = nullptr;
} // namespace

const MmhubRegs *mmhubRegsForVersion(const IPVersion &v) {
	if (v.major == 4) {
		// 4.1.0 is the only MMHUB 4.x this kext supports (Navi48).
		if (v.minor == 1) return &kMmhub4_1_0;
		return nullptr;
	}
	if (v.major == 3 && v.minor == 0) {
		// Select on the full triple. IPVersion is {major, minor, rev}, so the
		// .0.x patch level is `rev`, and it DOES change the register map:
		// mmhub_3_0_1_offset.h matches 3.0.0 field-for-field, but
		// mmhub_3_0_2_offset.h (the RX 7600's MMHUB, per on-die discovery) has
		// every register 0x80 dwords lower. An unknown rev returns nullptr so the
		// caller refuses to touch MMHUB rather than reading garbage.
		switch (v.rev) {
		case 0:
		case 1:  return &kMmhub3_0_0;
		case 2:  return &kMmhub3_0_2;
		default: return nullptr;
		}
	}
	return nullptr;
}

void          setMmhubRegs(const MmhubRegs *r) { g_mmhub = r; }
const MmhubRegs *mmhubRegs()                  { return g_mmhub; }
bool          mmhubRegsResolved()              { return g_mmhub != nullptr; }

} // namespace amdgpu