"""TEST ONLY: log every call of the save object's setters (which bit, how much, from where).

    python save_trace_patch.py <gen_eh> apply|revert

Wraps each function below: "savecall: <name> a1=<x1> a2=<x2> lr=<caller>" then the game's own. revert restores the
files from their .apsave copies. Run after setup.py's lift (it patches the lifted C as it is then)."""
import glob
import os
import re
import shutil
import sys

SETTERS = {                    # function -> what it is thought to be (save object offsets)
    '7100d96a20': 'bit a46c set',
    '7100d93648': 'bit a320 set',
    '7100d97608': 'bit aba0 set',
    '7100d90890': 'bit a4d8 set',
    '7100d908b8': 'bit a4dc set',
    '7100d908e0': 'bit a4e0 set',
    '7100d90a10': 'item a224 add',
    '7100d935c8': 'halos a840 add',
    '7100d90a38': 'a468 add',
}
gen, mode = sys.argv[1], sys.argv[2]
if mode == 'revert':
    for p in glob.glob(os.path.join(gen, '*.apsave')):
        shutil.move(p, p[:-len('.apsave')])
    print('reverted')
    sys.exit()
for f in sorted(glob.glob(os.path.join(gen, 'c_*.c'))):
    t = open(f, encoding='utf-8').read()
    found = [a for a in SETTERS if 'void f_%s(Ctx *c) {' % a in t]
    if not found:
        continue
    if not os.path.exists(f + '.apsave'):
        shutil.copy2(f, f + '.apsave')
    extra = '\n#include <stdio.h>\n'
    for a in found:
        t = t.replace('void f_%s(Ctx *c) {' % a, 'void f_%s_game(Ctx *c) {' % a)
        extra += ('void f_%s_game(Ctx *c);\nvoid f_%s(Ctx *c) { printf("savecall: %s a1=%%llx a2=%%llx lr=%%llx\\n", '
                  '(unsigned long long)c->x[1], (unsigned long long)c->x[2], (unsigned long long)c->x[30]); fflush(stdout); f_%s_game(c); }\n'
                  % (a, a, SETTERS[a], a))
    i = t.index('#include "funcs.h"\n') + len('#include "funcs.h"\n')
    open(f, 'w', encoding='utf-8').write(t[:i] + extra + t[i:])
    print('patched', os.path.basename(f), found)
