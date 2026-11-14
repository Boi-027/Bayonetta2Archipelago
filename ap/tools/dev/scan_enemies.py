"""Every phase's _EnemySet.bxm entries as TSV rows (research for the spawn traps).

    python scan_enemies.py <B2recomp folder> <out.tsv> [phase prefix ...]
Rows: phase, then each entry's fields as name=value."""
import os
import sys

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS)
from build_mod import romfs_files, dat_files  # noqa: E402
from bxm import parse  # noqa: E402

want = tuple(sys.argv[3:])
files = romfs_files(sys.argv[1], lambda p: p.split('/')[0].startswith('ph') and p.endswith('.dat')
                    and (not want or os.path.basename(p).startswith(want)))
rows = []


def walk(node, phase, depth=0):
    kids = node.get('kids') or []
    leaf = {k['name']: (k.get('text') or '').strip() for k in kids if not k.get('kids')}
    if 'Id' in leaf or 'EmId' in leaf or 'ID' in leaf:
        rows.append((phase, ' '.join('%s=%s' % kv for kv in leaf.items())))
    for k in kids:
        if k.get('kids'):
            walk(k, phase, depth + 1)


for path in sorted(files):
    d = files[path]
    for _, name, off, size, _ in dat_files(d):
        if name.endswith('_EnemySet.bxm'):
            walk(parse(d[off:off + size]), name.split('_')[0])
with open(sys.argv[2], 'w', encoding='utf-8') as o:
    for r in rows:
        o.write('\t'.join(r) + '\n')
print(len(rows), 'rows')
