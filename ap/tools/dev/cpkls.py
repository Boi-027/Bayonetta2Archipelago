"""List or extract CPK files. python cpkls.py <romfs> list > out.txt | python cpkls.py <romfs> get <path-substring> <outdir>"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'B2recomp', 'scripts'))
from cpk import Cpk


class F:
    def __init__(self, p): self.f = open(p, 'rb')
    def read(self, o, n): self.f.seek(o); return self.f.read(n)


romfs, mode = sys.argv[1], sys.argv[2]
for name in sorted(os.listdir(romfs)):
    if not name.endswith('.cpk'): continue
    c = Cpk(F(os.path.join(romfs, name)))
    for e in c.files:
        if mode == 'list':
            print(name, e['path'], e['extract_size'])
        elif sys.argv[3] in e['path']:
            out = os.path.join(sys.argv[4], name[:-4], e['path'])
            os.makedirs(os.path.dirname(out), exist_ok=True)
            open(out, 'wb').write(c.read(e))
            print('extracted', out)
