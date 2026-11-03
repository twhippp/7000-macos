#!/bin/zsh
# install-metal-mac.sh - put the Metal side in place on the Navi 33 bench (run as yourself; it asks for your password once).
#   1. builds n48nub + mtlprobe
#   2. adds navi48-metal=1 to OpenCore's boot-args (config.plist backed up next to it)
#   3. copies the aux kext to /Library/Extensions/Navi48Accel-0.0.7.kext and asks macOS to load it (the first answer is "not approved": expected)
#   4. installs the Metal bundle into /Library/GPUBundles
# Then: approve the kext in System Settings > Privacy & Security, reboot once, run tools/native/metal-check-mac.sh.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
AUX_SRC="$REPO/tools/native/navi48accel/build/Navi48Accel.kext"
TAR="$REPO/tools/native/navi48metal/build/Navi48Metal.bundle.tar"
AUX_DST=/Library/Extensions/Navi48Accel-0.0.7.kext
OC=/Volumes/OPENCORE/EFI/OC/config.plist
BA=':NVRAM:Add:7C436110-AB2A-4BBB-A880-FE41995C9F82:boot-args'
say() { print -r -- "== $*"; }

[ -d "$AUX_SRC" ] && [ -f "$TAR" ] || { echo "run tools/native/build-metal-mac.sh first"; exit 1; }

say "1/4 tools"
clang -O1 "$REPO/tools/native/n48nub/n48nub.c" -framework IOKit -o "$REPO/tools/native/n48nub/n48nub"
clang -fobjc-arc -O1 -arch x86_64 -mmacosx-version-min=13.0 "$REPO/tools/native/mtlprobe/mtlprobe.m" \
  -framework Metal -framework Foundation -framework IOKit -framework CoreGraphics -framework ImageIO -framework IOSurface \
  -o "$REPO/tools/native/mtlprobe/mtlprobe"
echo "built n48nub and mtlprobe"

