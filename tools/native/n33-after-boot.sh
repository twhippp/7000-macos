#!/bin/zsh
# n33-after-boot.sh - the RX 7600 checks after a reboot (kext >= 0.0.669, aux 0.0.8). Log: ~/navi48-native/after-boot.log
#   1. card readings before
#   2. GFX clock gating ON (n48nub smu 1000), readings 12 s and 24 s later
#   3. framebuffer step 1: publish the boot console (n48nub fbpublish), show what took the screen
# A reboot undoes both 2 and 3.
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
N="$REPO/tools/native/n48nub/n48nub"
LOG="$HOME/navi48-native/after-boot.log"
exec > >(tee "$LOG") 2>&1
say() { print -r -- "== $*"; }
rd() { sleep 12; ioreg -l -w0 -c Navi48Bringup | grep -E '"N33,(Temps|Fan|Clocks|ClockGating)"'; }
clang -O1 "$REPO/tools/native/n48nub/n48nub.c" -framework IOKit -o "$N" || exit 1
sudo -v
say "kexts: $(kmutil showloaded --list-only 2>/dev/null | grep -oE 'com.navi48.(bringup|accelprobe) \([0-9.]+\)' | tr '\n' ' ')"
say "1. before"; rd
say "2. GFX clock gating ON"; sudo "$N" smu 1000; rd; rd
say "3. framebuffer step 1 (screen may flicker)"; sudo "$N" fbpublish; sleep 5
ioreg -lw0 | grep -E '"IOClass" = ".*(Framebuffer|NDRV)"' | head
ioreg -lw0 -c Navi48Framebuffer | grep -E '"IOFBCurrentPixelCount"|"IOFBWidth"|"IOFBHeight"|"IOClass"' | head
say "done: $LOG"
