/*
 * radvprobe: load libvulkan_radeon.dylib directly (no Vulkan loader), create an instance and
 * print the physical device RADV enumerates: name, PCI id, API version, queues, memory heaps.
 *
 *   radvprobe [path/to/libvulkan_radeon.dylib] [--fill] [--triangle] [--present]
 *
 * --fill also creates a device, fills a 64 KiB host-visible buffer with vkCmdFillBuffer on the
 * first graphics/compute queue, waits up to 5 s on a fence and checks every dword. That is the
 * first real submit through the kext; only use it on the real GPU when you mean to.
 *
 * --triangle clears a 64x64 RGBA8 image to blue, draws one red triangle over its top-left half
 * (vertex + fragment shader, dynamic rendering), copies the image to a host-visible buffer and checks
 * the corners and the red pixel count (2016..2080 of 4096: the 64 pixels on the diagonal go either
 * way by the fill rule). On failure it writes the pixels to triangle.ppm for a look.
 *
 * --present draws the same picture at the console's size and shows it through the kext's scanout
 * (radv_darwin_scanout_*): acquire, register, present, hold 5 s, release (the console comes back).
 *
 * Safe check with no GPU access:  RADV_DARWIN_MOCK=1 RADV_DARWIN_MOCK_N33=1 radvprobe <dylib>
 * Exit: 0 ok, 1 load/instance failure, 2 no device, 3 fill failed or wrong data.
 *
 * Build: clang -std=gnu11 -O1 -I <mesa>/include radvprobe.c -o radvprobe
 */
#include <dlfcn.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "tri_shaders.h"

typedef PFN_vkVoidFunction (*PFN_icdGIPA)(VkInstance, const char *);
typedef VkResult (*PFN_icdNegotiate)(uint32_t *);

static PFN_icdGIPA gipa;
static VkInstance inst;
static void *g_lib;

/* radv_darwin_scanout.h (Mesa patch 0003), looked up with dlsym; the structs are those of the kext ABI. */
struct n48n_scan_query {
   uint32_t h_active, v_active, h_total, v_total, pitch_px, hubp_format, sw_mode, otg;
   uint32_t refresh_mhz, pix_clk_khz, dcc_en, flags;
   uint64_t frame_count, console_mc, plane_mc, earliest_mc;
   uint32_t acquired, plane_w, plane_h, reserved;
};
struct n48n_scan_status_head { /* the first 64 bytes of n48n_scan_status (256 B) */
   uint32_t acquired, flags, front_slot, pending_slot;
   uint64_t frame_count, console_mc, plane_mc, earliest_mc, presents, latched;
};
typedef int (*PFN_scan_query)(VkDevice, struct n48n_scan_query *);
typedef int (*PFN_scan_acquire)(VkDevice, uint64_t *, uint64_t *);
typedef int (*PFN_scan_register)(VkDevice, VkDeviceMemory, uint64_t, uint32_t, uint32_t, uint32_t, uint32_t *, uint64_t *);
typedef int (*PFN_scan_present)(VkDevice, uint32_t, uint64_t[3]);
typedef int (*PFN_scan_status)(VkDevice, void *);
typedef int (*PFN_scan_release)(VkDevice, uint64_t[2]);

#define LOAD(name) PFN_##name name = (PFN_##name)gipa(inst, #name)
#define CHECK(call)                                                                                \
   do {                                                                                            \
      VkResult r_ = (call);                                                                        \
      if (r_ != VK_SUCCESS) {                                                                      \
         fprintf(stderr, "radvprobe: %s -> VkResult %d\n", #call, r_);                             \
         return 3;                                                                                 \
      }                                                                                            \
   } while (0)

static int
fill_test(VkPhysicalDevice pd)
{
   LOAD(vkGetPhysicalDeviceQueueFamilyProperties);
   LOAD(vkGetPhysicalDeviceMemoryProperties);
   LOAD(vkCreateDevice);
   LOAD(vkGetDeviceProcAddr);

   uint32_t nq = 0;
   vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, NULL);
   VkQueueFamilyProperties qf[16];
   if (nq > 16)
      nq = 16;
   vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, qf);
   uint32_t fam = UINT32_MAX;
   for (uint32_t i = 0; i < nq && fam == UINT32_MAX; i++)
      if (qf[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
         fam = i;
   if (fam == UINT32_MAX) {
      fprintf(stderr, "radvprobe: no graphics/compute queue family\n");
      return 3;
   }

   const float prio = 1.0f;
   VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, fam, 1, &prio};
   VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
                             .pQueueCreateInfos = &qci};
   VkDevice dev;
   CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
   printf("device created (queue family %u)\n", fam);

