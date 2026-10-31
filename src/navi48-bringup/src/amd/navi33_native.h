//
//  navi33_native.h - the N48N engine for Navi 33 (gfx11): the same selectors as native_s1c (n1c_*),
//  on the queues and the VMID-8 address space that stages 9-12 brought up. Scanout is not here yet.
//
#pragma once
#include <IOKit/IOTypes.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "Navi48NativeABI.h"
#include "amdgpu_regs.h"

namespace amdgpu {

// Called once stage 12 passed: the engine becomes available to the user client.
void n33n_attach(DeviceContext &dev, uint64_t bootFbOff, uint64_t bootFbLen);
bool n33n_available();

IOReturn n33n_open();
void     n33n_close(const char *how);
bool     n33n_is_open();

IOReturn n33n_hello(uint64_t clientAbi, uint64_t flags, uint64_t out[4]);
IOReturn n33n_query_info(n48n_info *out);
IOReturn n33n_read_regs(uint64_t dwordOff, uint64_t count, uint64_t instance, uint32_t *out);
IOReturn n33n_bo_create(const n48n_gem_create_in *in, uint64_t out[4]);
IOReturn n33n_bo_free(uint64_t handle);
IOReturn n33n_gem_va(const n48n_gem_va *in);
IOReturn n33n_ctx(const n48n_ctx *in, n48n_ctx *out);
IOReturn n33n_submit(const uint8_t *in, uint32_t size, uint64_t *seqOut);
IOReturn n33n_wait(uint64_t target, uint64_t timeoutNs, uint64_t out[3]);
IOReturn n33n_memory_for_handle(uint32_t handle, IOOptionBits *options, IOMemoryDescriptor **memory);

} // namespace amdgpu