BUNDLE_ONLY=0; [ "${1:-}" = "--bundle" ] && BUNDLE_ONLY=1   # --bundle: only reinstall the Metal bundle (no reboot needed)
# --kext <Navi48Bringup.kext>: put a new bring-up kext on the OPENCORE volume (old one kept as Navi48Bringup.kext.<version>) and add
# navi48-metal-boot=1 (the kext publishes the Metal device by itself at boot). Needs a reboot.
# --desktop-bootargs: the boot-args the GPU desktop needs (upstream's ...-metal-disp-amfi variant): navi48-metal-disp=1 (display pipe glue),
# navi48-metal-ws=1 (WindowServer may open the GPU), amfi=0x80 (WindowServer may load the ad-hoc signed Metal bundle). navi48-metal-boot is
# REMOVED: the desktop script publishes the Metal device after the framebuffer, so WindowServer finds the GPU only when it is armed.
# --plain-bootargs: back to the plain Metal boot-args. Both back the config up first. Need a reboot.
if [ "${1:-}" = "--desktop-bootargs" ] || [ "${1:-}" = "--plain-bootargs" ]; then
  sudo -v
  [ -f "$OC" ] || sudo diskutil mount OPENCORE >/dev/null
  cur=$(/usr/libexec/PlistBuddy -c "Print $BA" "$OC")
  sudo cp "$OC" "$OC.pre-bootargs.$(date +%s).backup"
  new=" $cur "
  for a in navi48-metal-boot=1 navi48-metal-disp=1 navi48-metal-ws=1 amfi=0x80 navi33-fb-boot=1; do new=${new// $a / }; done
  new=$(echo $new)
  [ "$1" = "--desktop-bootargs" ] && new="$new navi48-metal-disp=1 navi48-metal-ws=1 amfi=0x80 navi33-fb-boot=1"
  sudo /usr/libexec/PlistBuddy -c "Set $BA $new" "$OC"
  say "boot-args: $(/usr/libexec/PlistBuddy -c "Print $BA" "$OC")"
  exit 0
fi
if [ "${1:-}" = "--kext" ]; then
  K="${2:?usage: install-metal-mac.sh --kext /path/to/Navi48Bringup.kext}"
  [ -f "$K/Contents/Info.plist" ] || { echo "not a kext: $K"; exit 1; }
  sudo -v
  [ -f "$OC" ] || sudo diskutil mount OPENCORE >/dev/null
  KD=/Volumes/OPENCORE/EFI/OC/Kexts
  old=$(/usr/libexec/PlistBuddy -c "Print :CFBundleVersion" "$KD/Navi48Bringup.kext/Contents/Info.plist" 2>/dev/null || echo old)
  new=$(/usr/libexec/PlistBuddy -c "Print :CFBundleVersion" "$K/Contents/Info.plist")
  [ -d "$KD/Navi48Bringup.kext" ] && sudo mv "$KD/Navi48Bringup.kext" "$HOME/navi48-staging/Navi48Bringup.kext.$old.$(date +%s)"
  sudo cp -R "$K" "$KD/Navi48Bringup.kext"; sudo xattr -cr "$KD/Navi48Bringup.kext"
  say "kext $old -> $new on OPENCORE (old one in ~/navi48-staging)"
  cur=$(/usr/libexec/PlistBuddy -c "Print $BA" "$OC")
  if [[ " $cur " != *" navi48-metal-boot=1 "* ]]; then
    sudo cp "$OC" "$OC.pre-metal-boot.backup"
    sudo /usr/libexec/PlistBuddy -c "Set $BA $cur navi48-metal-boot=1" "$OC"
  fi
  say "boot-args: $(/usr/libexec/PlistBuddy -c "Print $BA" "$OC")"
  say "KEXT OK. Reboot, then run tools/native/metal-check-mac.sh"
  exit 0
fi
sudo -v
if [ $BUNDLE_ONLY = 0 ]; then
say "2/4 boot-arg navi48-metal=1"
[ -f "$OC" ] || sudo diskutil mount OPENCORE >/dev/null
[ -f "$OC" ] || { echo "cannot find $OC"; exit 1; }
cur=$(/usr/libexec/PlistBuddy -c "Print $BA" "$OC")
if [[ " $cur " == *" navi48-metal=1 "* ]]; then echo "already there: $cur"
else
  sudo cp "$OC" "$OC.pre-metal.backup"
  sudo /usr/libexec/PlistBuddy -c "Set $BA $cur navi48-metal=1" "$OC"
  echo "boot-args now: $(/usr/libexec/PlistBuddy -c "Print $BA" "$OC")"
fi

say "3/4 aux kext -> $AUX_DST"
for p in /Library/Extensions/*.kext(N); do
  [ "$p" = "$AUX_DST" ] && continue
  if grep -q com.navi48.accelprobe "$p/Contents/Info.plist" 2>/dev/null; then sudo mv "$p" "$HOME/navi48-staging/$(basename $p).old.$(date +%s)"; echo "moved aside $p"; fi
done
sudo rm -rf "$AUX_DST"
sudo cp -R "$AUX_SRC" "$AUX_DST"
sudo chown -R root:wheel "$AUX_DST"; sudo chmod -R go-w "$AUX_DST"; sudo xattr -cr "$AUX_DST"
kmutil libraries -p "$AUX_DST" -a x86_64 2>&1 | tail -5 || true
echo "asking macOS to load it once ('not approved' is expected the first time):"
sudo kmutil load -p "$AUX_DST" 2>&1 | tail -5 || true

fi
say "4/4 Metal bundle -> /Library/GPUBundles"
U=$(mktemp -d)
tar -xf "$TAR" -C "$U"
sudo mkdir -p /Library/GPUBundles
sudo rm -rf /Library/GPUBundles/Navi48Metal.bundle
sudo cp -R "$U/Navi48Metal.bundle" /Library/GPUBundles/Navi48Metal.bundle
sudo chown -R root:wheel /Library/GPUBundles/Navi48Metal.bundle; sudo xattr -cr /Library/GPUBundles/Navi48Metal.bundle
codesign --verify -v /Library/GPUBundles/Navi48Metal.bundle 2>&1 | tail -2

say "SIP: $(csrutil status 2>&1 | head -1)"
say "INSTALL OK. Next: System Settings > Privacy & Security > allow the 'navi48' software (if asked), reboot, then run tools/native/metal-check-mac.sh"