#define DLOAD(name) PFN_##name name = (PFN_##name)vkGetDeviceProcAddr(dev, #name)
   DLOAD(vkGetDeviceQueue);
   DLOAD(vkCreateBuffer);
   DLOAD(vkGetBufferMemoryRequirements);
   DLOAD(vkAllocateMemory);
   DLOAD(vkBindBufferMemory);
   DLOAD(vkMapMemory);
   DLOAD(vkCreateCommandPool);
   DLOAD(vkAllocateCommandBuffers);
   DLOAD(vkBeginCommandBuffer);
   DLOAD(vkCmdFillBuffer);
   DLOAD(vkCmdPipelineBarrier);
   DLOAD(vkEndCommandBuffer);
   DLOAD(vkCreateFence);
   DLOAD(vkQueueSubmit);
   DLOAD(vkWaitForFences);
   DLOAD(vkDeviceWaitIdle);
   DLOAD(vkDestroyFence);
   DLOAD(vkDestroyCommandPool);
   DLOAD(vkDestroyBuffer);
   DLOAD(vkFreeMemory);
   DLOAD(vkDestroyDevice);

   VkQueue q;
   vkGetDeviceQueue(dev, fam, 0, &q);

   const VkDeviceSize size = 64 << 10;
   VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
                             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
   VkBuffer buf;
   CHECK(vkCreateBuffer(dev, &bci, NULL, &buf));
   VkMemoryRequirements mr;
   vkGetBufferMemoryRequirements(dev, buf, &mr);

   VkPhysicalDeviceMemoryProperties mp;
   vkGetPhysicalDeviceMemoryProperties(pd, &mp);
   const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   uint32_t type = UINT32_MAX;
   for (uint32_t i = 0; i < mp.memoryTypeCount && type == UINT32_MAX; i++)
      if ((mr.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
         type = i;
   if (type == UINT32_MAX) {
      fprintf(stderr, "radvprobe: no host-visible coherent memory type for the buffer\n");
      return 3;
   }
   VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, type};
   VkDeviceMemory mem;
   CHECK(vkAllocateMemory(dev, &mai, NULL, &mem));
   CHECK(vkBindBufferMemory(dev, buf, mem, 0));
   uint32_t *p;
   CHECK(vkMapMemory(dev, mem, 0, size, 0, (void **)&p));
   memset(p, 0, size);

   VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = fam};
   VkCommandPool pool;
   CHECK(vkCreateCommandPool(dev, &pci, NULL, &pool));
   VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool,
                                      VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
   VkCommandBuffer cb;
   CHECK(vkAllocateCommandBuffers(dev, &cai, &cb));
   VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                   .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
   CHECK(vkBeginCommandBuffer(cb, &cbi));
   const uint32_t magic = 0xDEADBEEFu;
   vkCmdFillBuffer(cb, buf, 0, VK_WHOLE_SIZE, magic);
   VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
   CHECK(vkEndCommandBuffer(cb));

   VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   CHECK(vkCreateFence(dev, &fci, NULL, &fence));
   VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};
   CHECK(vkQueueSubmit(q, 1, &si, fence));
   printf("submitted vkCmdFillBuffer(0x%08x, %llu bytes), waiting...\n", magic, (unsigned long long)size);
   VkResult wr = vkWaitForFences(dev, 1, &fence, VK_TRUE, 5000000000ull);
   if (wr != VK_SUCCESS) {
      fprintf(stderr, "radvprobe: fence wait -> VkResult %d (%s)\n", wr, wr == VK_TIMEOUT ? "timeout, GPU hung?" : "error");
      return 3;
   }
   uint32_t bad = 0, first_bad = 0;
   for (uint32_t i = 0; i < size / 4; i++)
      if (p[i] != magic && bad++ == 0)
         first_bad = i;
   if (bad) {
      printf("FILL WRONG: %u of %llu dwords differ, first at dword %u = 0x%08x\n", bad,
             (unsigned long long)(size / 4), first_bad, p[first_bad]);
      return 3;
   }
   printf("FILL OK: all %llu dwords are 0x%08x\n", (unsigned long long)(size / 4), magic);
   fflush(stdout);
   vkDeviceWaitIdle(dev);
   vkDestroyFence(dev, fence, NULL);
   vkDestroyCommandPool(dev, pool, NULL);
   vkDestroyBuffer(dev, buf, NULL);
   vkFreeMemory(dev, mem, NULL);
   vkDestroyDevice(dev, NULL);
   return 0;
}

