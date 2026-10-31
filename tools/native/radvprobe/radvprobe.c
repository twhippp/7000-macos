/*
 * radvprobe: load libvulkan_radeon.dylib directly (no Vulkan loader), create an instance and
 * print the physical device RADV enumerates: name, PCI id, API version, queues, memory heaps.
 *
 *   radvprobe [path/to/libvulkan_radeon.dylib] [--fill]
 *
 * --fill also creates a device, fills a 64 KiB host-visible buffer with vkCmdFillBuffer on the
 * first graphics/compute queue, waits up to 5 s on a fence and checks every dword. That is the
 * first real submit through the kext; only use it on the real GPU when you mean to.
 *
 * Safe check with no GPU access:  RADV_DARWIN_MOCK=1 RADV_DARWIN_MOCK_N33=1 radvprobe <dylib>
 * Exit: 0 ok, 1 load/instance failure, 2 no device, 3 fill failed or wrong data.
 *
 * Build: clang -std=gnu11 -O1 -I <mesa>/include radvprobe.c -o radvprobe
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

typedef PFN_vkVoidFunction (*PFN_icdGIPA)(VkInstance, const char *);
typedef VkResult (*PFN_icdNegotiate)(uint32_t *);

static PFN_icdGIPA gipa;
static VkInstance inst;

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
   vkDeviceWaitIdle(dev);
   vkDestroyDevice(dev, NULL); /* the rest is freed with the device and at process exit */
   return 0;
}

int
main(int argc, char **argv)
{
   const char *path = "libvulkan_radeon.dylib";
   int fill = 0;
   for (int i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--fill"))
         fill = 1;
      else
         path = argv[i];
   }

   void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
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

   if (fill)
      return fill_test(pds[0]);
   return 0;
}
