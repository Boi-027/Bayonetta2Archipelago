/* [Co-op] Waiting for each other: a verse starts only when both players are there, and a cinematic is skipped only
 * when both players chose Skip.
 *
 * Verses: the game's verse start 0x7100d6eb14(hash, ..) tells coop_verse_start (ap_patch.py COOPVERSE). With a partner
 * in the game who has not reached that verse, this game is held: the object manager's update passes are skipped
 * (COOPFREEZE: coop_frozen), so the world stands still, and the banner says "Waiting for your partner (1/2)". The
 * partner's game shows "Your partner is waiting for you at a verse"; when it reaches the same verse (its own start
 * call, the same hash), it goes straight on and this game lets go ("2/2"). The hold ends too when the partner leaves.
 *
 * The cinematic's pause menu (ap_opt.c ap_pause_frame hands it over: item ids at +204.., 10 = Skip, 0 = Play; cursor
 * +248; state +188: 0 the list, 1 the "Skip the current cinematic?" question, then 2.. while it closes). With a partner
 * in the game, choosing Skip is a vote: when the question comes up, A is held off (nnhid.c coop_hid_block), the vote is
 * sent, and the menu backs out (B: the cursor to No, B: back to the list, Play: closed), so the cinematic goes on.
 * The top-middle banner (ap_chat.c) says "Skip 1/2" / "Your partner wants to skip". When both voted, each game opens
 * its pause menu itself (Plus) and skips (Skip, A, then YES). A vote lasts SKIP_VOTE_MS.
 * The state goes to the partner in a small message (M_SYNC) every few frames: what each side wants is sent, not
 * events, so a lost packet costs nothing. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coop.h"
#include "ap_chat.h"

#define EXPORT __declspec(dllexport)
#define SKIP_VOTE_MS 120000
#define THEIRS_MS 2000                                /* the partner's last word counts this long */
enum { M_SYNC = 3 };
enum { BTN_A = 0, BTN_B = 1, BTN_PLUS = 10 };

#pragma pack(push, 1)
typedef struct { uint8_t type, skip, waiting, pad_; uint32_t verse; } SyncMsg;   /* verse: the one this game is at */
#pragma pack(pop)

void ap_hid_press(uint64_t buttons, int polls);      /* nnhid.c */
void coop_hid_block(uint64_t buttons);

static int g_my_skip, g_their_skip, g_go;
static ULONGLONG g_my_skip_t, g_their_t, g_menu_seen, g_go_t, g_plus_t, g_skipped_t;   /* g_skipped_t: the last skip done */

#define VERSE_SEND_MS 15000                           /* a verse reached is told this long (the partner may be waiting) */
static uint32_t g_wait_hash, g_reached_hash, g_their_verse;
static int g_their_waiting;
static ULONGLONG g_reached_t, g_released_t;
static char g_note[96];
static int g_note_frames;                             /* game frames it stays (the game over screen runs none: it stays up there) */

/* a short line on the banner at once, for that many game frames (coop_game.c: the partner fell) */
void coop_sync_note(const char *text, int frames) {
    snprintf(g_note, sizeof g_note, "%s", text);
    g_note_frames = frames;
    ap_banner_set(g_note);
}

static uint32_t rd32s(uint64_t a) { uint32_t v; memcpy(&v, (void *)(uintptr_t)a, 4); return v; }
static void wr32s(uint64_t a, uint32_t v) { memcpy((void *)(uintptr_t)a, &v, 4); }
static int item_index(uint64_t menu, uint32_t id) { for (int i = 0; i < 6; i++) if (rd32s(menu + 204 + 4 * i) == id) return i; return -1; }

static int active(void) { return coop_role() == COOP_CONNECTED && GetTickCount64() - g_their_t < THEIRS_MS; }   /* the partner's game is talking */
#define VERSE_PARTNER_MS 15000                        /* for verses, a partner heard this recently is still there (loading screens) */
static int partner_there(void) { return coop_role() == COOP_CONNECTED && GetTickCount64() - g_their_t < VERSE_PARTNER_MS; }