/* Index of a memory type in `bits` that has all of `want`, or UINT32_MAX. */
static uint32_t
find_type(const VkPhysicalDeviceMemoryProperties *mp, uint32_t bits, VkMemoryPropertyFlags want)
{
   for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
      if ((bits & (1u << i)) && (mp->memoryTypes[i].propertyFlags & want) == want)
         return i;
   return UINT32_MAX;
}

/* present == 0: the 64x64 RGBA8 check.  present == 1: the same picture at the console's size in B8G8R8A8, copied
 * into a registered scanout buffer and presented through the kext; held for 5 s, then the console is given back. */
static int
triangle_test(VkPhysicalDevice pd, int present)
{
   uint32_t W = 64, H = 64, rowPx = 64;
   const VkFormat fmt = present ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
   LOAD(vkGetPhysicalDeviceQueueFamilyProperties);
   LOAD(vkGetPhysicalDeviceMemoryProperties);
   LOAD(vkCreateDevice);
   LOAD(vkGetDeviceProcAddr);

   uint32_t nq = 0;
   vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, NULL);
   VkQueueFamilyProperties qf[16];
   if (nq > 16)
      nq = 16;
   vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, qf);
   uint32_t fam = UINT32_MAX;
   for (uint32_t i = 0; i < nq && fam == UINT32_MAX; i++)
      if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
         fam = i;
   if (fam == UINT32_MAX) {
      fprintf(stderr, "radvprobe: no graphics queue family\n");
      return 3;
   }

   const float prio = 1.0f;
   VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, fam, 1, &prio};
   VkPhysicalDeviceVulkan13Features f13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                                           .dynamicRendering = VK_TRUE, .synchronization2 = VK_TRUE};
   VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &f13, .queueCreateInfoCount = 1,
                             .pQueueCreateInfos = &qci};
   VkDevice dev;
   CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
   printf("device created (queue family %u, dynamic rendering)\n", fam);

