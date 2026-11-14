/* [Co-op] Story co-op in the stage: the partner is a second Bayonetta, made like the player herself and driven by
 * the partner's own controller.
 *
 * The partner: when connected and in a stage, a player-type object (0x10000 "pl0000", the spawn queue of ap_game.c)
 * is requested. Being a player, she takes the player slots of the player list [0x71018668f8] (+32/+40/+48, +80/+88/+96)
 * and the camera's target ([[0x71018668f0] +2704]); those go back to player 1 and she moves to the partner slot
 * (+56/+64/+72).
 * Her controller: the game keeps 6 raw controller records (0xc0 bytes each) at [0x7101870000+1424] (0x7100d9ea30) and
 * turns each into a controller object (0x7100d9eef4; 0x71018aab70 + 0x1f0 * n; copied to [0x7101866998] by
 * 0x710004e3d4). Each player copies controller 0 into its own input copies (object +2112 and +2608, 0x1f0 each) in
 * 0x7100025c68; right after (ap_patch.py COOPINPUT), hers are rewritten with the record her own game made this frame
 * (it comes over the connection). The players' updates run on several threads at once, so nothing shared is swapped.
 * Her stick: the left stick (controller object +0x10 x, +0x14 y; -1000..1000, up = -1000; +0 bits 0xf0000 = the
 * stick as directions) is camera-relative, and the two games' cameras look different ways, so it is turned by the
 * difference of the cameras' headings (the camera object [0x71018668f0]: +0x170 eye, +0x180 the point it looks at;
 * heading = atan2(dx, dz), the way "up" runs).
 * The camera: 0x710003e8f4 (from the player update 0x7100034968) writes the camera's follow block (+0x450..+0x4ac)
 * from the player it runs for; run for her too, the camera follows each of them in turn (jumps, views from inside a
 * Bayonetta, a fixed far view), so she does not run it (ap_patch.py COOPSKIP).
 * Her place: each state message carries the sender's position; the partner here is pulled toward it a little each
 * frame (COOP_PULL), or put there at once when more than COOP_SNAP_DIST away (a warp, a respawn).
 * Her HP here is the partner's own HP (never below 1: she falls only in her own game).
 * Leaving: when the connection is gone she is removed the game's own way: byte +0x4fc bit 1 asks the object manager
 * (0x71009a75f8, from 0x7100992cdc) to remove the object; it answers with bit 0 and destroys it two passes later
 * (0x71009a6d1c). Found by watching an enemy's vtable as it died. Her partner slots get back what was there before.
 * Falling together: each state message carries how often this player fell (own deaths: HP down to 0). When the
 * partner's count goes up, this game's Bayonetta is killed too (ap_game.c ap_coop_kill: the Death Link kill, not sent
 * to AP), so both take the Continue and start again from the checkpoint; a death caused that way is not counted
 * back. The enemy pairing starts over (coop_enemy_reset): the checkpoint makes the enemies again in both games. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "coop.h"

#define EXPORT __declspec(dllexport)
#define PLAYERS 0x71018668f8ull
#define CAMERA 0x71018668f0ull
#define PADS 0x7101870590ull                         /* [0x7101870000 + 1424]: the raw controller records (0xc0 each) */
#define PADOBJ 0x71018aab70ull                       /* controller 0's game state (0x1f0; 1..4 follow) */
#define PAD_SIZE 0x1f0
#define PAD_STICK 0x10                                /* the left stick x, y (floats) */
#define PAD_DIRS 0xf0000u                             /* +0: the stick as directions (left, right, up, down) */
#define PL_INPUT_A 2112                               /* a player's own input copies (0x1f0 each) */
#define PL_INPUT_B 2608
#define COOP_SNAP_DIST 8.0f
#define COOP_PULL 0.15f                               /* share of the gap closed per frame */
#define COOP_STILL 0.05f                              /* gaps below this are left alone */
#define PL_HP 0xcedc
#define OBJ_KILL 0x4fc                                /* byte: bit 1 = remove this object (the object manager) */
#define OBJ_MANAGER 0x71018668d8ull                   /* [..]: the object manager whose removal pass (0x71009a75f8) holds the players: +112 its list (node +0 entry, +16 next), +120 the end; entry +12 flags, +48 the object */
enum { M_STATE = 1 };

#pragma pack(push, 1)
typedef struct { uint8_t type, deaths; uint8_t pad_[2]; uint32_t frame; float pos[3]; float yaw; uint32_t hp; uint8_t rec[PAD_SIZE]; } StateMsg;
#pragma pack(pop)

static uint64_t rd64h(uint64_t a) { uint64_t v; memcpy(&v, (void *)(uintptr_t)a, 8); return v; }

