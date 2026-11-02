#!/bin/zsh
# metal-check-mac.sh - after install-metal-mac.sh + approval + reboot: publish the Metal nub, check the aux kext loaded, list Metal devices, draw a triangle.
# Log: ~/navi48-native/metal-check.log
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
LOG="$HOME/navi48-native/metal-check.log"
exec > >(tee "$LOG") 2>&1
say() { print -r -- "== $*"; }
sudo -v
say "boot-args: $(sysctl -n kern.bootargs)"
say "aux kext in the aux collection?"
kmutil inspect --aux-only 2>/dev/null | grep -i navi48 || echo "  not listed (not approved yet?)"
say "publish the Metal nub"
sudo "$REPO/tools/native/n48nub/n48nub" publish
sleep 3
say "loaded:"
kmutil showloaded --list-only 2>/dev/null | grep -iE "navi48|accelprobe" || echo "  aux kext NOT loaded"
ioreg -l -w0 -c Navi48Accelerator 2>/dev/null | grep -E "MetalPluginName|IOClass|model" | head -5
say "mtlprobe list"
sudo "$REPO/tools/native/mtlprobe/mtlprobe" list
say "mtlprobe triangle"
sudo "$REPO/tools/native/mtlprobe/mtlprobe" triangle "$HOME/navi48-native/metal-tri.png"; echo "triangle rc=$?"
say "kernel log (last 40 Navi48 lines)"
log show --last 3m --predicate 'sender CONTAINS "Navi48" OR sender CONTAINS "accelprobe"' 2>/dev/null | tail -40
say "done: $LOG"
