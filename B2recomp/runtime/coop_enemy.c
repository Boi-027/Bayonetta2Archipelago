/* [Co-op] Enemy sync. The object factory 0x71009cee34 tells coop_obj_new of each object it makes (ap_patch.py COOPNEW);
 * the enemies (ID 0x2xxxx) are kept here.
 *
 * Pairing: both games make the same enemies from the same stage data, so an enemy is known by its ID, its set
 * position (the set info the factory got: +0x40, rounded to 1 unit) and how many of that ID were made there before.
 * HP (object +0x5f4, int): each game sends each enemy's HP and takes the lower of the two. Both games already see
 * about the same hits (each player's own, and the partner repeating the other player's), so adding them up would count
 * each hit twice; the lower HP carries over what only one side saw, and a kill on one side is a kill on the other
 * (HP 0 is a real death: the game's own death follows). HP only goes down, so late or lost packets do no harm.
 * Place: the host's enemies are the real ones; on the joiner each is pulled toward the host's place (+0x40 and its
 * body +0xec0), COOP_EM_PULL of the gap per frame, put there at once past COOP_EM_SNAP.
 * Gone: the game's destructor sets the base class's vtable (0x71017d51c8); an enemy whose vtable is not the one it was
 * made with is gone (its last HP is still sent for EM_LINGER frames).
 * Waves: an enemy the other game lists that this game never made (a wave only that player has started) is made here
 * too, at its set position, by the object request (ap_game.c spawn_object_at): a mirror, with the other game's key.
 * Only top-level enemies (set info +0x64 = -1; parts are made by their enemy). When this game's own stage later makes
 * the same enemy (this player walked in too), the new one takes the mirror's key and HP and the mirror is put away
 * (far below, HP 0), so this game's own fight goes on with its own enemies. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "coop.h"

#define EXPORT __declspec(dllexport)
#define EM_MAX 256
#define EM_HP 0x5f4
#define EM_LINGER 120
#define EM_PER_MSG 36
#define COOP_EM_PULL 0.2f
#define COOP_EM_SNAP 15.0f
#define EM_ASK_FRAMES 300                             /* a mirror asked for and not made by then may be asked again */
enum { M_ENEMY = 2 };
enum { EF_GONE = 1, EF_TOP = 2 };                     /* EmEntry flags */

typedef struct {
    uint64_t obj, vt;
    uint32_t id;
    int16_t kx, kz;                                   /* the set position, rounded */
    uint8_t ord;                                      /* made before at that place with that ID */
    int alive, gone_frames, age;                     /* age: frames seen (HP is set up after the factory) */
    int top, mirror;                                  /* top-level enemy / made here for the other game */
    int32_t hp;                                       /* its HP when last seen (kept after it is gone) */
    int32_t adopt_hp;                                 /* > 0: the HP to take once set up (from the mirror it replaced) */
    int have_target; float target[3];                 /* the host's place (joiner) */
} Enemy;

#pragma pack(push, 1)
typedef struct { uint32_t id; int16_t kx, kz; uint8_t ord, flags; uint8_t pad_[2]; float pos[3]; int32_t hp; } EmEntry;
typedef struct { uint8_t type, n; uint8_t pad_[2]; EmEntry e[EM_PER_MSG]; } EmMsg;
#pragma pack(pop)

static Enemy g_em[EM_MAX];
static volatile LONG g_em_n;
static uint64_t g_em_stage;                           /* player 1 when the list was started */
static struct { uint32_t id; int16_t kx, kz; uint8_t ord; int frames; } g_ask[32];   /* mirrors asked for */
static int g_ask_n;
int spawn_object_at(uint32_t id, const float pos[3]);    /* ap_game.c */

static uint64_t rd64e(uint64_t a) { uint64_t v; memcpy(&v, (void *)(uintptr_t)a, 8); return v; }
static int32_t rdhp(uint64_t o) { int32_t v; memcpy(&v, (void *)(uintptr_t)(o + EM_HP), 4); return v; }
static void wrhp(uint64_t o, int32_t v) { memcpy((void *)(uintptr_t)(o + EM_HP), &v, 4); }
static void hide(uint64_t o) {                        /* far below, out of sight (its place and its body) */
    float v[3];
    memcpy(v, (void *)(uintptr_t)(o + 0x40), 12);
    v[1] -= 1000.0f;
    memcpy((void *)(uintptr_t)(o + 0x40), v, 12);
    memcpy((void *)(uintptr_t)(o + 0xec0), v, 12);
}

/* the object requests (ap_patch.py COOPREQ: 0x71009a7740(manager, name, ID, set info) -> the request): the factory is
 * handed the request, not the set info, so each enemy request's set position and +0x64 word are kept here */
