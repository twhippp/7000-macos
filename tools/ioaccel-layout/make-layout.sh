#!/bin/bash
# Generate the IOAcceleratorFamily2 layout map on THIS Mac (macOS Tahoe 26.6.2 / 25G83 is the tested build):
# copy the two kernel collections, carve the two kexts out of them, run dump_vtables / gen_header / check_layout.
# Output: tools/ioaccel-layout/generated/ (what navi48accel's build reads). Apple-derived: it stays on this Mac.
#
#   tools/ioaccel-layout/make-layout.sh
#
# Needs: Command Line Tools (nm, c++filt, clang++), python3, git (for the MacKernelSDK, cloned once).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
export C64_WORK="$HERE/c64-work"
export C64_LAYOUT_OUT="$HERE/generated"
export C64_MKSDK="$REPO/src/RDNA4FB/MacKernelSDK"
PY=python3
[ -x "$HOME/navi48-native/tools/venv/bin/python3" ] && PY="$HOME/navi48-native/tools/venv/bin/python3"
say() { printf '\n== %s\n' "$*"; }

say "macOS $(sw_vers -productVersion) ($(sw_vers -buildVersion))"
[ "$(sw_vers -buildVersion)" = 25G83 ] || echo "note: upstream tested only 25G83; check_layout's MATCH is what counts"

say "kernel collections"
mkdir -p "$C64_WORK/kc" "$C64_WORK/kexts"
for kc in BootKernelExtensions.kc SystemKernelExtensions.kc; do
	src=""
	for d in /System/Library/KernelCollections /Library/KernelCollections; do [ -f "$d/$kc" ] && { src="$d/$kc"; break; }; done
	[ -n "$src" ] || { echo "can't find $kc under /System/Library/KernelCollections"; exit 1; }
	cp "$src" "$C64_WORK/kc/$kc"
	echo "$kc  <- $src ($(du -h "$src" | cut -f1))"
done

say "carving the kexts"
for bid in com.apple.iokit.IOAcceleratorFamily2 com.apple.driver.AppleParavirtGPU; do
	done_one=0
	for kc in SystemKernelExtensions.kc BootKernelExtensions.kc; do
		if "$PY" "$HERE/carve_kc.py" --list "$C64_WORK/kc/$kc" | grep -q " $bid\$"; then
			"$PY" "$HERE/carve_kc.py" "$C64_WORK/kc/$kc" "$bid" "$C64_WORK/kexts/$bid"
			done_one=1; break
		fi
	done
	[ $done_one = 1 ] || { echo "$bid is in neither kernel collection"; exit 1; }
	echo "  nm sees $(nm -n "$C64_WORK/kexts/$bid" 2>/dev/null | grep -c __ZTV) vtables in $bid"
done

say "MacKernelSDK (for check_layout)"
if [ ! -d "$C64_MKSDK/Headers" ]; then
	git clone -q --depth 1 https://github.com/somestupidgirl/RDNA4FB "$REPO/src/RDNA4FB"
fi
ls -d "$C64_MKSDK/Headers"

say "dump_vtables / gen_header / check_layout"
cd "$HERE"
"$PY" dump_vtables.py
"$PY" gen_header.py
"$PY" check_layout.py 2>&1 | tee "$C64_WORK/check_layout.txt" || true
grep -q MATCH "$C64_WORK/check_layout.txt" && echo "LAYOUT OK: tools/ioaccel-layout/generated is ready" || { echo "LAYOUT MISMATCH: see above"; exit 1; }
