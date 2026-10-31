# Navi 33 — where the work stands (after the app door)

Stages 1-12 pass on the RX 7600 and the N48N app door (`src/amd/navi33_native.cpp`, `n33n_*`)
is in: `N33,Native` says ready.

## Fixed in the kext (needs a fresh build)
- `kGbAddrConfig` was 0x263e, which is already the absolute dword address; `navi33_gc_reg` added
  the GC seg-0 base again, so QueryInfo's `gb_addr_config` read the wrong register and ReadRegs
  turned down Mesa's read of 0x263e (its window was base+0x263e). Now 0x13de (gc_11_0_0_offset.h,
  BASE_IDX 0) -> 0x263e. Without this Mesa's handshake fails at ReadRegs(GB_ADDR_CONFIG).

## Mesa for gfx1102
- Mesa already ships a canned `navi33` entry in amdgpu_devices.c (a real RX 7600: 0x7480,
  2 SE x 2 SA, 32 CU, GB_ADDR_CONFIG 0x343, gfx 11.0.2), so no hand-made radeon_info.
- `mesa-patches/0006`: the handshake picks the canned entry from QueryInfo's `gc_version`
  (12.0.1 -> gfx1201, 11.0.2 -> navi33) and checks the live GB_ADDR_CONFIG against that entry.
  `RADV_DARWIN_CANNED=<name>` overrides; `RADV_DARWIN_MOCK_N33=1` makes the mock answer as N33.
  Checked off-Mac against Mesa 26.2.4 with the patches applied: mock N48 -> 0x7550/GB 0x08200545,
  mock N33 -> 0x7480/GB 0x343, mismatch refused; acd_kext_test 95/95.
- PRT half-VA reservation stays on (Linux does it for gfx6-12 except gfx9).
- Memory heaps come live from the kext (visible pool only on N33: `vram_hi_total` is 0).
- Build: `tools/build-mesa-mac.sh` (pinned f5cb8ee, patches 0001-0006, RADV only, ACO, no LLVM,
  output `~/navi48-native/mesa-mac/build-x86_64/src/amd/vulkan/libvulkan_radeon.dylib`).

## Next
1. Build Mesa on the Mac; run the mock check (`RADV_DARWIN_MOCK=1 RADV_DARWIN_MOCK_N33=1`).
2. Load the new kext, then real `vulkaninfo` against it: expect "GC 11.0.2 -> canned navi33".
3. First real submit (a vkCmdFillBuffer / compute dispatch) on the gfx queue through VMID 8.
4. Things gfx11 RADV may want that gfx12 didn't: check for READ_MMR_REG of anything beyond
   GB_ADDR_CONFIG, and the firmware versions RADV gates features on (canned me 2390 / pfp 2530 /
   mec 2600 vs what the kext loaded).
5. Then scanout (copy into the boot framebuffer first; DCN 3.2 later).