static struct { uint64_t req; float pos[3]; uint32_t parent; } g_req[64];
static volatile LONG g_req_next;
static _Thread_local uint64_t g_req_now;         /* the set info of the request being made on this thread (req 0) */
EXPORT void coop_obj_req(uint64_t req, uint32_t id, uint64_t si) {
    if (!req) { g_req_now = (id >> 16) == 2 ? si : 0; return; }
    g_req_now = 0;
    if ((id >> 16) != 2 || !si) return;
    LONG k = (InterlockedIncrement(&g_req_next) - 1) & 63;
    g_req[k].req = req;
    memcpy(g_req[k].pos, (void *)(uintptr_t)(si + 0x40), 12);
    memcpy(&g_req[k].parent, (void *)(uintptr_t)(si + 0x64), 4);
}

EXPORT void coop_obj_new(uint64_t obj, uint32_t id, uint64_t req) {
    if ((id >> 16) != 2) return;
    LONG i = InterlockedIncrement(&g_em_n) - 1;
    if (i >= EM_MAX) { InterlockedDecrement(&g_em_n); return; }
    Enemy *e = &g_em[i];
    memset(e, 0, sizeof *e);
    e->obj = obj; e->vt = rd64e(obj); e->id = id; e->alive = 1;
    float p[3] = {0};
    int known = 0;
    for (int k = 0; k < 64; k++)
        if (req && g_req[k].req == req) { memcpy(p, g_req[k].pos, 12); e->top = g_req[k].parent == 0xffffffffu; known = 1; break; }
    if (!known && g_req_now) {                        /* made inside its request call */
        uint32_t parent;
        memcpy(p, (void *)(uintptr_t)(g_req_now + 0x40), 12);
        memcpy(&parent, (void *)(uintptr_t)(g_req_now + 0x64), 4);
        e->top = parent == 0xffffffffu;
    }
    e->kx = (int16_t)lroundf(p[0]); e->kz = (int16_t)lroundf(p[2]);
    e->hp = rdhp(obj);
    for (int a = 0; a < g_ask_n; a++)               /* a mirror this game asked for: the other game's key */
        if (g_ask[a].id == id && g_ask[a].kx == e->kx && g_ask[a].kz == e->kz) {
            e->mirror = 1; e->ord = g_ask[a].ord;
            g_ask[a] = g_ask[--g_ask_n];
            return;
        }
    /* this game's own: the k-th of that ID this game made at that place is the other game's k-th there */
    for (LONG k = 0; k < i; k++) if (!g_em[k].mirror && g_em[k].id == id && g_em[k].kx == e->kx && g_em[k].kz == e->kz) e->ord++;
    for (LONG k = 0; k < i; k++)                     /* made here before as a mirror? */
        if (g_em[k].mirror && g_em[k].id == id && g_em[k].kx == e->kx && g_em[k].kz == e->kz && g_em[k].ord == e->ord) {
            Enemy *m = &g_em[k];
            if (m->alive) {                           /* this one takes over (HP, key); the mirror goes */
                e->adopt_hp = m->hp > 0 ? m->hp : 1;
                hide(m->obj);
                wrhp(m->obj, 0);
            } else e->adopt_hp = -1;                  /* already beaten: this one goes too (this game's fight counts it) */
            m->id = 0; m->alive = 0;                  /* no longer known by its key */
            break;
        }
}

static Enemy *find(const EmEntry *x) {
    for (LONG i = 0; i < g_em_n && i < EM_MAX; i++)
        if (g_em[i].id && g_em[i].id == x->id && g_em[i].kx == x->kx && g_em[i].kz == x->kz && g_em[i].ord == x->ord) return &g_em[i];
    return NULL;
}

/* once per game frame (coop_game.c), before the messages: the enemies still here, the pull, their HP and places out */
/* both players fell (coop_game.c): the checkpoint makes the enemies again in both games, so the pairing starts over */
void coop_enemy_reset(void) { InterlockedExchange(&g_em_n, 0); g_ask_n = 0; }

