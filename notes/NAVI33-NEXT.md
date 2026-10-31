# Navi 33 — where the work stands (after stage 12)

Stages 1-12 pass on the RX 7600. Next: the app door (N48N user client) on Navi 33.

Plan (decided, not started):
- New engine `src/amd/navi33_native.cpp` exposing `n33n_*` with the same signatures as `n1c_*`
  (hello, query_info, read_regs, bo_create/free, bo_import_host, gem_va, ctx, submit, wait,
  memory_for_handle). Scanout/DAL/mode/metal-nub selectors -> Unsupported for now.
- `Navi48NativeClient`: a `n33` flag set at create(); each selector calls n33n_* instead of n1c_*.
- Reuse the pure helpers in native_s1c_pure.h (place_decide, gemva_check, cs_parse, VA map, pt_map
  templates, wait/hang logic). gfx12-specific parts to replace for gfx11:
  - PTE: no IS_PTE bit 63; MTYPE at bits 48-50 (UC = 3 << 48); SYSTEM bit1, SNOOPED bit2 same.
  - build_submit: same shape (CONTEXT_CONTROL, IB with vmid<<24, RELEASE_MEM) — check gfx11 GCR bits.
- VMID 8 setup = stage 12 (navi33_vm_test): CONTEXT8 regs, depth 3, TLB flush via the compute
  queue (MMIO flush never acks).
- Ring = our gfx queue (gGfxWptrDw, doorbell 0x116, ring 0x10000 in the slot, 2048 dwords).
- Seq/fence slot: in the slot VRAM (BAR0 readable).
- Pools: visible VRAM [vramBase+32 MiB, bar0Size) minus the console FB; hi pool [bar0Size, TMR)
  (TMR sits at vramSize - 32 MiB - tmrSize); GTT = IOBufferMemoryDescriptor, PTEs SYSTEM|SNOOPED.
- Then Mesa: patch 0001's canned gfx1201 device -> gfx1102 radeon_info. Then scanout (copy into the
  boot framebuffer first; DCN 3.2 later).
