"""TEMPORARY research trace. python trace_patch.py <gen_eh> apply|revert
Logs: message-ID hashes (f_7100dd220c), main-menu wheel label lookups (f_7100d5959c), and the first entry of every
function in the UI files (FN_FILES), each with milliseconds since start."""
import glob, os, re, shutil, sys

gen, mode = sys.argv[1], sys.argv[2]
MSG, MENU = os.path.join(gen, 'c_0126.c'), os.path.join(gen, 'c_011c.c')
FN_FILES = []   # function-entry trace off: its printf from many threads deadlocked a run
HELPER = r'''
/* [ap-trace] TEMPORARY */
#include <stdio.h>
__declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);
static unsigned long long ap_t0;
unsigned long long ap_ms(void) { unsigned long long t = GetTickCount64(); if (!ap_t0) ap_t0 = t; return t - ap_t0; }
static unsigned ap_seen[4096];
void ap_trace_msg(uint64_t id, uint64_t txt) {
    char s[96]; int n = 0;
    unsigned h = 2166136261u;
    for (; n < 95 && id; n++) { s[n] = (char)rd8(id + n); if (!s[n]) break; h = (h ^ (unsigned char)s[n]) * 16777619u; }
    s[n] = 0;
    unsigned *slot = &ap_seen[h & 4095];
    if (*slot == h) return;                       /* each ID once (collisions just drop a line) */
    *slot = h;
    printf("ap-trace: %llu hash %s -> %08llx\n", ap_ms(), s, (unsigned long long)txt); fflush(stdout);
}
void ap_trace_menu(uint64_t self, uint64_t kind, uint64_t idx) {
    static uint64_t last = ~0ull;
    uint64_t k = (kind << 32) | (uint32_t)idx;
    if (k == last) return;
    last = k;
    printf("ap-trace: %llu menu kind %u index %d\n", ap_ms(), (unsigned)kind, (int)idx); fflush(stdout);
}
static volatile long ap_fn_lock;
static uint64_t ap_fn_tab[1 << 16];
void ap_fn(uint64_t a) {                              /* first entry of each function (racy reads are fine: dupes only) */
    unsigned i = (unsigned)(a >> 2) & 0xffff;
    while (ap_fn_tab[i]) { if (ap_fn_tab[i] == a) return; i = (i + 1) & 0xffff; }
    while (__sync_lock_test_and_set(&ap_fn_lock, 1)) {}
    i = (unsigned)(a >> 2) & 0xffff;
    while (ap_fn_tab[i] && ap_fn_tab[i] != a) i = (i + 1) & 0xffff;
    if (!ap_fn_tab[i]) { ap_fn_tab[i] = a; printf("ap-fn: %llu %llx\n", ap_ms(), (unsigned long long)a); fflush(stdout); }
    __sync_lock_release(&ap_fn_lock);
}
'''
PROTO = '\nvoid ap_trace_menu(uint64_t, uint64_t, uint64_t);\nvoid ap_fn(uint64_t);\n'
FN_RE = re.compile(r'^(  c->fn = (0x[0-9a-f]+ULL);\n)', re.M)


def patch(path, edits, prepend='', fn=False):
    if not os.path.exists(path + '.aporig'): shutil.copy2(path, path + '.aporig')
    t = open(path + '.aporig', encoding='utf-8').read()
    for old, new in edits:
        assert t.count(old) == 1, (path, old)
        t = t.replace(old, new)
    if fn:
        t = FN_RE.sub(lambda m: m.group(1) + '  ap_fn(%s);\n' % m.group(2), t)
    if prepend:                                    # after the generated #includes
        i = t.index('#include "funcs.h"\n') + len('#include "funcs.h"\n')
        t = t[:i] + prepend + t[i:]
    open(path, 'w', encoding='utf-8').write(t)


if mode == 'apply':
    patch(MSG, [('L_7100dd2230: { f_7100dd2094(c); return; }',
                 'L_7100dd2230: { uint64_t id_ = c->x[0]; f_7100dd2094(c); ap_trace_msg(id_, c->x[0]); return; }')],
          HELPER + PROTO)
    patch(MENU, [('  c->fn = 0x7100d5959cULL;\n', '  c->fn = 0x7100d5959cULL;\n  ap_trace_menu(c->x[0], c->x[1], c->x[2]);\n')],
          PROTO)
    for p in FN_FILES:
        if p not in (MSG, MENU): patch(p, [], PROTO, fn=True)
    print('applied')
else:
    for p in glob.glob(os.path.join(gen, '*.aporig')):
        shutil.move(p, p[:-len('.aporig')])
    print('reverted')