void coop_enemy_frame(uint64_t p1, int connected) {
    if (p1 != g_em_stage) {                           /* another stage: only the enemies still here stay */
        g_em_stage = p1;
        LONG n = 0;
        for (LONG i = 0; i < g_em_n && i < EM_MAX; i++) if (g_em[i].alive && rd64e(g_em[i].obj) == g_em[i].vt) g_em[n++] = g_em[i];
        g_em_n = n;
        g_ask_n = 0;
    }
    for (int a = 0; a < g_ask_n; a++) if (++g_ask[a].frames > EM_ASK_FRAMES) g_ask[a--] = g_ask[--g_ask_n];
    int host = coop_is_host();
    for (LONG i = 0; i < g_em_n && i < EM_MAX; i++) {
        Enemy *e = &g_em[i];
        if (e->alive && rd64e(e->obj) != e->vt) { e->alive = 0; e->gone_frames = 0; }
        if (!e->alive) { e->gone_frames++; continue; }
        if (e->age < 10 && ++e->age == 10 && e->adopt_hp) {   /* set up now: the HP it takes over / gone if already beaten */
            if (e->adopt_hp < 0) { hide(e->obj); wrhp(e->obj, 0); }
            else if (rdhp(e->obj) > e->adopt_hp) wrhp(e->obj, e->adopt_hp);
        }
        e->hp = rdhp(e->obj);
        if (connected && !host && e->have_target) {    /* toward the host's place */
            float here[3], d[3];
            memcpy(here, (void *)(uintptr_t)(e->obj + 0x40), 12);
            for (int k = 0; k < 3; k++) d[k] = e->target[k] - here[k];
            float gap = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            if (gap < 0.01f) continue;
            if (gap < COOP_EM_SNAP * COOP_EM_SNAP) for (int k = 0; k < 3; k++) d[k] *= COOP_EM_PULL;
            static const int k_pos[] = {0x40, 0xec0};
            for (int j = 0; j < 2; j++) {
                float v[3];
                memcpy(v, (void *)(uintptr_t)(e->obj + k_pos[j]), 12);
                for (int k = 0; k < 3; k++) v[k] += d[k];
                memcpy((void *)(uintptr_t)(e->obj + k_pos[j]), v, 12);
            }
        }
    }
    {   /* TEST ONLY: RECOMPS_COOP_EMDBG=1: each enemy once a second */
        static int f;
        if (getenv("RECOMPS_COOP_EMDBG") && ++f % 60 == 0)
            for (LONG i = 0; i < g_em_n && i < EM_MAX; i++) {
                Enemy *e = &g_em[i]; float p[3] = {0};
                if (e->alive) memcpy(p, (void *)(uintptr_t)(e->obj + 0x40), 12);
                printf("coop_emdbg: %05x (%d,%d)#%d alive %d top %d mirror %d hp %d pos (%.1f %.1f %.1f) target %d (%.1f %.1f %.1f)\n",
                       e->id, e->kx, e->kz, e->ord, e->alive, e->top, e->mirror, e->hp, p[0], p[1], p[2],
                       e->have_target, e->target[0], e->target[1], e->target[2]);
                fflush(stdout);
            }
    }
    static unsigned tick, start;
    if (!connected || ++tick % 3) return;             /* 20 a second */
    EmMsg m;
    memset(&m, 0, sizeof m);
    m.type = M_ENEMY;
    LONG n = g_em_n < EM_MAX ? g_em_n : EM_MAX;
    for (LONG c = 0; c < n && m.n < EM_PER_MSG; c++) {   /* in turns when there are more than fit */
        Enemy *e = &g_em[(start + c) % n];
        if (!e->id || (!e->alive && e->gone_frames > EM_LINGER)) continue;
        EmEntry *x = &m.e[m.n++];
        x->id = e->id; x->kx = e->kx; x->kz = e->kz; x->ord = e->ord; x->flags = (uint8_t)((e->alive ? 0 : EF_GONE) | (e->top ? EF_TOP : 0)); x->hp = e->hp;
        if (e->alive) memcpy(x->pos, (void *)(uintptr_t)(e->obj + 0x40), 12);
    }
    start = n ? (start + EM_PER_MSG) % n : 0;
    if (m.n) coop_send(&m, (int)(4 + m.n * sizeof(EmEntry)));
}

/* a message from the partner (coop_game.c hands over what is not its own) */
void coop_enemy_msg(const uint8_t *buf, int n) {
    if (n < 4 || buf[0] != M_ENEMY) return;
    const EmMsg *m = (const EmMsg *)buf;
    int cnt = m->n;
    if (4 + cnt * (int)sizeof(EmEntry) > n) return;
    int from_host = !coop_is_host();
    for (int i = 0; i < cnt; i++) {
        const EmEntry *x = &m->e[i];
        Enemy *e = find(x);
        if (!e) {                                     /* not made here: a wave only the other player started */
            if ((x->flags & (EF_GONE | EF_TOP)) != EF_TOP || g_ask_n >= 32) continue;
            int a;
            for (a = 0; a < g_ask_n; a++) if (g_ask[a].id == x->id && g_ask[a].kx == x->kx && g_ask[a].kz == x->kz && g_ask[a].ord == x->ord) break;
            if (a < g_ask_n) continue;
            float at[3] = {(float)x->kx, x->pos[1], (float)x->kz};
            if (!spawn_object_at(x->id, at)) continue;
            g_ask[g_ask_n].id = x->id; g_ask[g_ask_n].kx = x->kx; g_ask[g_ask_n].kz = x->kz; g_ask[g_ask_n].ord = x->ord; g_ask[g_ask_n].frames = 0;
            g_ask_n++;
            printf("coop: enemy %05x (%d,%d)#%d from the other game made here\n", x->id, x->kx, x->kz, x->ord); fflush(stdout);
            continue;
        }
        /* the lower HP (not from an enemy that left without dying, nor before its HP is set up here) */
        if (e->alive && e->age >= 10 && !((x->flags & EF_GONE) && x->hp > 0)) {
            int32_t hp = rdhp(e->obj);
            if (x->hp < hp) { wrhp(e->obj, x->hp > 0 ? x->hp : 0); e->hp = x->hp > 0 ? x->hp : 0; }
        }
        if (from_host && !(x->flags & EF_GONE)) { memcpy(e->target, x->pos, 12); e->have_target = 1; }
    }
}
