"""Dump printable strings from an ELF with their virtual addresses: python elfstrings.py main.elf out.txt [base]"""
import struct, sys, re

path, out = sys.argv[1], sys.argv[2]
base = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x7100000000
data = open(path, 'rb').read()
assert data[:4] == b'\x7fELF', 'not an ELF'
phoff, = struct.unpack_from('<Q', data, 0x20)
phentsize, phnum = struct.unpack_from('<HH', data, 0x36)
segs = []
for i in range(phnum):
    p_type, p_flags, p_offset, p_vaddr, _, p_filesz, _, _ = struct.unpack_from('<IIQQQQQQ', data, phoff + i * phentsize)
    if p_type == 1:
        segs.append((p_offset, p_vaddr, p_filesz))
with open(out, 'w', encoding='utf-8') as f:
    for off, vaddr, size in segs:
        va = vaddr if vaddr >= base else base + vaddr
        for m in re.finditer(rb'[\x20-\x7e]{5,}', data[off:off + size]):
            f.write('%x %s\n' % (va + m.start(), m.group().decode('ascii')))
print('segments', [(hex(v), hex(s)) for _, v, s in segs])
