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
# Needs: Xcode Command Line Tools (xcode-select --install) and
#        python3 -m pip install --user meson ninja pkgconf mako pyyaml packaging   (or Homebrew's)
set -euo pipefail

MESA_SHA=f5cb8ee032adabef599ae892ec4e42d796b84da9          # VERSION 26.3.0-devel, see mesa-patches/README.txt
MESA_URL=${MESA_URL:-https://gitlab.freedesktop.org/mesa/mesa.git}
MESA_DIR=${MESA_DIR:-$HOME/navi48-native/mesa-mac}
BUILD=build-x86_64
HERE="$(cd "$(dirname "$0")/.." && pwd)"
PATCHES="$HERE/mesa-patches"
LOG="$HOME/navi48-native/build-mesa.log"
TOOLS="$HOME/navi48-native/tools"
PYVER=3.12
mkdir -p "$(dirname "$MESA_DIR")"

say() { printf '\n== %s\n' "$*"; }
die() { printf '\nbuild-mesa-mac: %s\n' "$*" >&2; exit 1; }

mode=${1:-build}

# Mesa needs Python >= 3.10; macOS's Command Line Tools ship 3.9. Without Homebrew or an admin
# password, uv fetches a standalone Python into the user's home and a venv there holds the build
# tools (meson, ninja, pkgconf, mako, pyyaml, packaging, cmake).
ensure_python() {
	local venv="$TOOLS/venv"
	PATH="$venv/bin:$PATH"; export PATH
	if [ -x "$venv/bin/python3" ] && "$venv/bin/python3" -c 'import sys, mako, yaml, packaging; sys.exit(sys.version_info < (3, 10))' 2>/dev/null &&
		[ -x "$venv/bin/meson" ]; then return; fi
	if python3 -c 'import sys; sys.exit(sys.version_info < (3, 10))' 2>/dev/null &&
		python3 -c 'import mako, yaml, packaging' 2>/dev/null && command -v meson >/dev/null; then return; fi
	say "setting up Python $PYVER and the build tools in $venv (one time)"
	# `python3 -m site --user-base` exits non-zero when user site-packages are off (inside any venv),
	# which set -e would turn into a silent exit; hence || true.
	local ub; ub="$(python3 -m site --user-base 2>/dev/null || true)/bin"; PATH="$PATH:$ub"
	command -v uv >/dev/null || python3 -m pip install --user -q uv || die "pip install uv failed"
	uv venv -q --python "$PYVER" "$venv" || die "uv could not set up Python $PYVER"
	uv pip install -q --python "$venv/bin/python3" meson ninja pkgconf mako pyyaml packaging cmake ||
		die "installing the build tools into $venv failed"
	"$venv/bin/python3" --version
}

check_tools() {
	ensure_python
	say "checking tools"
	local miss=()
	for t in git clang meson ninja python3; do command -v "$t" >/dev/null || miss+=("$t"); done
	[ ${#miss[@]} -eq 0 ] || die "missing: ${miss[*]}  (xcode-select --install; pip3 install --user meson ninja pkgconf)"
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

# RADV's BVH shaders need glslangValidator at build time. Without Homebrew there is none on macOS,
# so build it once from source (cmake from pip, no SPIRV-Tools) into $TOOLS.
GLSLANG_TAG=16.4.0
ensure_glslang() {
	PATH="$TOOLS/bin:$PATH"; export PATH
	if command -v glslangValidator >/dev/null; then return; fi
	say "building glslang $GLSLANG_TAG into $TOOLS (one time)"
	command -v cmake >/dev/null || python3 -m pip install --user -q cmake || die "pip install cmake failed"
	local src="$TOOLS/src/glslang"
	[ -d "$src/.git" ] || git clone -q --depth 1 -b "$GLSLANG_TAG" https://github.com/KhronosGroup/glslang.git "$src"
	cmake -S "$src" -B "$src/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$TOOLS" \
		-DENABLE_OPT=OFF -DBUILD_EXTERNAL=OFF -DGLSLANG_TESTS=OFF -DBUILD_SHARED_LIBS=OFF >/dev/null
	cmake --build "$src/build" --target install -j "$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >/dev/null ||
		die "glslang build failed (cmake --build $src/build)"
	command -v glslangValidator >/dev/null || die "glslangValidator still missing after the build"
	glslangValidator --version | head -1
}

configure() {
	local cross=()
	if [ "$(uname -m)" != x86_64 ]; then cross=(--cross-file "$MESA_DIR/cross-x86_64.ini"); fi
	ensure_glslang
	if [ -f "$MESA_DIR/$BUILD/build.ninja" ]; then return; fi
	rm -rf "$MESA_DIR/$BUILD"   # a failed earlier setup leaves a half-made build dir
	local note=""; [ ${#cross[@]} -eq 0 ] || note=", cross to x86_64"
	say "configuring ($(uname -m) host$note)"
	# RADV only, ACO only (no LLVM), no GL/EGL/GBM/X11/Wayland: the only consumer is the Metal
	# bundle and test tools, which load the dylib directly.
	# ${cross[@]+...}: macOS bash 3.2 treats an empty array as unbound under set -u.
	meson setup "$MESA_DIR/$BUILD" "$MESA_DIR" ${cross[@]+"${cross[@]}"} \
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

	# radvprobe: loads the dylib directly (no Vulkan loader or SDK needed) and lists the device.
	local probe="$MESA_DIR/$BUILD/radvprobe"
	clang -std=gnu11 -O1 -I "$MESA_DIR/include" "$HERE/tools/native/radvprobe/radvprobe.c" -o "$probe" ||
		die "radvprobe did not compile"
	say "mock check: no GPU access, the in-process mock kext answers as the RX 7600"
	if RADV_DARWIN_MOCK=1 RADV_DARWIN_MOCK_N33=1 "$probe" "$lib"; then
		echo "MOCK CHECK OK"
	else
		echo "MOCK CHECK FAILED (exit $?)"
	fi
	echo
	echo "Real GPU (kext loaded):  $probe $lib          list only"
	echo "                         $probe $lib --fill   first submit: fill a buffer on the GPU"
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
