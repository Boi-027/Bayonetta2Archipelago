"""PlatinumGames DAT archive: list / extract. python datls.py <file.dat> [outdir]"""
import os, struct, sys

d = open(sys.argv[1], 'rb').read()
assert d[:4] == b'DAT\0', d[:4]
n, off_tab, ext_tab, name_tab, size_tab = struct.unpack_from('<IIIII', d, 4)
name_len, = struct.unpack_from('<I', d, name_tab)
for i in range(n):
    off, = struct.unpack_from('<I', d, off_tab + 4 * i)
    size, = struct.unpack_from('<I', d, size_tab + 4 * i)
    name = d[name_tab + 4 + i * name_len:name_tab + 4 + (i + 1) * name_len].split(b'\0')[0].decode()
    print('%-32s off %8x size %8x  %s' % (name, off, size, d[off:off + 4]))
    if len(sys.argv) > 2:
        os.makedirs(sys.argv[2], exist_ok=True)
        open(os.path.join(sys.argv[2], name), 'wb').write(d[off:off + size])
