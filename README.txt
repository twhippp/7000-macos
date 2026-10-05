Navi48-MacOS
The first public unofficial MacOS GPU driver.

What this repo contains
-----------------------
  src/navi48-bringup   Bring-up kext (IOKit): PCIe/BAR setup, IP discovery, PSP/SMU/GMC/
                       GFX/SDMA/MES initialisation, display (DCN 4.1) mode-setting, a
                       user client ("N48N") exposing buffers, command submission and
                       scanout, and hooks into Apple's AMDRadeonX6000 accelerator.
  src/dcn41            DCN 4.1 display-engine helpers and generated register headers
                       (derived from Linux amdgpu, MIT).
  src/xlat12           Command-stream translation from Apple's GFX10.3 PM4/register
                       programming to gfx12.
  src/g2capture        Small capture tool for Apple shader-compiler records.
  tools/native         navi48metal (a Metal device bundle that runs Metal on top of the
                       Vulkan driver), mtlprobe (API probe), autotranslate (AIR -> SPIR-V
                       translation scripts) and navi48accel (aux kext).
  tools/pc, tools/dcn41, tools/conductor
                       PC-side test CLI and run scripts, display-register tooling, test
                       suite runners and "planted break" mutation tests.
  mesa-patches         RADV (Mesa Vulkan) Darwin port as five patches on a pinned Mesa
                       commit; see mesa-patches/README.txt.

Status
------
Reached on one x86_64 test PC with an RX 9070 XT: a 60 fps
GPU-composited macOS desktop on one DisplayPort display, macOS Tahoe 26.6.2, x86_64
PC booted through OpenCore. The Metal compositor runs on this driver stack. A lot is
still rough. Only one display path has been exercised at length, performance and
stability work is ongoing, and many pieces exist to work around one specific macOS
build.

Coming Soon
-----------
Multi-monitor support
Multiple display paths
Native GPU rendering for app content

In progress: RDNA3 (Navi 33 / gfx1102)
----------------------------------------
The kext is being extended to a second ASIC family, currently targeting the RX 7600
(PCI 0x7480). Only the detection stage exists so far: `navi33bringup=1` makes the
bring-up personality claim a Navi 33 card, log the on-die IP versions and the MMHUB
framebuffer location, and attach read-only. No gfx11 firmware is loaded and no
functional register is programmed, so a Navi 33 card will not produce a desktop yet.
The survey is read back with `navi48test log` — an OpenCore-injected kext does not
appear in the unified log. MMHUB register offsets are selected from the discovered IP
version rather than from the device name, since MMHUB 3.0 and 4.1 place the same
registers at different offsets. The profile matching and the table selection are
host-tested (no kext, SDK or firmware needed) as `asic_profile` in
tools/conductor/suites.sh.

Can I install this?
-------------------
Not reliably. Wait for the first official public release
for stability and usability. This is just the code.

External projects and dependencies (not vendored; clone them yourself)
----------------------------------------------------------------------
  RDNA4FB      https://github.com/somestupidgirl/RDNA4FB   (display-only RDNA4 kext, MacKernelSDK)
  mac-amdgpu   https://github.com/lemonade-sdk/mac-amdgpu  (Navi 48 bring-up on macOS)
  USBToolBox   https://github.com/USBToolBox/tool          (USB port mapping, optional)
  metal2vulkan https://github.com/steelbrain/metal2vulkan  (Metal AIR -> SPIR-V translator,
               LGPL-3.0-or-later; used as an external tool by tools/native, not included)
  Mesa         https://gitlab.freedesktop.org/mesa/mesa    (RADV, MIT; see mesa-patches/)
  linux-firmware (AMD firmware blobs, own licence; see above)

Credits
-------
RDNA4FB (Sunneva N. Mariu), mac-amdgpu (lemonade-sdk / Geramy Loveless), the Mesa
project (RADV and the register databases), the Linux amdgpu driver authors at AMD and
the community (register definitions and documentation), and the metal2vulkan author.
