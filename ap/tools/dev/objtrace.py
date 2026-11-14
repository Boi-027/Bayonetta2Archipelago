"""TEMPORARY research trace of object creation. python objtrace.py <gen_eh>   (revert: copy back the .orig files)"""
import os, sys
gen = sys.argv[1]
FILES = {'c_00cd.c': ['71009a7740', '71009a69e8'], 'c_00d1.c': ['71009cee34', '71009cfd6c']}
MARK = '/* [obj-trace] TEMPORARY */'
HELP = MARK + r'''
#include <stdio.h>
#include <stdlib.h>
static int ot_on(void) { static int on = -1; if (on < 0) on = getenv("RECOMPS_TRACE_OBJ") != NULL; return on; }
'''
WRAP = {
    '71009a7740': r'''void f_71009a7740_game(Ctx *c);
void f_71009a7740(Ctx *c) {
    uint64_t name = c->x[1], ra = c->x[30]; uint32_t id = (uint32_t)c->x[2]; uint64_t si = c->x[3];
    f_71009a7740_game(c);
    if (ot_on()) { char n[16] = {0}; for (int i = 0; i < 15 && name; i++) { n[i] = (char)rd8(name + i); if (!n[i]) break; }
        printf("objtrace: request id %05x name %s setinfo %llx from %llx -> %llx\n", id, n, (unsigned long long)si, (unsigned long long)ra, (unsigned long long)c->x[0]); fflush(stdout); }
}
''',
    '71009a69e8': r'''void f_71009a69e8_game(Ctx *c);
void f_71009a69e8(Ctx *c) {
    uint64_t req = c->x[0];
    f_71009a69e8_game(c);
    if (ot_on()) { uint32_t id = rd32(req + 8); if ((id >> 16) == 1) { printf("objtrace: process req %llx id %05x -> %u (state %x)\n", (unsigned long long)req, id, (uint32_t)c->x[0], rd32(req + 12)); fflush(stdout); } }
}
''',
    '71009cfd6c': r'''void f_71009cfd6c_game(Ctx *c);
void f_71009cfd6c(Ctx *c) {
    uint64_t pool = c->x[0]; uint32_t id = (uint32_t)c->x[2];
    if (ot_on() && (id >> 16) == 1) { printf("objtrace: pool %llx count %u max %u for id %05x\n", (unsigned long long)pool, rd32(pool + 48), rd32(pool + 52), id); fflush(stdout); }
    f_71009cfd6c_game(c);
}
''',
    '71009cee34': r'''void f_71009cee34_game(Ctx *c);
void f_71009cee34(Ctx *c) {
    uint64_t ra = c->x[30], si = c->x[1]; uint32_t id = (uint32_t)c->x[2], w3 = (uint32_t)c->x[3];
    f_71009cee34_game(c);
    if (ot_on()) { printf("objtrace: create id %05x w3 %x setinfo %llx from %llx -> obj %llx\n", id, w3, (unsigned long long)si, (unsigned long long)ra, (unsigned long long)c->x[0]); fflush(stdout); }
}
'''}
for name, fns in FILES.items():
    p = os.path.join(gen, name)
    s = open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    assert MARK not in s
    body = HELP
    for fn in fns:
        head = 'void f_%s(Ctx *c) {' % fn
        assert s.count(head) == 1, fn
        s = s.replace(head, 'void f_%s_game(Ctx *c) {' % fn)
        body += WRAP[fn]
    inc = '#include "funcs.h"' + nl
    s = s.replace(inc, inc + body.replace('\n', nl), 1)
    open(p, 'w', encoding='utf-8', newline='').write(s)
print('applied')
