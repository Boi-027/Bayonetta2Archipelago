"""Every phase's TreasureChest / SetCrow / SetBook placements as TSV rows.

    python scan_chests.py <B2recomp folder> <out.tsv>
Rows: kind, phase, No, Id (item ID for chests / Type for books), Num, Type, UniqueId, Difficulty, position."""
import os
import sys

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS)
from build_mod import romfs_files, dat_files  # noqa: E402
from bxm import parse  # noqa: E402

files = romfs_files(sys.argv[1], lambda p: p.split('/')[0].startswith('ph') and p.endswith('.dat'))
rows = []
for path in sorted(files):
    d = files[path]
    for _, name, off, size, _ in dat_files(d):
        for kind in ('TreasureChest', 'SetCrow', 'SetBook'):
            if not name.endswith('_%s.bxm' % kind):
                continue
            for lst in parse(d[off:off + size])['kids']:
                for si in lst['kids']:
                    f = {k['name']: (k.get('text') or '').strip() for k in si['kids']}
                    rows.append((kind, name.split('_')[0], f.get('No', ''), f.get('Id', f.get('Type', '')), f.get('Num', ''),
                                 f.get('Type', ''), f.get('UniqueId', ''), f.get('Difficulty', ''), f.get('Trans', '')))
with open(sys.argv[2], 'w', encoding='utf-8') as o:
    for r in rows:
        o.write('\t'.join(r) + '\n')
print(len(rows), 'rows')
