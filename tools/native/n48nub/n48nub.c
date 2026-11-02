// n48nub: publish / withdraw the Navi48 Metal nub (N48N selectors 19 / 20) so the aux kext (Navi48Accel) can match it.
// Needs the boot-arg navi48-metal=1. Build: clang -O1 n48nub.c -framework IOKit -o n48nub     Usage: sudo ./n48nub publish|withdraw
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>

#define N48N_UC_TYPE 0x4E34384Eu   /* 'N48N' */

int main(int argc, char **argv) {
    if (argc != 2 || (strcmp(argv[1], "publish") && strcmp(argv[1], "withdraw"))) { fprintf(stderr, "usage: n48nub publish|withdraw\n"); return 64; }
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("Navi48Bringup"));
    if (!svc) { printf("n48nub: no Navi48Bringup service\n"); return 1; }
    io_connect_t c = 0;
    kern_return_t kr = IOServiceOpen(svc, mach_task_self(), N48N_UC_TYPE, &c);
    IOObjectRelease(svc);
    if (kr) { printf("n48nub: open failed 0x%08x (another client open? run as root)\n", kr); return 1; }
    uint64_t in[2] = { 1, 1u << 8 }, out[4] = { 0 }; uint32_t n = 4;
    kr = IOConnectCallScalarMethod(c, 0, in, 2, out, &n);
    if (kr) { printf("n48nub: hello failed 0x%08x\n", kr); IOServiceClose(c); return 1; }
    printf("n48nub: hello ABI %llu.%llu\n", out[0] & 0xffff, out[0] >> 16);
    uint64_t f = 0; memset(out, 0, sizeof out);
    if (!strcmp(argv[1], "publish")) {
        n = 4; kr = IOConnectCallScalarMethod(c, 19, &f, 1, out, &n);
        printf("publish: %d (%s) state %llu nub id %#llx ops ABI %llu size %llu\n", kr, kr ? "refused" : "Success", out[0], out[1], out[2], out[3]);
    } else {
        n = 1; kr = IOConnectCallScalarMethod(c, 20, &f, 1, out, &n);
        printf("withdraw: %d (%s) previous state %llu\n", kr, kr ? "refused" : "Success", out[0]);
    }
    IOServiceClose(c);
    return kr ? 1 : 0;
}
