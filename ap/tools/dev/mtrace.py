"""TEMPORARY: which objects the player methods are called on. python mtrace.py <gen_eh>  (revert: copy back the .orig files)"""
import os, sys, glob, shutil
gen = sys.argv[1]
FNS = ['7100019acc', '7100021ddc', '7100033194', '7100033d90', '7100034968', '7100041720']
MARK = '/* [m-trace] TEMPORARY */'
saved = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mtrace_orig')
os.makedirs(saved, exist_ok=True)
for fn in FNS:
    head = 'void f_%s(Ctx *c) {' % fn
    name = next(p for p in glob.glob(os.path.join(gen, 'c_*.c')) if head in open(p, encoding='utf-8', newline='').read())
    if not os.path.exists(os.path.join(saved, os.path.basename(name))):
        shutil.copy(name, os.path.join(saved, os.path.basename(name)))
    s = open(name, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace(head, 'void f_%s_game(Ctx *c) {' % fn)
    body = MARK + r'''
#include <stdio.h>
#include <stdlib.h>
void f_%(fn)s_game(Ctx *c);
void f_%(fn)s(Ctx *c) {
    static uint64_t seen[16]; static int n;
    uint64_t o = c->x[0];
    if (getenv("RECOMPS_TRACE_M")) { int k = 0; while (k < n && seen[k] != o) k++;
        if (k == n && n < 16) { seen[n++] = o; printf("mtrace: f_%(fn)s on %%llx (from %%llx)\n", (unsigned long long)o, (unsigned long long)c->x[30]); fflush(stdout); } }
    f_%(fn)s_game(c);
}
''' % {'fn': fn}
    inc = '#include "funcs.h"' + nl
    s = s.replace(inc, inc + body.replace('\n', nl), 1)
    open(name, 'w', encoding='utf-8', newline='').write(s)
    print(fn, os.path.basename(name))
