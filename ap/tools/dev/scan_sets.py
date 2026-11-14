"""Every phase's EnemySet: which object types are placed, and every placement that carries an item.

    python scan_sets.py <B2recomp folder> <out.tsv>
Rows: phase, group, set no, object id (hex), item id (hex), hash no, position."""
import os
import sys

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS)
from build_mod import romfs_files, dat_files  # noqa: E402
from bxm import parse  # noqa: E402

files = romfs_files(sys.argv[1], lambda p: p.split('/')[0].startswith('ph') and p.endswith('.dat'))
types = {}
rows = []
for path in sorted(files):
    d = files[path]
    for _, name, off, size, _ in dat_files(d):
        if not name.endswith('_EnemySet.bxm'):
            continue
        root = parse(d[off:off + size])
        for lst in root['kids']:
            for grp in lst['kids']:
                for si in grp['kids']:
                    f = {k['name']: k['text'] for k in si['kids']}
                    oid = int(f.get('Id', '0'))
                    item = int(f.get('ItemId', '4294967295'))
                    types[oid >> 16] = types.get(oid >> 16, 0) + 1
                    if item != 0xffffffff or (oid >> 16) not in (2,):
                        rows.append((name[:-13], grp['attrs'][0][1] if grp['attrs'] else '', f.get('SetNo'), '%x' % oid,
                                     '%x' % item, f.get('HashNo'), f.get('Trans')))
with open(sys.argv[2], 'w', encoding='utf-8') as o:
    for r in rows:
        o.write('\t'.join(str(x) for x in r) + '\n')
print('object types (id >> 16: count):', sorted(types.items()))
print(len(rows), 'rows')