static uint64_t g_partner;                           /* the partner's object here */
static uint64_t g_p1, g_keep[6];                     /* player 1 and her slots, kept to put back */
static uint32_t g_cam_keep;
static int g_wanted;                                  /* a partner was requested and not seen yet */
static uint8_t g_remote_rec[PAD_SIZE];
static int g_have_remote;
static float g_remote_pos[3], g_remote_yaw;
static uint32_t g_frame;
static int g_snaps;                                   /* how often she was put in place (the debug line) */
static uint64_t g_keep_slot2[3];                      /* the partner slots (+56/+64/+72) before her */
static uint64_t g_leaving, g_leaving_vt;              /* the partner being removed (until the game destroys her) */
static uint32_t g_remote_hp;
static uint8_t g_my_deaths, g_their_deaths;
static int g_their_deaths_known, g_partner_fell;
static ULONGLONG g_quiet_t;                           /* a death the partner's fall caused: not counted */

int spawn_object_request(uint32_t id);               /* ap_game.c */
void ap_coop_kill(void);
void coop_enemy_reset(void);                         /* coop_enemy.c */
void coop_sync_note(const char *text, int frames);   /* coop_sync.c */

/* the camera's heading here (radians; 0 when there is no camera) */
static float cam_yaw(void) {
    uint64_t cam = rd64h(CAMERA);
    if (!cam) return 0;
    float e[3], t[3];
    memcpy(e, (void *)(uintptr_t)(cam + 0x170), 12);
    memcpy(t, (void *)(uintptr_t)(cam + 0x180), 12);
    return atan2f(t[0] - e[0], t[2] - e[2]);
}

/* her record for this frame: the partner's, with the stick turned from her camera to this one */
static uint8_t g_her_rec[PAD_SIZE];
static int g_her_ready;
static void her_record(void) {
    memcpy(g_her_rec, g_remote_rec, sizeof g_her_rec);
    float st[2];
    memcpy(st, g_her_rec + PAD_STICK, 8);
    float m = sqrtf(st[0] * st[0] + st[1] * st[1]);
    if (m > 1) {                                      /* "up" runs along the heading: angle = heading - stick angle */
        float a = atan2f(st[0], -st[1]) + cam_yaw() - g_remote_yaw;
        st[0] = m * sinf(a); st[1] = -m * cosf(a);
        memcpy(g_her_rec + PAD_STICK, st, 8);
        uint32_t dirs;                                /* and the stick's direction bits with it */
        memcpy(&dirs, g_her_rec, 4);
        dirs &= ~PAD_DIRS;
        if (st[0] < -500) dirs |= 0x10000;
        if (st[0] > 500) dirs |= 0x20000;
        if (st[1] < -500) dirs |= 0x40000;
        if (st[1] > 500) dirs |= 0x80000;
        memcpy(g_her_rec, &dirs, 4);
    }
    g_her_ready = 1;
}

/* after a player copied the controller into its own input copies (ap_patch.py COOPINPUT): hers become her record */
EXPORT void coop_obj_input(uint64_t obj) {
    if (obj && (obj == g_leaving || (obj == g_partner && !g_have_remote))) {   /* going, or her player not in the stage yet: no input */
        memset((void *)(uintptr_t)(obj + PL_INPUT_A), 0, PAD_SIZE);
        memset((void *)(uintptr_t)(obj + PL_INPUT_B), 0, PAD_SIZE);
        return;
    }
    if (!obj || obj != g_partner || !g_have_remote) return;
    if (!g_her_ready) her_record();
    memcpy((void *)(uintptr_t)(obj + PL_INPUT_A), g_her_rec, sizeof g_her_rec);
    memcpy((void *)(uintptr_t)(obj + PL_INPUT_B), g_her_rec, sizeof g_her_rec);
}

/* player functions the partner does not run (ap_patch.py COOPSKIP: 0x710003e8f4, the camera's follow block) */
EXPORT int coop_obj_skip(uint64_t obj) { return obj && (obj == g_partner || obj == g_leaving); }

/* the connection is gone: she is removed (the game destroys her a few frames later) */
static void partner_remove(uint64_t m) {
    uint8_t b;
    memcpy(&b, (void *)(uintptr_t)(g_partner + OBJ_KILL), 1);
    b |= 2;
    memcpy((void *)(uintptr_t)(g_partner + OBJ_KILL), &b, 1);
    /* the manager's entry for her: the players' entries are kept out of the removal pass (flags +12 bit 1 clear) */
    uint64_t mm = rd64h(OBJ_MANAGER), node = mm ? rd64h(mm + 112) : 0, end = mm ? rd64h(mm + 120) : 0;
    for (int n = 0; node && node != end && n < 8192; n++, node = rd64h(node + 16)) {
        uint64_t ent = rd64h(node);
        if (ent && rd64h(ent + 48) == g_partner) {
            uint32_t fl;
            memcpy(&fl, (void *)(uintptr_t)(ent + 12), 4);
            fl |= 2;
            memcpy((void *)(uintptr_t)(ent + 12), &fl, 4);
            break;
        }
    }
    if (rd64h(m + 72) == g_partner) memcpy((void *)(uintptr_t)(m + 56), g_keep_slot2, 24);
    g_leaving = g_partner; g_leaving_vt = rd64h(g_partner);
    printf("coop: the partner left: %llx removed\n", (unsigned long long)g_partner); fflush(stdout);
    g_partner = 0; g_have_remote = 0;
}

