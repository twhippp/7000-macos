#!/usr/bin/env python3
"""Carve one kext out of a macOS kernel collection (MH_FILESET) as a standalone Mach-O.

    carve_kc.py <collection.kc> <bundle-id> <out-file>
    carve_kc.py --list <collection.kc>

The README says this step "was done by hand, and no tool for it ships here". This is that tool.
What the rest of ioaccel-layout needs from a carved kext (kcmod.Image): its LC_SEGMENT_64s with the
SAME vmaddrs it has inside the collection, the segment bytes at those addresses (raw: vtable slots
stay chained-fixup words, kcmod decodes them itself), and a symbol table `nm` can read, also at the
collection's addresses. So the carve keeps every vmaddr, moves the file data, and copies the kext's
slice of the collection's symbol and string tables. Load commands whose data lives in the shared
__LINKEDIT of the collection (function starts, data-in-code, indirect symbols, ...) are emptied:
nothing in the pipeline reads them, and pointing them at missing bytes would upset `nm`.

After writing, the carve re-reads its own output and checks every segment's bytes against the
collection at the same addresses (and that the symbol table survived): it prints OK or exits 1.
Reads your own collection, writes Apple-derived output: keep it on your machine (see README.md).
"""
import struct, sys

MH_MAGIC_64 = 0xfeedfacf
MH_FILESET = 0xc
LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x2
LC_DYSYMTAB = 0xb
LC_FILESET_ENTRY = 0x80000035
# linkedit_data_command (cmd, cmdsize, dataoff, datasize): their data is in the collection's shared __LINKEDIT
LINKEDIT_DATA_CMDS = {0x1d, 0x1e, 0x26, 0x29, 0x2b, 0x2e, 0x80000033, 0x80000034}
PAGE = 0x1000


def align(x, a=PAGE):
    return (x + a - 1) & ~(a - 1)


def fileset_entries(kc):
    magic, _, _, ftype, ncmds, _, _, _ = struct.unpack_from('<IiiIIIII', kc, 0)
    if magic != MH_MAGIC_64:
        sys.exit('not a 64-bit Mach-O')
    if ftype != MH_FILESET:
        sys.exit('not a kernel collection (MH_FILESET), filetype %#x' % ftype)
    off = 32
    out = []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', kc, off)
        if cmd == LC_FILESET_ENTRY:
            vmaddr, fileoff, idoff = struct.unpack_from('<QQI', kc, off + 8)
            name = kc[off + idoff: off + size].split(b'\0')[0].decode()
            out.append((name, vmaddr, fileoff))
        off += size
    return out


