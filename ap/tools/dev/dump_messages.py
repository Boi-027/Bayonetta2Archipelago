"""Every English message of the game, as "<file> <message ID>: <text>", one per line.

    python dump_messages.py <B2recomp folder> <out.txt>
"""
import os
import sys

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS)
from build_mod import romfs_files, dat_files  # noqa: E402
from mcd import Mcd  # noqa: E402

files = romfs_files(sys.argv[1], lambda p: p.endswith('_us.dat'))
with open(sys.argv[2], 'w', encoding='utf-8') as out:
    for path in sorted(files):
        d = files[path]
        try:
            members = list(dat_files(d))
        except AssertionError:
            continue
        for _, name, off, size, _ in members:
            if not name.endswith('.mcd'):
                continue
            m = Mcd(d[off:off + size])
            names = {idx: n.split(b'\0')[0].decode(errors='replace') for (h, idx), n in m.events}
            for i in range(len(m.messages)):
                try:
                    text = ' | '.join(m.text(i))
                except Exception as e:                      # noqa: BLE001  (a message this reader cannot spell)
                    text = '<%s>' % e
                out.write('%s %s: %s\n' % (os.path.basename(path), names.get(i, '#%d' % i), text))
print('done')
