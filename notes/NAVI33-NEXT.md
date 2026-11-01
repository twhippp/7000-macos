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

## Status 2026-10-31
- Mesa built on the Hackintosh (Sequoia, x86_64): pinned f5cb8ee + 0001-0006 applied cleanly,
  775/775, `~/navi48-native/mesa-mac/build-x86_64/src/amd/vulkan/libvulkan_radeon.dylib` (26.9 MB).
  Undefined: 6601 weak layer entrypoints (by design) + system libs only (libSystem, libc++, IOKit, libz).
- Build env without Homebrew/sudo: uv Python 3.12 venv + glslang 16.4.0 from source, both in
  `~/navi48-native/tools`. The tools/build-mesa-mac.sh handles both.
- Kext with the GB_ADDR_CONFIG fix: CI run 41226717600 (workflow_dispatch on this branch).

- 2026-10-31, real RX 7600 (kext build 33012 + registerService fix, CI run 41261603715):
  radvprobe lists "AMD Radeon RX 7600 (RADV NAVI33)", 0x7480, GC 11.0.2 -> canned navi33,
  GB_ADDR_CONFIG 0x343, VRAM 216 MiB visible + 0 hi, GTT cap 512 MiB.
  `radvprobe --fill`: device created, vkCmdFillBuffer 64 KiB (RADV's compute-shader fill on the
  gfx queue, VMID 8) -> FILL OK, 16384/16384 0xdeadbeef. First Vulkan work on Navi 33 under macOS.
- `radvprobe --triangle` on the RX 7600: clear + one triangle (vertex/fragment, dynamic rendering)
  + copy back -> TRIANGLE OK, 2016 red / 2080 blue / 0 other, identical to lavapipe. First
  graphics work on Navi 33 under macOS.
- `radvprobe --present` on the RX 7600 (kext CI run 41268991458, copy-to-console scanout): console
  1920x1080 pitch 1920; rendered pixels correct; acquire/register/present/release all 0; the
  triangle was SEEN on the monitor for 5 s and the desktop came back (restore retired). First
  GPU-rendered frame on screen on Navi 33 under macOS. No vsync (copy, not flip): tearing expected.
- Known limits: only the gfx queue (compute/transfer submits are refused by cs_parse), all BOs in
  the BAR0-visible pool (216 MiB), GTT requests also land there.

## Next
1-3. Done (mock check, real enumerate, first submit: see Status).
4. Things gfx11 RADV may want that gfx12 didn't: check for READ_MMR_REG of anything beyond
   GB_ADDR_CONFIG, and the firmware versions RADV gates features on (canned me 2390 / pfp 2530 /
   mec 2600 vs what the kext loaded).
5. Scanout: copy-to-console done (above). Real flips on DCN 3.2 (vsync, no console fight) later.
6. Next big piece: the Metal side (tools/native/navi48metal) on Navi 33, so macOS itself draws
   through RADV. Then more VRAM (the hi pool above BAR0), compute/SDMA queues.
