#!/bin/bash
# Build the Metal side on THIS Mac: Navi48Accel.kext (aux kext), libn48xlate.dylib (the shader translator,
# metal2vulkan + third-party/metal2vulkan-n48) and Navi48Metal.bundle (with RADV and the translator inside).
#
#   tools/native/build-metal-mac.sh
#
# Needs: Command Line Tools, the layout map (tools/ioaccel-layout/make-layout.sh), the Mesa build
# (tools/build-mesa-mac.sh). Installs Rust into ~/.cargo the first time (no sudo).
# Outputs: tools/native/navi48accel/build/Navi48Accel.kext, tools/native/navi48metal/build/Navi48Metal.bundle.tar
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
# the layout tools need these (same values make-layout.sh used)
export C64_WORK="$REPO/tools/ioaccel-layout/c64-work"
export C64_LAYOUT_OUT="$REPO/tools/ioaccel-layout/generated"
export C64_MKSDK="$REPO/src/RDNA4FB/MacKernelSDK"
NN="$HOME/navi48-native"
M2V="$NN/metal2vulkan"
M2V_SHA=43c46ac8a24adf1a6e872b8a52c706ec9614fad0      # third-party/metal2vulkan-n48/NOTICE
[ -d "$NN/tools/venv/bin" ] && export PATH="$NN/tools/venv/bin:$PATH"   # python >= 3.10 for the gate scripts
say() { printf '\n== %s\n' "$*"; }

say "checks"
[ -f "$REPO/tools/ioaccel-layout/generated/model.json" ] || { echo "no layout map: run tools/ioaccel-layout/make-layout.sh"; exit 1; }
[ -d "$REPO/src/RDNA4FB/MacKernelSDK/Headers" ] || { echo "no MacKernelSDK: run tools/ioaccel-layout/make-layout.sh"; exit 1; }
RADV="$NN/mesa-mac/build-x86_64/src/amd/vulkan/libvulkan_radeon.dylib"
[ -f "$RADV" ] || { echo "no RADV build: run tools/build-mesa-mac.sh"; exit 1; }
echo "python $(python3 --version 2>&1), RADV $(ls -l "$RADV" | awk '{print $5}') bytes"

say "1/3 the aux kext (Navi48Accel)"
make -C "$REPO/tools/native/navi48accel" 2>&1 | tail -15
ls -d "$REPO/tools/native/navi48accel/build/Navi48Accel.kext"

say "2/3 the shader translator (libn48xlate)"
if [ ! -d "$M2V/.git" ]; then
	git clone -q https://github.com/steelbrain/metal2vulkan "$M2V"
fi
git -C "$M2V" fetch -q origin "$M2V_SHA" 2>/dev/null || true
git -C "$M2V" checkout -q "$M2V_SHA"
PATCH="$REPO/third-party/metal2vulkan-n48/patches/0001-metal2vulkan-changes-for-n48xlate.patch"
if git -C "$M2V" apply --check "$PATCH" 2>/dev/null; then git -C "$M2V" apply "$PATCH"; echo "patch applied"
elif git -C "$M2V" apply --reverse --check "$PATCH" 2>/dev/null; then echo "patch already applied"
else echo "the n48xlate patch neither applies nor is applied: $M2V has other changes"; exit 1; fi
rm -rf "$M2V/n48xlate"; cp -R "$REPO/third-party/metal2vulkan-n48/n48xlate" "$M2V/n48xlate"
if ! command -v cargo >/dev/null && [ ! -x "$HOME/.cargo/bin/cargo" ]; then
	echo "installing Rust (rustup, into ~/.cargo, no sudo)"
	curl -sSf https://sh.rustup.rs | sh -s -- -y -q --profile minimal
fi
export PATH="$HOME/.cargo/bin:$PATH"
cargo --version
( cd "$M2V" && MACOSX_DEPLOYMENT_TARGET=12.0 cargo build --release -p n48xlate --target x86_64-apple-darwin 2>&1 | tail -5 )
XL="$M2V/target/x86_64-apple-darwin/release/libn48xlate.dylib"
ls -l "$XL"

say "3/3 the Metal bundle (Navi48Metal)"
N48_XLATE_LIB="$XL" N48_RADV_LIB="$RADV" N48_VK_INC="-I$NN/mesa-mac/include" zsh "$REPO/tools/native/navi48metal/build.sh" 2>&1 | tail -8
ls -l "$REPO/tools/native/navi48metal/build/Navi48Metal.bundle.tar"
say "BUILD OK"
