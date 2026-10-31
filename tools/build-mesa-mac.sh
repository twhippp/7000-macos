#!/bin/bash
# Build RADV (libvulkan_radeon.dylib) for macOS from the pinned Mesa commit plus mesa-patches/*.patch.
#
#   tools/build-mesa-mac.sh            fetch (first run), patch, configure, build
#   tools/build-mesa-mac.sh --rebuild  re-apply the patches onto a clean tree, then build
#   tools/build-mesa-mac.sh --test     only run the mock smoke test on an existing build
#
# Works on an Intel Mac / Hackintosh (native x86_64) and on Apple Silicon (cross, via the patch's
# cross-x86_64.ini). Result: $MESA_DIR/build-x86_64/src/amd/vulkan/libvulkan_radeon.dylib, the path
# tools/native/navi48metal/build.sh reads by default.
#
# Needs: Xcode Command Line Tools, Homebrew: brew install meson ninja pkg-config python@3
#        and Python modules: python3 -m pip install --user mako pyyaml packaging
set -euo pipefail

MESA_SHA=f5cb8ee032adabef599ae892ec4e42d796b84da9          # VERSION 26.3.0-devel, see mesa-patches/README.txt
MESA_URL=${MESA_URL:-https://gitlab.freedesktop.org/mesa/mesa.git}
MESA_DIR=${MESA_DIR:-$HOME/navi48-native/mesa-mac}
BUILD=build-x86_64
HERE="$(cd "$(dirname "$0")/.." && pwd)"
PATCHES="$HERE/mesa-patches"
LOG="$HOME/navi48-native/build-mesa.log"
mkdir -p "$(dirname "$MESA_DIR")"

say() { printf '\n== %s\n' "$*"; }
die() { printf '\nbuild-mesa-mac: %s\n' "$*" >&2; exit 1; }

mode=${1:-build}

check_tools() {
	say "checking tools"
	local miss=()
	for t in git clang meson ninja pkg-config python3; do command -v "$t" >/dev/null || miss+=("$t"); done
	[ ${#miss[@]} -eq 0 ] || die "missing: ${miss[*]}  (xcode-select --install; brew install meson ninja pkg-config)"
	python3 -c 'import mako, yaml, packaging' 2>/dev/null ||
		die "python modules missing: python3 -m pip install --user mako pyyaml packaging"
	echo "meson $(meson --version), ninja $(ninja --version), $(clang --version | head -1)"
}

fetch() {
	if [ -d "$MESA_DIR/.git" ]; then return; fi
	say "fetching Mesa $MESA_SHA into $MESA_DIR"
	git init -q "$MESA_DIR"
	git -C "$MESA_DIR" remote add origin "$MESA_URL"
	# One commit, no history; fall back to a blobless clone if the server refuses a SHA fetch.
	if ! git -C "$MESA_DIR" fetch --depth 1 origin "$MESA_SHA"; then
		echo "shallow SHA fetch refused; doing a blobless fetch of main (slower)"
		git -C "$MESA_DIR" fetch --filter=blob:none origin main
	fi
	git -C "$MESA_DIR" -c advice.detachedHead=false checkout -q "$MESA_SHA"
}

patch_tree() {
	say "applying $(ls "$PATCHES"/*.patch | wc -l | tr -d ' ') patches"
	git -C "$MESA_DIR" am --abort >/dev/null 2>&1 || true
	git -C "$MESA_DIR" reset -q --hard "$MESA_SHA"
	git -C "$MESA_DIR" clean -qfdx -e "$BUILD"
	git -C "$MESA_DIR" -c user.name=build -c user.email=build@localhost am -q --whitespace=nowarn "$PATCHES"/*.patch ||
		die "a patch did not apply (see git -C $MESA_DIR am --show-current-patch)"
	git -C "$MESA_DIR" log --oneline "$MESA_SHA"..HEAD
}

configure() {
	local cross=()
	if [ "$(uname -m)" != x86_64 ]; then cross=(--cross-file "$MESA_DIR/cross-x86_64.ini"); fi
	if [ -f "$MESA_DIR/$BUILD/build.ninja" ]; then return; fi
	say "configuring ($(uname -m) host${cross:+, cross to x86_64})"
	# RADV only, ACO only (no LLVM), no GL/EGL/GBM/X11/Wayland: the only consumer is the Metal
	# bundle and test tools, which load the dylib directly.
	meson setup "$MESA_DIR/$BUILD" "$MESA_DIR" "${cross[@]}" \
		-Dbuildtype=debugoptimized -Db_ndebug=false \
		-Dvulkan-drivers=amd -Dgallium-drivers= -Dplatforms= \
		-Dopengl=false -Dgles1=disabled -Dgles2=disabled -Degl=disabled -Dglx=disabled -Dgbm=disabled \
		-Dllvm=disabled -Dvideo-codecs= -Dvulkan-layers= -Dtools= \
		-Dzstd=disabled -Dlibunwind=disabled -Dvalgrind=disabled -Dxmlconfig=disabled
}

build() {
	say "building (log: $LOG)"
	ninja -C "$MESA_DIR/$BUILD" src/amd/vulkan/libvulkan_radeon.dylib 2>&1 | tee "$LOG" | grep -E "error|FAILED|warning: .*darwin" || true
	[ "${PIPESTATUS[0]}" -eq 0 ] || die "ninja failed, see $LOG"
}

smoke() {
	local lib="$MESA_DIR/$BUILD/src/amd/vulkan/libvulkan_radeon.dylib"
	[ -f "$lib" ] || die "no $lib"
	say "result"
	ls -l "$lib"; file "$lib"
	local undef; undef=$(nm -u "$lib" | grep -c . || true)
	echo "undefined symbols (dynamic_lookup by design): $undef"
	nm -gU "$lib" | grep -q vk_icdGetInstanceProcAddr && echo "exports vk_icdGetInstanceProcAddr: yes"
	# The ICD json the Vulkan loader (or vulkaninfo) needs to find the dylib.
	local icd="$MESA_DIR/$BUILD/radeon_icd.darwin.json"
	printf '{ "file_format_version": "1.0.1", "ICD": { "library_path": "%s", "api_version": "1.4.0" } }\n' "$lib" >"$icd"
	echo "ICD json: $icd   (VK_DRIVER_FILES=$icd vulkaninfo --summary)"
	echo
	echo "Safe check with no GPU access (mock kext, answers as the RX 7600):"
	echo "  RADV_DARWIN_MOCK=1 RADV_DARWIN_MOCK_N33=1 VK_DRIVER_FILES=$icd vulkaninfo --summary"
}

check_tools
case "$mode" in
	--test) smoke ;;
	--rebuild) fetch; patch_tree; configure; build; smoke ;;
	build|*)
		fetch
		if ! git -C "$MESA_DIR" log --format=%s "$MESA_SHA"..HEAD 2>/dev/null | grep -q "canned device from the kext"; then patch_tree; fi
		configure; build; smoke ;;
esac
