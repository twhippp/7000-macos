Mesa patches (RADV Darwin / macOS port for the Navi 48 native kext)
===================================================================

These six patches add a macOS backend to Mesa's RADV Vulkan driver so that it
talks to the Navi48 bring-up kext (user-client ABI "N48N") instead of a DRM
node. Only our changes are included, not the Mesa tree.

Upstream base (apply on top of exactly this commit):
    mesa  f5cb8ee032adabef599ae892ec4e42d796b84da9   (VERSION 26.3.0-devel)
    https://gitlab.freedesktop.org/mesa/mesa.git

Licence: Mesa is MIT licensed. These patches are changes to Mesa and are
released under the same licence (see Mesa's docs/license.rst and
third-party/THIRD-PARTY-LICENSES.txt).

Apply:
    git clone https://gitlab.freedesktop.org/mesa/mesa.git
    cd mesa
    git checkout f5cb8ee032adabef599ae892ec4e42d796b84da9
    git am /path/to/Navi48-MacOS/mesa-patches/*.patch
(The patch author and date headers are placeholders; "git am" works as-is,
or use "git apply" / "patch -p1" in order.)

Order matters: 0001 .. 0006. 0006 picks the canned device from the GC version the
kext reports (12.0.1 -> gfx1201 / Navi 48, 11.0.2 -> navi33 / RX 7600, gfx1102);
RADV_DARWIN_CANNED=<name> overrides it, RADV_DARWIN_MOCK_N33=1 makes the mock answer
as the RX 7600 kext.

On a Mac, tools/build-mesa-mac.sh does the fetch, apply and RADV-only build in one go. The kext ABI header that patches 0002-0004 mirror
is src/navi48-bringup/src/Navi48NativeABI.h in this repository; Mesa's copy and
the kext's copy must be the same ABI version (currently 1.9 or newer).