/* once per game frame (ap_game.c ap_game_frame) */
void coop_enemy_frame(uint64_t p1, int connected);   /* coop_enemy.c */
void coop_enemy_msg(const uint8_t *buf, int n);
void coop_sync_frame(int connected);                 /* coop_sync.c */
void coop_sync_msg(const uint8_t *buf, int n);

void coop_game_frame(void) {
    g_her_ready = 0;
    int connected = coop_role() == COOP_CONNECTED;
    /* the partner's messages (also in menus and cinematics, where there is no player) */
    uint8_t buf[COOP_MSG_MAX];
    int n;
    while (connected && (n = coop_take(buf, sizeof buf)) > 0) {
        if (buf[0] == M_STATE && n >= (int)sizeof(StateMsg)) {
            const StateMsg *r = (const StateMsg *)buf;
            memcpy(g_remote_rec, r->rec, PAD_SIZE);
            memcpy(g_remote_pos, r->pos, 12);
            g_remote_yaw = r->yaw;
            g_remote_hp = r->hp;
            g_have_remote = 1;
            if (!g_their_deaths_known) { g_their_deaths_known = 1; g_their_deaths = r->deaths; }
            else if (r->deaths != g_their_deaths) { g_their_deaths = r->deaths; g_partner_fell = 1; }
        } else if (buf[0] == 3) coop_sync_msg(buf, n);
        else coop_enemy_msg(buf, n);
    }
    if (!connected) { g_their_deaths_known = 0; g_partner_fell = 0; }
    coop_sync_frame(connected);
    uint64_t m = rd64h(PLAYERS);
    uint64_t p1 = m ? rd64h(m + 48) : 0;
    if (!m || !p1) { g_partner = 0; g_p1 = 0; g_wanted = 0; g_have_remote = 0; g_leaving = 0; g_partner_fell = 0; return; }
    if (g_leaving && rd64h(g_leaving) != g_leaving_vt) g_leaving = 0;   /* destroyed */
    if (p1 != g_p1 && p1 != g_partner) {
        if (g_wanted && g_p1 && rd64h(m + 48) != g_p1) {
            /* the requested partner arrived and took the player slots: back to player 1, her to the partner slot */
            uint64_t s[3];
            memcpy(s, (void *)(uintptr_t)(m + 32), 24);
            memcpy(g_keep_slot2, (void *)(uintptr_t)(m + 56), 24);
            memcpy((void *)(uintptr_t)(m + 56), s, 24);
            memcpy((void *)(uintptr_t)(m + 32), &g_keep[0], 24);
            memcpy((void *)(uintptr_t)(m + 80), &g_keep[3], 24);
            uint64_t cam = rd64h(CAMERA);
            if (cam) memcpy((void *)(uintptr_t)(cam + 2704), &g_cam_keep, 4);
            g_partner = s[2]; g_wanted = 0;
            printf("coop: the partner is %llx (player 1 %llx)\n", (unsigned long long)g_partner, (unsigned long long)g_p1);
            fflush(stdout);
            p1 = g_p1;
        } else {
            g_p1 = p1; g_partner = 0; g_wanted = 0;
            memcpy(&g_keep[0], (void *)(uintptr_t)(m + 32), 24);
            memcpy(&g_keep[3], (void *)(uintptr_t)(m + 80), 24);
            uint64_t cam = rd64h(CAMERA);
            if (cam) memcpy(&g_cam_keep, (void *)(uintptr_t)(cam + 2704), 4);
        }
    }
    if (!connected && g_partner) partner_remove(m);
    {   /* falling together */
        static uint64_t pl_was;
        static uint32_t hp_was;
        uint32_t hp;
        memcpy(&hp, (void *)(uintptr_t)(g_p1 + PL_HP), 4);
        if (g_p1 == pl_was && hp == 0 && hp_was > 0 && hp_was < 100000) {
            if (GetTickCount64() > g_quiet_t) { g_my_deaths++; printf("coop: fell (%u)\n", g_my_deaths); fflush(stdout); }
            coop_enemy_reset();
        }
        pl_was = g_p1; hp_was = hp;
        if (g_partner_fell) {
            g_partner_fell = 0;
            if (hp > 0) {
                g_quiet_t = GetTickCount64() + 10000;
                ap_coop_kill();
                coop_sync_note("Your partner fell - back to the checkpoint", 360);
                printf("coop: the partner fell: falling too\n"); fflush(stdout);
            }
        }
    }
    coop_enemy_frame(g_p1, connected);
    if (connected && !g_partner && !g_wanted && g_p1) {   /* ask for her (made a few frames later) */
        if (spawn_object_request(0x10000)) { g_wanted = 1; printf("coop: the partner is requested\n"); fflush(stdout); }
    }
    {   /* TEST ONLY: RECOMPS_COOP_DEBUG=1: player 1, the partner and the camera, once a second */
        static int f;
        if (getenv("RECOMPS_COOP_DEBUG") && ++f % 60 == 0) {
            uint64_t cam = rd64h(CAMERA);
            float a[3] = {0}, b[3] = {0};
            memcpy(a, (void *)(uintptr_t)(g_p1 + 0x40), 12);
            if (g_partner) memcpy(b, (void *)(uintptr_t)(g_partner + 0x40), 12);
            uint32_t c0 = 0, c1 = 0, c2 = 0; uint64_t t = 0;
            if (cam) { memcpy(&c0, (void *)(uintptr_t)(cam + 2704), 4); memcpy(&t, (void *)(uintptr_t)(cam + 2712), 8); memcpy(&c1, (void *)(uintptr_t)(cam + 1056), 4); }
            memcpy(&c2, &g_cam_keep, 4);
            uint64_t pads = rd64h(PADS);
            printf("coop_dbg: records:");
            for (int k = 0; pads && k < 6; k++) {
                uint32_t w[8]; memcpy(w, (void *)(uintptr_t)(pads + 0xc0 * k), 32);
                printf(" [%d] %x %x %x %x %x %x", k, w[0], w[2], w[3], w[4], w[5], w[6]);
            }
            printf("\n");
            printf("coop_dbg: p1 (%.1f %.1f %.1f) partner (%.1f %.1f %.1f) cam +2704 %x (kept %x) +2712 %llx +1056 %u remote %d gap (%.2f %.2f %.2f) snaps %d yaw %.0f/%.0f\n",
                   a[0], a[1], a[2], b[0], b[1], b[2], c0, c2, (unsigned long long)t, c1, g_have_remote,
                   g_remote_pos[0] - b[0], g_remote_pos[1] - b[1], g_remote_pos[2] - b[2], g_snaps, cam_yaw() * 57.3f, g_remote_yaw * 57.3f);
            fflush(stdout);
        }
    }
    if (!connected) return;
    /* this player's state to the partner */
    StateMsg s;
    memset(&s, 0, sizeof s);
    s.type = M_STATE; s.frame = ++g_frame; s.deaths = g_my_deaths;
    memcpy(&s.hp, (void *)(uintptr_t)(g_p1 + PL_HP), 4);
    memcpy(s.pos, (void *)(uintptr_t)(g_p1 + 0x40), 12);
    s.yaw = cam_yaw();
    memcpy(s.rec, (void *)(uintptr_t)PADOBJ, PAD_SIZE);
    coop_send(&s, sizeof s);
    if (g_partner && g_have_remote && g_remote_hp < 100000) {   /* her HP: the partner's own (she falls only over there) */
        uint32_t hp = g_remote_hp ? g_remote_hp : 1;
        memcpy((void *)(uintptr_t)(g_partner + PL_HP), &hp, 4);
    }
    if (g_partner && g_have_remote) {                     /* toward where she is over there */
        float here[3];
        memcpy(here, (void *)(uintptr_t)(g_partner + 0x40), 12);
        float d[3] = {g_remote_pos[0] - here[0], g_remote_pos[1] - here[1], g_remote_pos[2] - here[2]};
        float gap = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        if (gap > COOP_SNAP_DIST * COOP_SNAP_DIST) g_snaps++;
        else if (gap > COOP_STILL * COOP_STILL) for (int k = 0; k < 3; k++) d[k] *= COOP_PULL;
        else return;
        /* the same shift to each copy of her place: +0x40 (her transform), +0xee0 / +0xef0 (her body: now / before) */
        static const int k_pos[] = {0x40, 0xee0, 0xef0};
        for (int i = 0; i < 3; i++) {
            float v[3];
            memcpy(v, (void *)(uintptr_t)(g_partner + k_pos[i]), 12);
            for (int k = 0; k < 3; k++) v[k] += d[k];
            memcpy((void *)(uintptr_t)(g_partner + k_pos[i]), v, 12);
        }
    }
}
