"""Story chests give 1 Halo instead of their own contents (Archipelago gives the chest's item instead).

    python chests_mod.py <B2recomp folder> <mod folder>

Every story phase archive (ph*/p*.dat) with a <phase>_TreasureChest.bxm gets each real chest's Id and Num pointed at
the string "1" already in that BXM (Id 1 = Halos, Num = how many: 1000 / 2000 / 3000 in vanilla), so the file keeps
its size and is patched in place. The content-less type 4-8 props (Id 0) and the Muspelheim reward chests (pa11) are
left alone, and so are the type-9 items lying in the open (a
Halo there is picked up as the stage loads). Output: <mod>/romfs/<same path>."""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_mod import dat_files, romfs_files  # noqa: E402


def neutralize(bxm):
    """the BXM with every real chest's Id / Num value -> "1"; (bytes, chests changed)"""
    d = bytearray(bxm)
    nn, nd, _ = struct.unpack_from('>HHI', d, 8)
    do = 16 + 8 * nn
    so = do + 4 * nd
    strings = d[so:]
    one = strings.find(b'1\x00')                 # "1" itself, or the tail of any string ending in 1 ("-1", "0.1")

    def s(o):
        return None if o == 0xffff else strings[o:strings.index(b'\0', o)].decode()
    nodes = [struct.unpack_from('>4H', d, 16 + 8 * i) for i in range(nn)]
    changed = 0
    for cc, first, _, di in nodes:
        if s(struct.unpack_from('>H', d, do + 4 * di)[0]) != 'SetInfo':
            continue
        kids = {}
        for k in range(first, first + cc):
            kdi = nodes[k][3]
            name_o, val_o = struct.unpack_from('>2H', d, do + 4 * kdi)
            kids[s(name_o)] = (kdi, s(val_o))
        # type 9: an item lying in the open; given Halos it is taken at once when the stage loads (+0xadd0 bit set)
        if kids.get('Type', (0, '0'))[1] in ('4', '5', '6', '7', '8', '9') or kids.get('Id', (0, '0'))[1] == '0':
            continue
        if one < 0:
            raise ValueError('no "1" string in this TreasureChest table')
        for field in ('Id', 'Num'):
            if field in kids:
                struct.pack_into('>H', d, do + 4 * kids[field][0] + 2, one)
        changed += 1
    return bytes(d), changed


def main(b2recomp, mod):
    phases = {}
    with open(os.path.join(mod, 'ap_phases.txt')) as f:            # written by build_mod.write_verse_table
        for line in f:
            p, c = line.split()
            phases[p] = int(c)
    files = romfs_files(b2recomp, lambda p: p.startswith('ph') and p.endswith('.dat'))
    total = 0
    for path, d in sorted(files.items()):
        name = os.path.basename(path)[:-4]                          # p212
        if phases.get(name[1:], 99) >= 18:
            continue
        out = bytearray(d)
        n = 0
        for _, member, off, size, _ in dat_files(d):
            if member.endswith('_TreasureChest.bxm'):
                try:
                    new, n = neutralize(d[off:off + size])
                except ValueError as e:
                    raise ValueError("%s: %s" % (path, e))
                out[off:off + size] = new
        if n:
            dst = os.path.join(mod, 'romfs', *path.split('/'))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, 'wb') as f:
                f.write(out)
            print('%s: %d chests give 1 Halo' % (path, n))
            total += n
    print(total, 'chests')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
