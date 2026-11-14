/* [Archipelago] TEST ONLY research aid: RECOMPS_AP_SAVEWATCH=1 logs every change of the game's save object.
 *
 * The save object is the save file's image in memory (save0000.sav offsets = object offsets); the game reaches it
 * through [0x71018668d0] (the object the title menu's unlock checks read). A host thread copies 0xb000 bytes every
 * 100 ms and prints each 32-bit word that changed: "savewatch: <ms> +<offset> <old> -> <new>". Nothing runs without
 * the variable. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAVE_PTR 0x71018668d0ull
#define SPAN 0xb000

static int readable(uint64_t a, size_t n) {
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery((void *)(uintptr_t)a, &m, sizeof m) || m.State != MEM_COMMIT) return 0;
    return a + n <= (uint64_t)(uintptr_t)m.BaseAddress + m.RegionSize;
}

static DWORD WINAPI watch(void *p) {
    (void)p;
    static uint8_t prev[SPAN], now[SPAN];
    uint64_t base = 0;
    ULONGLONG t0 = GetTickCount64();
    for (;;) {
        Sleep(100);
        int only = 0;
        {   /* TEST ONLY: RECOMPS_AP_TEST_WATCHPTR=<pointer address hex>,<offset hex>[,<seconds>]: once [pointer] is set, a write
             * watch (watch.c; RECOMPS_WATCH_RW=3 for reads too) on [pointer] + offset, 4 bytes */
            static int armed;
            const char *e = getenv("RECOMPS_AP_TEST_WATCHPTR");
            unsigned long long at, off;
            uint64_t p = 0;
            unsigned after = 0;                             /* ,<seconds>: not before then (the save object moves) */
            if (!armed && e && sscanf(e, "%llx,%llx,%u", &at, &off, &after) >= 2 && GetTickCount64() - t0 >= after * 1000ull && readable(at, 8) && (memcpy(&p, (void *)(uintptr_t)at, 8), p)) {
                void watch_arm_at(uint64_t addr, int len);
                armed = 1;
                watch_arm_at(p + off, 4);
            }
            only = (e || getenv("RECOMPS_AP_TEST_LOGPTR")) && !getenv("RECOMPS_AP_SAVEWATCH");   /* no save copies */
        }
        {   /* TEST ONLY: RECOMPS_AP_TEST_LOGPTR=<address hex>[,...]: the u32 at [address] logged when it changes */
            static char spec[256]; static int read; static uint32_t last[8]; static int seen[8];
            if (!read) { read = 1; const char *e = getenv("RECOMPS_AP_TEST_LOGPTR"); if (e) strncpy(spec, e, sizeof spec - 1); }
            int i = 0;
            for (const char *q = spec; *q && i < 8; i++) {
                unsigned long long at = strtoull(q, NULL, 16);
                uint64_t p = 0;
                if (readable(at, 8)) memcpy(&p, (void *)(uintptr_t)at, 8);
                static uint64_t lastp[8];
                if (p != lastp[i]) { printf("logptr: %llu [%llx] -> %llx\n", GetTickCount64() - t0, at, (unsigned long long)p); fflush(stdout); lastp[i] = p; }
                if (p && readable(p, 4)) {
                    uint32_t v; memcpy(&v, (void *)(uintptr_t)p, 4);
                    if (!seen[i] || v != last[i]) { printf("logptr: %llu [%llx] = %u\n", GetTickCount64() - t0, at, v); fflush(stdout); last[i] = v; seen[i] = 1; }
                }
                q = strchr(q, ','); if (!q) break; q++;
            }
        }
        if (only) continue;
        uint64_t b = 0;
        if (readable(SAVE_PTR, 8)) memcpy(&b, (void *)(uintptr_t)SAVE_PTR, 8);
        if (!b || !readable(b, SPAN)) continue;
        {   /* TEST ONLY: RECOMPS_AP_TEST_WARP=<chapter>,<phase hex>,<sub-phase name>: the checkpoint CONTINUE loads
             * (save +0x1e0 chapter, +0xa7c8 phase, +0xa7cc CRC32(lowercase name) & 0x7fffffff, +0xa7d0 name), rewritten
             * every tick during the first 120 s so that the save loaded from the file gets it too */
            static int wc = -2; static unsigned wp; static char wn[24];
            if (wc == -2) { const char *e = getenv("RECOMPS_AP_TEST_WARP"); wc = -1; if (e && sscanf(e, "%d,%x,%23s", &wc, &wp, wn) != 3) wc = -1; }
            if (wc >= 0 && GetTickCount64() - t0 < 120000) {
                uint32_t crc = ~0u;
                for (const char *s = wn; *s; s++) { crc ^= (uint8_t)(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s); for (int k = 0; k < 8; k++) crc = crc >> 1 ^ (0xedb88320u & -(crc & 1)); }
                crc = ~crc & 0x7fffffff;
                uint32_t c32 = (uint32_t)wc;
                char name[24] = {0};
                strncpy(name, wn, sizeof name - 1);
                memcpy((void *)(uintptr_t)(b + 0x1e0), &c32, 4);
                memcpy((void *)(uintptr_t)(b + 0xa7c8), &wp, 4);
                memcpy((void *)(uintptr_t)(b + 0xa7cc), &crc, 4);
                memcpy((void *)(uintptr_t)(b + 0xa7d0), name, sizeof name);
            }
        }
        {   /* TEST ONLY: RECOMPS_AP_TEST_UNLOCK=<n>: the per-difficulty progress counters (+0x284, stride 0x804, 4 of them)
             * at least n, so Chapter Select offers that many chapters */
            static long n = -1;
            if (n < 0) { const char *e = getenv("RECOMPS_AP_TEST_UNLOCK"); n = e ? atol(e) : 0; }
            for (int k = 0; n > 0 && k < 4; k++) {
                uint32_t v;
                memcpy(&v, (void *)(uintptr_t)(b + 0x284 + 0x804 * k), 4);
                if (v < (uint32_t)n) { v = (uint32_t)n; memcpy((void *)(uintptr_t)(b + 0x284 + 0x804 * k), &v, 4); }
            }
        }
        {   /* TEST ONLY: RECOMPS_AP_TEST_SETBITS=<offset hex>:<mask hex>[,...] ORs the mask into the save's word */
            static char spec[256];
            static int read;
            if (!read) { read = 1; const char *e = getenv("RECOMPS_AP_TEST_SETBITS"); if (e) strncpy(spec, e, sizeof spec - 1); }
            for (const char *s = spec; *s; ) {
                unsigned off, mask;
                char op = ':';                                     /* <off>:<mask> ORs, <off>=<value> sets the word */
                if (sscanf(s, "%x%c%x", &off, &op, &mask) != 3 || off + 4 > SPAN) break;
                uint32_t v;
                memcpy(&v, (void *)(uintptr_t)(b + off), 4);
                if (op == '=') { if (v != mask) memcpy((void *)(uintptr_t)(b + off), &mask, 4); }
                else if ((v & mask) != mask) { v |= mask; memcpy((void *)(uintptr_t)(b + off), &v, 4); }
                s = strchr(s, ',');
                if (!s) break;
                s++;
            }
        }
        {   /* TEST ONLY: RECOMPS_AP_TEST_HALOS=<n> keeps the halos at least n (shop research) */
            static long want = -1;
            if (want < 0) { const char *e = getenv("RECOMPS_AP_TEST_HALOS"); want = e ? atol(e) : 0; }
            uint32_t h;
            memcpy(&h, (void *)(uintptr_t)(b + 0xa840), 4);
            if (want > 0 && h < (uint32_t)want) { h = (uint32_t)want; memcpy((void *)(uintptr_t)(b + 0xa840), &h, 4); }
        }
        memcpy(now, (void *)(uintptr_t)b, SPAN);
        if (b != base) {
            printf("savewatch: %llu save object at %llx\n", GetTickCount64() - t0, (unsigned long long)b);
            base = b;
            memcpy(prev, now, SPAN);
            continue;
        }
        {   /* the player candidates: [[0x71018668f8] + 48 / 72] + 0xcedc (HP) */
            static uint32_t last[2] = {~0u, ~0u};
            uint64_t m = 0;
            if (readable(0x71018668f8ull, 8)) memcpy(&m, (void *)(uintptr_t)0x71018668f8ull, 8);
            for (int k = 0; k < 2 && m && readable(m + 48 + 24 * k, 8); k++) {
                uint64_t pl;
                memcpy(&pl, (void *)(uintptr_t)(m + 48 + 24 * k), 8);
                if (!pl || !readable(pl + 0xcedc, 8)) continue;
                uint32_t hp[2];
                memcpy(hp, (void *)(uintptr_t)(pl + 0xcedc), 8);
                if (hp[0] != last[k]) { printf("savewatch: %llu player%d %llx hp %u / %u\n", GetTickCount64() - t0, k, (unsigned long long)pl, hp[0], hp[1]); last[k] = hp[0]; }
            }
        }
        {   /* the phase manager [0x7101869650]: its first 0x800 bytes, words that look like phase numbers */
            static uint32_t pm_prev[0x200];
            static uint64_t pm_base;
            uint64_t pm = 0;
            if (readable(0x7101869650ull, 8)) memcpy(&pm, (void *)(uintptr_t)0x7101869650ull, 8);
            if (pm && readable(pm, 0x800)) {
                uint32_t cur[0x200];
                memcpy(cur, (void *)(uintptr_t)pm, sizeof cur);
                if (pm != pm_base) { pm_base = pm; memcpy(pm_prev, cur, sizeof cur); printf("savewatch: phase manager at %llx\n", (unsigned long long)pm); }
                for (int i = 0; i < 0x200; i++)
                    if (cur[i] != pm_prev[i] && ((cur[i] >= 0x100 && cur[i] < 0xd00) || (pm_prev[i] >= 0x100 && pm_prev[i] < 0xd00)))
                        printf("savewatch: %llu phase +%03x %x -> %x\n", GetTickCount64() - t0, i * 4, pm_prev[i], cur[i]);
                memcpy(pm_prev, cur, sizeof cur);
            }
        }
        int lines = 0;
        for (int o = 0; o < SPAN; o += 4) {
            uint32_t x, y;
            memcpy(&x, prev + o, 4); memcpy(&y, now + o, 4);
            if (x != y && lines++ < 200) printf("savewatch: %llu +%04x %08x -> %08x\n", GetTickCount64() - t0, o, x, y);
        }
        if (lines) fflush(stdout);
        memcpy(prev, now, SPAN);
    }
    return 0;
}

__attribute__((constructor)) static void ap_dev_start(void) {   /* at load: nothing else calls it */
    const char *e = getenv("RECOMPS_AP_SAVEWATCH");
    if ((e && atoi(e)) || getenv("RECOMPS_AP_TEST_WATCHPTR") || getenv("RECOMPS_AP_TEST_LOGPTR")) CreateThread(NULL, 0, watch, NULL, 0, NULL);
}