/* a verse starts (ap_patch.py COOPVERSE, before the game's own start) */
EXPORT void coop_verse_start(uint32_t hash) {
    g_reached_hash = hash; g_reached_t = GetTickCount64();
    if (!partner_there()) return;
    if (g_their_verse == hash) { printf("coop: verse %08x: the partner is here already\n", hash); fflush(stdout); return; }
    g_wait_hash = hash;
    printf("coop: verse %08x: waiting for the partner\n", hash); fflush(stdout);
}

static int g_test_freeze;                             /* TEST ONLY (RECOMPS_COOP_FREEZE) */
EXPORT int coop_frozen(void) { return g_wait_hash != 0 || g_test_freeze; }

/* the pause menu, once per frame (ap_opt.c ap_pause_frame, before the menu's own update); 1 = handled here */
int coop_pause_frame(uint64_t menu, int easy_skip) {
    static ULONGLONG seen;
    static int frames, step, voted_here, drive;
    ULONGLONG now = GetTickCount64();
    if (now - seen > 300) { frames = 0; step = 0; voted_here = 0; drive = 0; }   /* a new opening */
    seen = now; g_menu_seen = now;
    frames++;
    if (!menu || !active()) { coop_hid_block(0); return 0; }
    int skip = item_index(menu, 10), play = item_index(menu, 0);
    uint32_t state = rd32s(menu + 188);
    if (skip < 0) {                                   /* not a cinematic's menu */
        coop_hid_block(0);
        if (g_go && g_plus_t && frames > 10) { ap_hid_press(1ull << BTN_PLUS, 6); g_go = 0; g_plus_t = 0; g_my_skip = 0; }   /* opened for nothing: the cinematic is over */
        return g_go ? 1 : 0;
    }
    if (g_go) {                                       /* both want it: Skip, then YES (Easy Skip's steps) */
        coop_hid_block(0);
        if (drive == 0 && state == 1 && frames > 10) { ap_hid_press(1ull << BTN_B, 6); frames = 0; }   /* a question already up: back to the list */
        else if (drive == 0 && state == 0 && frames > 10) { wr32s(menu + 248, (uint32_t)skip); ap_hid_press(1ull << BTN_A, 6); drive = 1; frames = 0; }
        else if (drive == 1 && state == 1 && frames > 10) {
            ap_hid_press(1ull << BTN_A, 6); drive = 2;
            g_go = 0; g_my_skip = 0; g_their_skip = 0; g_plus_t = 0; g_skipped_t = now;
            printf("coop: both skipped: the cinematic is skipped\n"); fflush(stdout);
        }
        return 1;
    }
    if (drive == 2) return 1;                        /* skipped: the menu is closing, nothing more this time */
    if (state == 1) {                                 /* the question: a vote, never a skip of one's own */
        coop_hid_block(1ull << BTN_A);
        if (!voted_here) {
            voted_here = 1; step = 1; frames = 0;
            if (!g_my_skip) { g_my_skip = 1; g_my_skip_t = now; printf("coop: skip vote\n"); fflush(stdout); }
        }
    } else coop_hid_block(0);
    if (step == 1 && frames > 4) { ap_hid_press(1ull << BTN_B, 6); step = 2; frames = 0; }            /* the cursor to No */
    else if (step == 2 && frames > 12) { ap_hid_press(1ull << BTN_B, 6); step = 3; frames = 0; }      /* back to the list */
    else if (step == 3 && state == 0 && frames > 12 && play >= 0) {                                 /* Play: back to the cinematic */
        wr32s(menu + 248, (uint32_t)play); ap_hid_press(1ull << BTN_A, 6); step = 4;
    }
    if (easy_skip && !voted_here && state == 0 && frames > 10 && step == 0) {   /* Easy Cutscene Skip: votes at once */
        wr32s(menu + 248, (uint32_t)skip); ap_hid_press(1ull << BTN_A, 6); voted_here = 1; step = 5; frames = 0;
    }
    if (step == 5 && state == 1) { voted_here = 0; step = 0; }   /* its question: counted above next frame */
    return 1;
}