#define DLOAD(name) PFN_##name name = (PFN_##name)vkGetDeviceProcAddr(dev, #name)
   DLOAD(vkGetDeviceQueue);
   DLOAD(vkCreateImage);
   DLOAD(vkGetImageMemoryRequirements);
   DLOAD(vkBindImageMemory);
   DLOAD(vkCreateImageView);
   DLOAD(vkCreateBuffer);
   DLOAD(vkGetBufferMemoryRequirements);
   DLOAD(vkAllocateMemory);
   DLOAD(vkBindBufferMemory);
   DLOAD(vkMapMemory);
   DLOAD(vkCreateShaderModule);
   DLOAD(vkCreatePipelineLayout);
   DLOAD(vkCreateGraphicsPipelines);
   DLOAD(vkCreateCommandPool);
   DLOAD(vkAllocateCommandBuffers);
   DLOAD(vkBeginCommandBuffer);
   DLOAD(vkCmdPipelineBarrier);
   DLOAD(vkCmdBeginRendering);
   DLOAD(vkCmdBindPipeline);
   DLOAD(vkCmdDraw);
   DLOAD(vkCmdEndRendering);
   DLOAD(vkCmdCopyImageToBuffer);
   DLOAD(vkEndCommandBuffer);
   DLOAD(vkCreateFence);
   DLOAD(vkQueueSubmit);
   DLOAD(vkWaitForFences);
   DLOAD(vkDeviceWaitIdle);
   DLOAD(vkDestroyFence);
   DLOAD(vkDestroyCommandPool);
   DLOAD(vkDestroyPipeline);
   DLOAD(vkDestroyPipelineLayout);
   DLOAD(vkDestroyShaderModule);
   DLOAD(vkDestroyBuffer);
   DLOAD(vkDestroyImageView);
   DLOAD(vkDestroyImage);
   DLOAD(vkFreeMemory);
   DLOAD(vkDestroyDevice);

   VkQueue q;
   vkGetDeviceQueue(dev, fam, 0, &q);
   VkPhysicalDeviceMemoryProperties mp;
   vkGetPhysicalDeviceMemoryProperties(pd, &mp);

   PFN_scan_query scan_query = NULL;
   PFN_scan_acquire scan_acquire = NULL;
   PFN_scan_register scan_register = NULL;
   PFN_scan_present scan_present = NULL;
   PFN_scan_status scan_status = NULL;
   PFN_scan_release scan_release = NULL;
   if (present) {
      scan_query = (PFN_scan_query)dlsym(g_lib, "radv_darwin_scanout_query");
      scan_acquire = (PFN_scan_acquire)dlsym(g_lib, "radv_darwin_scanout_acquire");
      scan_register = (PFN_scan_register)dlsym(g_lib, "radv_darwin_scanout_register");
      scan_present = (PFN_scan_present)dlsym(g_lib, "radv_darwin_scanout_present");
      scan_status = (PFN_scan_status)dlsym(g_lib, "radv_darwin_scanout_status");
      scan_release = (PFN_scan_release)dlsym(g_lib, "radv_darwin_scanout_release");
      if (!scan_query || !scan_acquire || !scan_register || !scan_present || !scan_status || !scan_release) {
         fprintf(stderr, "radvprobe: this driver has no radv_darwin_scanout_* (not the macOS RADV build)\n");
         return 3;
      }
      struct n48n_scan_query sq;
      int r = scan_query(dev, &sq);
      if (r || !sq.plane_w || !sq.plane_h || sq.pitch_px < sq.plane_w) {
         fprintf(stderr, "radvprobe: scanout query -> %d, plane %ux%u pitch %u px, flags 0x%x\n", r, sq.plane_w, sq.plane_h,
                 sq.pitch_px, sq.flags);
         return 3;
      }
      W = sq.plane_w;
      H = sq.plane_h;
      rowPx = sq.pitch_px;
      printf("scanout: console %ux%u, pitch %u px, flags 0x%x\n", W, H, rowPx, sq.flags);
   }

   /* the render target */
   VkImageCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                            .format = fmt, .extent = {W, H, 1}, .mipLevels = 1, .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
   VkImage img;
   CHECK(vkCreateImage(dev, &ici, NULL, &img));
   VkMemoryRequirements mr;
   vkGetImageMemoryRequirements(dev, img, &mr);
   uint32_t t = find_type(&mp, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   if (t == UINT32_MAX)
      t = find_type(&mp, mr.memoryTypeBits, 0);
   VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, t};
   VkDeviceMemory imem;
   CHECK(vkAllocateMemory(dev, &mai, NULL, &imem));
   CHECK(vkBindImageMemory(dev, img, imem, 0));
   VkImageViewCreateInfo vci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img,
                                .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt,
                                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
   VkImageView view;
   CHECK(vkCreateImageView(dev, &vci, NULL, &view));

   /* the readback buffer; for --present the scanout buffer (visible VRAM, the console's pitch) */
   const VkDeviceSize bytes = (VkDeviceSize)rowPx * H * 4;
   VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = bytes,
                             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
   VkBuffer buf;
   CHECK(vkCreateBuffer(dev, &bci, NULL, &buf));
   vkGetBufferMemoryRequirements(dev, buf, &mr);
   t = find_type(&mp, mr.memoryTypeBits,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                    (present ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0));
   if (t == UINT32_MAX) {
      fprintf(stderr, "radvprobe: no host-visible coherent%s memory type for the %s buffer\n", present ? " device-local" : "",
              present ? "scanout" : "readback");
      return 3;
   }
   mai.allocationSize = mr.size;
   mai.memoryTypeIndex = t;
   VkDeviceMemory bmem;
   CHECK(vkAllocateMemory(dev, &mai, NULL, &bmem));
   CHECK(vkBindBufferMemory(dev, buf, bmem, 0));
   uint32_t *px;
   CHECK(vkMapMemory(dev, bmem, 0, bytes, 0, (void **)&px));
   if (!present)
      memset(px, 0x55, bytes); /* neither red nor blue: an untouched buffer cannot pass */

   /* the pipeline */
   VkShaderModuleCreateInfo smi = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                   .codeSize = sizeof(tri_vert_spv), .pCode = tri_vert_spv};
   VkShaderModule vs, fs;
   CHECK(vkCreateShaderModule(dev, &smi, NULL, &vs));
   smi.codeSize = sizeof(tri_frag_spv);
   smi.pCode = tri_frag_spv;
   CHECK(vkCreateShaderModule(dev, &smi, NULL, &fs));
   VkPipelineLayoutCreateInfo plci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
   VkPipelineLayout layout;
   CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &layout));

   VkPipelineShaderStageCreateInfo stages[2] = {
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vs, .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = fs, .pName = "main"},
   };
   VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
   VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
   VkViewport vp = {0, 0, W, H, 0, 1};
   VkRect2D sc = {{0, 0}, {W, H}};
   VkPipelineViewportStateCreateInfo vps = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                            .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc};
   VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
                                                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f};
   VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                              .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
   VkPipelineColorBlendAttachmentState cba = {.colorWriteMask = 0xf};
   VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                             .attachmentCount = 1, .pAttachments = &cba};
   VkPipelineRenderingCreateInfo pri = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                        .colorAttachmentCount = 1, .pColorAttachmentFormats = &fmt};
   VkGraphicsPipelineCreateInfo gpi = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &pri,
                                       .stageCount = 2, .pStages = stages, .pVertexInputState = &vi,
                                       .pInputAssemblyState = &ia, .pViewportState = &vps, .pRasterizationState = &rs,
                                       .pMultisampleState = &ms, .pColorBlendState = &cb, .layout = layout};
   VkPipeline pipe;
   CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpi, NULL, &pipe));
   printf("pipeline compiled (vertex + fragment)\n");

   /* record: clear blue + draw, then copy to the buffer */
   VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = fam};
   VkCommandPool pool;
   CHECK(vkCreateCommandPool(dev, &pci, NULL, &pool));
   VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool,
                                      VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
   VkCommandBuffer cmd;
   CHECK(vkAllocateCommandBuffers(dev, &cai, &cmd));
   VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                   .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
   CHECK(vkBeginCommandBuffer(cmd, &cbi));

   VkImageMemoryBarrier ib = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                              .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                              .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                              .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .image = img, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                        NULL, 0, NULL, 1, &ib);
   VkRenderingAttachmentInfo att = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = view,
                                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                    .clearValue.color.float32 = {0.0f, 0.0f, 1.0f, 1.0f}};
   VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = sc, .layerCount = 1,
                         .colorAttachmentCount = 1, .pColorAttachments = &att};
   vkCmdBeginRendering(cmd, &ri);
   vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
   vkCmdDraw(cmd, 3, 1, 0, 0);
   vkCmdEndRendering(cmd);

   ib.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
   ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
   ib.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                        NULL, 0, NULL, 1, &ib);
   VkBufferImageCopy reg = {.bufferRowLength = rowPx, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                            .imageExtent = {W, H, 1}};
   vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &reg);
   VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
   CHECK(vkEndCommandBuffer(cmd));

   VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   CHECK(vkCreateFence(dev, &fci, NULL, &fence));
   VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
   CHECK(vkQueueSubmit(q, 1, &si, fence));
   printf("submitted clear + draw + copy, waiting...\n");
   VkResult wr = vkWaitForFences(dev, 1, &fence, VK_TRUE, 5000000000ull);
   if (wr != VK_SUCCESS) {
      fprintf(stderr, "radvprobe: fence wait -> VkResult %d (%s)\n", wr, wr == VK_TIMEOUT ? "timeout, GPU hung?" : "error");
      return 3;
   }

   int ok;
   if (present) {
      /* B8G8R8A8 dword = 0xAARRGGBB: spot-check two pixels through the (slow, uncached) CPU map */
      const uint32_t tl = px[2 * rowPx + 2], br = px[(H - 3) * rowPx + (W - 3)];
      printf("rendered %ux%u: top-left 0x%08x (want 0xffff0000), bottom-right 0x%08x (want 0xff0000ff)\n", W, H, tl, br);
      uint64_t con = 0, fc = 0, so[3] = {0, 0, 0}, ro[2] = {0, 0};
      uint32_t slot = 0;
      uint64_t slot_mc = 0;
      int r = scan_acquire(dev, &con, &fc);
      printf("scanout acquire -> %d (console MC 0x%llx)\n", r, (unsigned long long)con);
      if (!r) {
         r = scan_register(dev, bmem, 0, rowPx * 4, W, H, &slot, &slot_mc);
         printf("scanout register -> %d (slot %u, MC 0x%llx)\n", r, slot, (unsigned long long)slot_mc);
      }
      if (!r) {
         r = scan_present(dev, slot, so);
         printf("scanout present -> %d (present id %llu)\n", r, (unsigned long long)so[0]);
      }
      if (!r) {
         printf("ON SCREEN NOW: red triangle top-left, blue bottom-right. Holding 5 s...\n");
         fflush(stdout);
         for (int i = 0; i < 5; i++) {
            sleep(1);
            uint8_t st[256];
            memset(st, 0, sizeof(st));
            if (!scan_status(dev, st)) {
               const struct n48n_scan_status_head *h = (const void *)st;
               printf("  status: presents %llu, done %llu, front slot %u\n", (unsigned long long)h->presents,
                      (unsigned long long)h->latched, h->front_slot);
            }
         }
      }
      int rr = scan_release(dev, ro);
      printf("scanout release -> %d (console restored: %s)\n", rr, ro[0] ? "yes" : "NO");
      ok = !r && !rr && ro[0];
      printf("%s\n", ok ? "PRESENT OK (did the picture show?)" : "PRESENT FAILED");
   } else {
   /* check: R8G8B8A8 little-endian dword = 0xAABBGGRR */
   const uint32_t RED = 0xFF0000FFu, BLUE = 0xFFFF0000u;
   uint32_t red = 0, blue = 0, other = 0, first_other = 0;
   for (uint32_t i = 0; i < W * H; i++) {
      if (px[i] == RED)
         red++;
      else if (px[i] == BLUE)
         blue++;
      else if (other++ == 0)
         first_other = i;
   }
   const uint32_t tl = px[2 * W + 2], br = px[(H - 3) * W + (W - 3)];
   printf("pixels: %u red, %u blue, %u other; top-left 0x%08x, bottom-right 0x%08x\n", red, blue, other, tl, br);
   ok = tl == RED && br == BLUE && other == 0 && red >= 2016 && red <= 2080;
   if (!ok) {
      if (other)
         printf("first other pixel at (%u,%u) = 0x%08x\n", first_other % W, first_other / W, px[first_other]);
      FILE *f = fopen("triangle.ppm", "wb");
      if (f) {
         fprintf(f, "P6\n%d %d\n255\n", W, H);
         for (uint32_t i = 0; i < W * H; i++) {
            const unsigned char c[3] = {px[i] & 0xff, (px[i] >> 8) & 0xff, (px[i] >> 16) & 0xff};
            fwrite(c, 1, 3, f);
         }
         fclose(f);
         printf("wrote triangle.ppm\n");
      }
      printf("TRIANGLE WRONG\n");
   } else {
      printf("TRIANGLE OK: red top-left half, blue bottom-right half\n");
   }
   } /* !present */
   fflush(stdout);

   /* Every child object goes before the device: Mesa's debug build asserts on leftovers. */
   vkDeviceWaitIdle(dev);
   vkDestroyFence(dev, fence, NULL);
   vkDestroyCommandPool(dev, pool, NULL);
   vkDestroyPipeline(dev, pipe, NULL);
   vkDestroyPipelineLayout(dev, layout, NULL);
   vkDestroyShaderModule(dev, vs, NULL);
   vkDestroyShaderModule(dev, fs, NULL);
   vkDestroyBuffer(dev, buf, NULL);
   vkFreeMemory(dev, bmem, NULL);
   vkDestroyImageView(dev, view, NULL);
   vkDestroyImage(dev, img, NULL);
   vkFreeMemory(dev, imem, NULL);
   vkDestroyDevice(dev, NULL);
   return ok ? 0 : 3;
}

