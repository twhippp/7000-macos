#!/bin/zsh
# install-translator-mac.sh - the on-Mac shader auto-translator (upstream's tools/native/autotranslate, LaunchDaemon com.navi48.translate).
# WindowServer never translates shaders itself: a shader missing from the bundle's spvcache draws the magenta FALLBACK and its AIR is
# dumped to /private/tmp/n48m/<uid>/. This daemon (user nobody, sandboxed) translates each dump with metal2vulkan into
# /private/var/tmp/n48m-spv/, and the bundle HOT-SWAPS the real pipeline in while WindowServer runs. No reboot needed.
# Needs: ~/navi48-native/metal2vulkan (made by build-metal-mac.sh). Run as yourself; asks for your password once.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
A="$REPO/tools/native/autotranslate"
M2V="$HOME/navi48-native/metal2vulkan"
D=/usr/local/navi48
[ -d "$M2V" ] || { echo "run tools/native/build-metal-mac.sh first ($M2V missing)"; exit 1; }
[ -x "$HOME/.cargo/bin/cargo" ] && export PATH="$HOME/.cargo/bin:$PATH"

print "== 1/3 the metal2vulkan command-line translator"
(cd "$M2V" && cargo build --release --features serde --target x86_64-apple-darwin --bin metal2vulkan 2>&1 | tail -3)
BIN="$M2V/target/x86_64-apple-darwin/release/metal2vulkan"
[ -x "$BIN" ] || { echo "metal2vulkan binary not built: $BIN"; exit 1; }
ls -l "$BIN"

print "== 2/3 install into $D"
sudo -v
sudo mkdir -p "$D"
for f in pc-translate.sh pc-translate.py n48-llvm-dis n48-spirv-val-stub overrides.txt n48-translate.sb; do sudo cp "$A/$f" "$D/$f"; done
sudo cp "$BIN" "$D/metal2vulkan"
sudo chown -R root:wheel "$D"; sudo chmod 755 "$D" "$D/pc-translate.sh" "$D/n48-llvm-dis" "$D/n48-spirv-val-stub" "$D/metal2vulkan"; sudo chmod 644 "$D/pc-translate.py" "$D/overrides.txt" "$D/n48-translate.sb"
sudo mkdir -p /private/var/tmp/n48m-spv /private/var/tmp/n48m-translate-state
sudo chown nobody:nobody /private/var/tmp/n48m-spv /private/var/tmp/n48m-translate-state

print "== 3/3 the LaunchDaemon"
sudo cp "$A/com.navi48.translate.plist" /Library/LaunchDaemons/com.navi48.translate.plist
sudo chown root:wheel /Library/LaunchDaemons/com.navi48.translate.plist; sudo chmod 644 /Library/LaunchDaemons/com.navi48.translate.plist
sudo launchctl bootout system/com.navi48.translate 2>/dev/null || true
sudo launchctl bootstrap system /Library/LaunchDaemons/com.navi48.translate.plist
sleep 2
sudo launchctl print system/com.navi48.translate 2>/dev/null | grep -E "state|pid" | head -3
sudo tail -3 /private/var/tmp/n48m-translate-state/translate.log 2>/dev/null || true
print "== TRANSLATOR OK. Now run tools/native/n33-desktop.sh; translations land in /private/var/tmp/n48m-spv (log: sudo tail -f /private/var/tmp/n48m-translate-state/translate.log)"