/* once per game frame (coop_game.c) */
void coop_sync_frame(int connected) {
    { const char *e = getenv("RECOMPS_AP_TEST_BANNER"); if (e) { ap_banner_set(e); return; } }   /* TEST ONLY */
    ULONGLONG now = GetTickCount64();
    if (g_my_skip && now - g_my_skip_t > SKIP_VOTE_MS) g_my_skip = 0;
    if (now - g_their_t > THEIRS_MS) g_their_skip = 0;
    {   /* TEST ONLY: RECOMPS_COOP_FREEZE=<frames>,<frames>: the world held from the first count of frames on for the second */
        static int f, at = -1, len;
        if (at < 0) { const char *e = getenv("RECOMPS_COOP_FREEZE"); at = 0; if (e) sscanf(e, "%d,%d", &at, &len); }
        if (at > 0) { f++; g_test_freeze = f >= at && f < at + len; ap_banner_set(g_test_freeze ? "Waiting for your partner (1/2)" : ""); return; }
    }
    if (g_wait_hash && !partner_there()) {            /* the partner left: no more waiting */
        printf("coop: verse %08x: the partner is gone, going on\n", g_wait_hash); fflush(stdout);
        g_wait_hash = 0;
    }
    if (g_wait_hash && g_their_verse == g_wait_hash) {
        printf("coop: verse %08x: both players here\n", g_wait_hash); fflush(stdout);
        g_wait_hash = 0; g_released_t = now;
    }
    if (!connected) { g_my_skip = g_their_skip = g_go = 0; ap_banner_set(""); return; }
    if (g_my_skip && g_their_skip && !g_go) { g_go = 1; g_go_t = now; g_plus_t = 0; printf("coop: both want to skip\n"); fflush(stdout); }
    if (g_go) {                                       /* this game's pause menu opened (Plus) when it is not up */
        if (now - g_menu_seen > 300 && (!g_plus_t || now - g_plus_t > 1500)) { ap_hid_press(1ull << BTN_PLUS, 6); g_plus_t = now; }
        if (now - g_go_t > 8000) { g_go = 0; g_my_skip = 0; g_plus_t = 0; }   /* no cinematic any more */
    }
    static int tick;
    if (++tick % 6 == 0) {
        SyncMsg m;
        memset(&m, 0, sizeof m);
        m.type = M_SYNC; m.skip = (uint8_t)(g_my_skip || g_go); m.waiting = (uint8_t)(g_wait_hash != 0);
        m.verse = g_wait_hash ? g_wait_hash : now - g_reached_t < VERSE_SEND_MS ? g_reached_hash : 0;
        coop_send(&m, sizeof m);
    }
    if (g_wait_hash) ap_banner_set("Waiting for your partner (1/2)");
    else if (now - g_released_t < 1500) ap_banner_set("Both players here (2/2)");
    else if (g_note_frames > 0) { g_note_frames--; ap_banner_set(g_note); }
    else if (g_their_waiting && g_their_verse && g_their_verse != g_reached_hash) ap_banner_set("Your partner is waiting for you at a verse (1/2)");
    else if (g_go) ap_banner_set("Skipping...");
    else if (g_my_skip && !g_their_skip) ap_banner_set("Skip 1/2 - waiting for your partner");
    else if (g_their_skip && !g_my_skip) ap_banner_set("Your partner wants to skip (1/2) - press + and Skip");
    else ap_banner_set("");
}

void coop_sync_msg(const uint8_t *buf, int n) {
    if (n < (int)sizeof(SyncMsg) || buf[0] != M_SYNC) return;
    const SyncMsg *m = (const SyncMsg *)buf;
    g_their_skip = GetTickCount64() - g_skipped_t < 3000 ? 0 : m->skip;   /* the partner's own last "skipping" words */
    g_their_verse = m->verse; g_their_waiting = m->waiting;
    g_their_t = GetTickCount64();
}