int
main(int argc, char **argv)
{
   const char *path = "libvulkan_radeon.dylib";
   int fill = 0, tri = 0, pres = 0;
   for (int i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--fill"))
         fill = 1;
      else if (!strcmp(argv[i], "--triangle"))
         tri = 1;
      else if (!strcmp(argv[i], "--present"))
         pres = 1;
      else
         path = argv[i];
   }

   void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
   g_lib = lib;
   if (!lib) {
      fprintf(stderr, "radvprobe: dlopen %s: %s\n", path, dlerror());
      return 1;
   }
   PFN_icdNegotiate negotiate = (PFN_icdNegotiate)dlsym(lib, "vk_icdNegotiateLoaderICDInterfaceVersion");
   if (negotiate) {
      uint32_t v = 5;
      negotiate(&v);
   }
   gipa = (PFN_icdGIPA)dlsym(lib, "vk_icdGetInstanceProcAddr");
   if (!gipa) {
      fprintf(stderr, "radvprobe: no vk_icdGetInstanceProcAddr in %s\n", path);
      return 1;
   }

   LOAD(vkCreateInstance);
   VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "radvprobe",
                            .apiVersion = VK_API_VERSION_1_3};
   VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
   VkResult r = vkCreateInstance(&ici, NULL, &inst);
   if (r != VK_SUCCESS) {
      fprintf(stderr, "radvprobe: vkCreateInstance -> VkResult %d\n", r);
      return 1;
   }

   LOAD(vkEnumeratePhysicalDevices);
   LOAD(vkGetPhysicalDeviceProperties);
   LOAD(vkGetPhysicalDeviceQueueFamilyProperties);
   LOAD(vkGetPhysicalDeviceMemoryProperties);

   uint32_t n = 0;
   r = vkEnumeratePhysicalDevices(inst, &n, NULL);
   printf("physical devices: %u (VkResult %d)\n", n, r);
   if (!n)
      return 2;
   VkPhysicalDevice pds[4];
   if (n > 4)
      n = 4;
   vkEnumeratePhysicalDevices(inst, &n, pds);

   for (uint32_t d = 0; d < n; d++) {
      VkPhysicalDeviceProperties pp;
      vkGetPhysicalDeviceProperties(pds[d], &pp);
      printf("[%u] %s\n    vendor 0x%04x device 0x%04x, Vulkan %u.%u.%u, driver 0x%x, type %d\n", d, pp.deviceName,
             pp.vendorID, pp.deviceID, VK_API_VERSION_MAJOR(pp.apiVersion), VK_API_VERSION_MINOR(pp.apiVersion),
             VK_API_VERSION_PATCH(pp.apiVersion), pp.driverVersion, pp.deviceType);

      uint32_t nq = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(pds[d], &nq, NULL);
      VkQueueFamilyProperties qf[16];
      if (nq > 16)
         nq = 16;
      vkGetPhysicalDeviceQueueFamilyProperties(pds[d], &nq, qf);
      for (uint32_t i = 0; i < nq; i++)
         printf("    queue family %u: flags 0x%x, %u queue(s)\n", i, qf[i].queueFlags, qf[i].queueCount);

      VkPhysicalDeviceMemoryProperties mp;
      vkGetPhysicalDeviceMemoryProperties(pds[d], &mp);
      for (uint32_t i = 0; i < mp.memoryHeapCount; i++)
         printf("    heap %u: %llu MiB%s\n", i, (unsigned long long)(mp.memoryHeaps[i].size >> 20),
                (mp.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? " device-local" : "");
   }

   if (fill) {
      int r = fill_test(pds[0]);
      if (r)
         return r;
   }
   if (tri) {
      int r = triangle_test(pds[0], 0);
      if (r)
         return r;
   }
   if (pres)
      return triangle_test(pds[0], 1);
   return 0;
}
