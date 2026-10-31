// navi33 stage 10: gfx1102 (RDNA3) wave32 compute shader that paints a colour gradient.
// Assemble: llvm-mc -arch=amdgcn -mcpu=gfx1102 -filetype=obj navi33_gradient.s -o g.o
//           llvm-objcopy -O binary --only-section=.text g.o g.bin   (words embedded in navi33_gfx.cpp)
.text
// s[0:1] = destination, s2 = pitch in pixels, s3 = frame seed, s4 = tgid_x, s5 = tgid_y
// v0 = packed thread id (x in bits 9:0). One wave32 per 32 pixels of a row.
v_and_b32 v1, 0x3ff, v0
s_lshl_b32 s6, s4, 5
v_add_nc_u32 v1, s6, v1            // x
s_mul_i32 s7, s5, s2               // y * pitch
v_add_nc_u32 v2, s7, v1            // pixel index
v_lshlrev_b32 v2, 2, v2            // byte offset
v_mov_b32 v5, s1
v_add_co_u32 v3, vcc_lo, s0, v2
v_add_co_ci_u32_e32 v4, vcc_lo, 0, v5, vcc_lo
v_add_nc_u32 v9, s3, v1            // x + seed
v_and_b32 v6, 0xff, v9
v_lshlrev_b32 v6, 16, v6           // R = x
v_mov_b32 v7, s5
v_and_b32 v7, 0xff, v7
v_lshlrev_b32 v7, 8, v7            // G = y
v_or_b32 v6, v6, v7
v_add_nc_u32 v8, s5, v9
v_and_b32 v8, 0xff, v8             // B = x + y
v_or_b32 v6, v6, v8
v_or_b32 v6, 0xff000000, v6
global_store_b32 v[3:4], v6, off
s_endpgm