def carve(kc, fileoff):
    magic, cpu, sub, ftype, ncmds, sizeofcmds, flags, _ = struct.unpack_from('<IiiIIIII', kc, fileoff)
    if magic != MH_MAGIC_64:
        sys.exit('no Mach-O header at the entry offset %#x' % fileoff)
    cmds = bytearray(kc[fileoff + 32: fileoff + 32 + sizeofcmds])

    # Pass 1: the segments, in load-command order.
    segs = []          # (cmd offset in cmds, name, vmaddr, vmsize, old fileoff, filesize, nsects)
    symtab = None
    off = 0
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', cmds, off)
        if cmd == LC_SEGMENT_64:
            name = cmds[off + 8: off + 24].split(b'\0')[0].decode()
            vm, vs, fo, fs = struct.unpack_from('<QQQQ', cmds, off + 24)
            nsects = struct.unpack_from('<I', cmds, off + 64)[0]
            segs.append([off, name, vm, vs, fo, fs, nsects])
        elif cmd == LC_SYMTAB:
            symtab = (off,) + struct.unpack_from('<IIII', cmds, off + 8)   # symoff, nsyms, stroff, strsize
        off += size
    if not segs:
        sys.exit('the entry has no segments')
    if symtab is None:
        sys.exit('the entry has no LC_SYMTAB: nothing for nm to read')

    # The header must open the first file-backed segment (normally __TEXT): that segment goes to file offset 0.
    first = min((s for s in segs if s[5] > 0), key=lambda s: s[4])
    if first[4] != fileoff:
        sys.exit('the header is not at the start of segment %s (fileoff %#x, header %#x)' % (first[1], first[4], fileoff))

    out = bytearray()
    newoff = {}
    for s in sorted((s for s in segs if s[5] > 0 and s[1] != '__LINKEDIT'), key=lambda s: s[4]):
        pos = 0 if s is first else align(len(out))
        out.extend(b'\0' * (pos - len(out)))
        out.extend(kc[s[4]: s[4] + s[5]])
        newoff[s[1]] = pos

    # The symbol table slice, then the string table, as the new __LINKEDIT.
    _, symoff, nsyms, stroff, strsize = symtab
    le_start = align(len(out))
    out.extend(b'\0' * (le_start - len(out)))
    new_symoff = len(out)
    out.extend(kc[symoff: symoff + nsyms * 16])
    new_stroff = len(out)
    out.extend(kc[stroff: stroff + strsize])
    le_size = len(out) - le_start

    # Pass 2: rewrite the load commands.
    off = 0
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', cmds, off)
        if cmd == LC_SEGMENT_64:
            name = cmds[off + 8: off + 24].split(b'\0')[0].decode()
            vm, vs, fo, fs = struct.unpack_from('<QQQQ', cmds, off + 24)
            nsects = struct.unpack_from('<I', cmds, off + 64)[0]
            if name == '__LINKEDIT':
                struct.pack_into('<QQ', cmds, off + 40, le_start, le_size)
            elif fs > 0:
                nf = newoff[name]
                struct.pack_into('<Q', cmds, off + 40, nf)
                for k in range(nsects):
                    so = off + 72 + 80 * k
                    sec_off = struct.unpack_from('<I', cmds, so + 48)[0]
                    if sec_off:
                        struct.pack_into('<I', cmds, so + 48, sec_off - fo + nf)
                    struct.pack_into('<II', cmds, so + 56, 0, 0)            # reloff, nreloc: none carried over
            else:
                struct.pack_into('<Q', cmds, off + 40, 0)
        elif cmd == LC_SYMTAB:
            struct.pack_into('<IIII', cmds, off + 8, new_symoff, nsyms, new_stroff, strsize)
        elif cmd == LC_DYSYMTAB:
            # keep the local / external / undefined symbol ranges (indices into the symtab, still valid);
            # zero every table that lives in the collection's shared __LINKEDIT
            vals = list(struct.unpack_from('<18I', cmds, off + 8))
            for i in range(6, 18):
                vals[i] = 0
            struct.pack_into('<18I', cmds, off + 8, *vals)
        elif cmd in LINKEDIT_DATA_CMDS:
            struct.pack_into('<II', cmds, off + 8, 0, 0)
        off += size

    # The header: an ordinary kext image (MH_KEXT_BUNDLE), same cpu, the rewritten commands.
    hdr = struct.pack('<IiiIIIII', MH_MAGIC_64, cpu, sub, 0xb, ncmds, sizeofcmds, flags & ~0x80000000, 0)
    out[0: 32 + sizeofcmds] = hdr + bytes(cmds)
    return bytes(out), segs


def verify(path, kc, segs):
    """Re-read the carve: every segment's bytes must equal the collection's at the same vmaddrs."""
    f = open(path, 'rb').read()
    ncmds = struct.unpack_from('<I', f, 16)[0]
    off = 32
    got = {}
    nsyms = 0
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', f, off)
        if cmd == LC_SEGMENT_64:
            name = f[off + 8: off + 24].split(b'\0')[0].decode()
            got[name] = struct.unpack_from('<QQQQ', f, off + 24)
        elif cmd == LC_SYMTAB:
            nsyms = struct.unpack_from('<I', f, off + 12)[0]
        off += size
    bad = 0
    for _, name, vm, vs, fo, fs, _ in segs:
        if name == '__LINKEDIT' or fs == 0:
            continue
        g = got.get(name)
        if not g or g[0] != vm or g[3] != fs:
            print('  segment %s: address or size changed' % name); bad += 1; continue
        # skip the header + load commands (rewritten on purpose) in the first segment
        skip = 32 + struct.unpack_from('<I', f, 20)[0] if g[2] == 0 else 0
        if f[g[2] + skip: g[2] + fs] != kc[fo + skip: fo + fs]:
            print('  segment %s: bytes differ from the collection' % name); bad += 1
    if nsyms == 0:
        print('  no symbols'); bad += 1
    return bad == 0, nsyms


def main():
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == '--list':
        for name, vm, fo in fileset_entries(open(a[1], 'rb').read()):
            print('%#018x  %#012x  %s' % (vm, fo, name))
        return
    if len(a) != 3:
        sys.exit(__doc__)
    kcpath, bid, outpath = a
    kc = open(kcpath, 'rb').read()
    hits = [e for e in fileset_entries(kc) if e[0] == bid]
    if not hits:
        sys.exit('%s is not in %s (carve_kc.py --list %s)' % (bid, kcpath, kcpath))
    data, segs = carve(kc, hits[0][2])
    open(outpath, 'wb').write(data)
    ok, nsyms = verify(outpath, kc, segs)
    print('%s: %s -> %s (%d bytes, %d segments, %d symbols): %s' % (
        'OK' if ok else 'FAILED', bid, outpath, len(data), len([s for s in segs if s[5]]), nsyms,
        'every segment matches the collection' if ok else 'see above'))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
