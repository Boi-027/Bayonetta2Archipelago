"""Find data words pointing at given addresses (raw u64 or RELATIVE relocation addends): python xref.py main.elf addr..."""
import struct, sys

BASE = 0x7100000000
data = open(sys.argv[1], 'rb').read()
targets = {int(a, 16) for a in sys.argv[2:]}
phoff, = struct.unpack_from('<Q', data, 0x20)
phentsize, phnum = struct.unpack_from('<HH', data, 0x36)
segs, dyn = [], None
for i in range(phnum):
    t, _, off, va, _, fsz, _, _ = struct.unpack_from('<IIQQQQQQ', data, phoff + i * phentsize)
    if t == 1: segs.append((off, va, fsz))
    if t == 2: dyn = (off, fsz)

def va2off(v):
    v -= BASE if v >= BASE else 0
    for off, va, fsz in segs:
        if va <= v < va + fsz: return off + v - va

hits = []
for off, va, fsz in segs:          # raw absolute pointers
    for o in range(off, off + fsz - 7, 8):
        q, = struct.unpack_from('<Q', data, o)
        if q in targets or q + BASE in targets:
            hits.append(('raw', BASE + va + o - off, q))
if dyn:                            # DT_RELA / DT_RELASZ
    tags = {}
    for o in range(dyn[0], dyn[0] + dyn[1], 16):
        tag, val = struct.unpack_from('<QQ', data, o)
        tags.setdefault(tag, val)
    if 7 in tags:
        r = va2off(tags[7])
        for o in range(r, r + tags[8], 24):
            where, info, add = struct.unpack_from('<QQq', data, o)
            if add in targets or add + BASE in targets:
                hits.append(('rela', where + BASE, add))
for kind, where, val in sorted(hits, key=lambda h: h[1]):
    print(kind, hex(where), '->', hex(val if val >= BASE else val + BASE))
print(len(hits), 'hits')
