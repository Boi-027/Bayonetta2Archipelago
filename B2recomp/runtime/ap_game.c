/* [Archipelago] The game side of the client, once per game frame on the game's thread (ap_game_frame, called by
 * the patched play-time update 0x7100d9293c with the save object; see scripts/ap_patch.py and ap/NOTES.md).
 *
 * Locations (IDs as in the apworld's Locations.py):
 *   verses: when the game stores a verse result (ap_verse_end with the verse's hash; ap_verses.txt -> chapter, number)
 *       story verse v of chapter c -> 221000 + c * 32 + v; Muspelheim / Witch Trial verse v -> 222000 + v
 *   verse medals: the story verse's overall medal (verse_medal) -> 226000 + tier * 1000 + c * 32 + v for tier 0
 *       (any medal) and 1 (Bronze+) .. the medal (5 = Pure Platinum); the server ignores the tiers the seed lacks
 *   chapters: the CONTINUE chapter (save +0x1e0) moving on from c, or a committed result (ap_chapter_done) -> 221900 + c
 * Items: spendable ones (halos, consumables) are applied once each: the count applied is kept in
 * config\ap_<seed>_<slot>.ini; permanent ones are re-applied whenever the game's state lacks them. */
#include <windows.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ap.h"
#include "ap_chat.h"

#define EXPORT __declspec(dllexport)
#define SAVE_CONTINUE 0x1e0                   /* the chapter CONTINUE resumes */
#define SAVE_ITEM_SEEN 0xa450                 /* bitset by game item ID (MSB first): the Items page lists only these */
#define SAVE_ITEMS 0xa564                     /* 8-byte record per game item ID, +0 = count (verified in the Items page) */
#define SAVE_HEART_PIECES 0xa82c              /* Broken Witch Hearts held, one bit each (MSB first), 4 per heart */
#define SAVE_PEARL_PIECES 0xa830              /* Broken Moon Pearls held, 2 per pearl (+0xa838: a piece's world pickup?) */
#define SAVE_HALOS 0xa840
#define HALOS_MAX 999999999u
#define VERSE_BASE 221000
#define CHAPTER_BASE 221900
#define TRIAL_BASE 222000
#define RANK_BASE 226000                      /* verse medals: + tier * 1000 + c * 32 + v (apworld Locations.py) */
#define ITEM_BASE 220000

static uint32_t rd32h(uint64_t a) { uint32_t v; memcpy(&v, (void *)(uintptr_t)a, 4); return v; }
static void wr32h(uint64_t a, uint32_t v) { memcpy((void *)(uintptr_t)a, &v, 4); }

/* ---------------------------------------------------------------------------------------------- progress file */

static char g_session[96], g_file[MAX_PATH];
static int g_applied;                          /* received items already applied (spendable ones) */

static void progress_open(void) {
    char s[96];
    ap_net_session(s, sizeof s);
    if (!s[0] || !strcmp(s, g_session)) return;
    strcpy(g_session, s);
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, sizeof exe);
    char *p = strrchr(exe, '\\'); if (p) *p = 0;
    snprintf(g_file, sizeof g_file, "%s\\..\\config\\ap_%s.ini", exe, s);
    g_applied = GetPrivateProfileIntA("progress", "applied", 0, g_file);
    printf("ap: session %s, %d items applied before\n", s, g_applied);
    fflush(stdout);
}
static void progress_save(void) {
    char v[16];
    snprintf(v, sizeof v, "%d", g_applied);
    WritePrivateProfileStringA("progress", "applied", v, g_file);
}

/* ---------------------------------------------------------------------------------------------- chapter blocking */

/* With chapter_blocking (slot_data) a chapter needs its "Chapter Unlock" item (220500 + shown chapter - 1; the
 * Prologue and the starting chapter are open). Playing a locked chapter is allowed, but its checks are held (kept in
 * the progress file) and sent once the unlock arrives, so the multiworld's logic holds. */
static int shown_chapter(int c) { return c <= 1 ? 0 : c - 1; }     /* ChapterData 0, 1 = the Prologue */

/* config\archipelago.ini [cheats] send_locked_chapters=1: a locked chapter's checks go out anyway (the story is played
 * in order and Chapter Select cannot jump ahead to an unlocked chapter yet) */
static int send_locked_chapters(void) {
    static int v = -1;
    if (v < 0) {
        char ini[MAX_PATH + 32], *s;
        GetModuleFileNameA(NULL, ini, MAX_PATH);
        if ((s = strrchr(ini, '\\'))) *s = 0;
        strcat(ini, "\\..\\config\\archipelago.ini");
        v = GetPrivateProfileIntA("cheats", "send_locked_chapters", 0, ini) != 0;
    }
    return v;
}

static int chapter_open(int c) {
    if (c < 0 || c >= 18 || !ap_net_slot_int("chapter_blocking", 0) || send_locked_chapters()) return 1;
    int d = shown_chapter(c);
    if (d == 0 || d == ap_net_slot_int("starting_chapter", 1)) return 1;
    ApItem it[256];
    int from = 0, total = 0;
    do {
        int k = ap_net_items(from, it, 256, &total);
        if (k <= 0) break;
        for (int i = 0; i < k; i++) if (it[i].item == 220500 + d - 1) return 1;
        from += k;
    } while (from < total);
    return 0;
}

static void chapter_check(int64_t location, int c) {
    if (chapter_open(c)) { ap_net_check(location); return; }
    char key[24], val[8];
    snprintf(key, sizeof key, "%lld", (long long)location);
    snprintf(val, sizeof val, "%d", c);
    if (g_file[0]) WritePrivateProfileStringA("held", key, val, g_file);
    static uint32_t told;                         /* the chat says so once per chapter and run */
    if (!(told & 1u << c)) {
        static const char *roman[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII", "XIII",
                                      "XIV", "XV", "XVI"};
        char t[200], item[64];
        uint8_t col[200];
        int d = shown_chapter(c);
        told |= 1u << c;
        snprintf(item, sizeof item, "Chapter Unlock: Chapter %s", roman[d]);
        int n = snprintf(t, sizeof t, "Chapter %s is locked: its checks are sent when %s arrives", roman[d], item);
        int at = (int)(strstr(t, item) - t), len = (int)strlen(item);
        for (int i = 0; i < n && i < 200; i++) col[i] = i >= at && i < at + len ? CHAT_PROGRESSION : CHAT_TEXT;
        ap_chat_add(t, col);
    }
    printf("ap: check %lld held (chapter %d is locked)\n", (long long)location, c);
    fflush(stdout);
}

/* Chapter Select shows only the chapters AP gave (with chapter blocking, while connected).
 * Statuses: per difficulty k (0..4) one per ChapterData chapter c at save +0x238 + 0x804 * k + 0x4c * c (0 not reached,
 * 1 reached, 2 open, 3 cleared on that difficulty). The card menu (0x7100b2aa80) counts the entries of the chapter table
 * [0x710186d298] (24 ChapterData numbers) that 0x7100d96244(save, c) calls open (status 2 or 3 in any difficulty) and
 * shows that many cards, card i = table[i]; 0x7100b2aca0 finds the card of a chapter. So here the table lists the given
 * chapters first, 0x7100d96244 answers "given" (ap_chapter_given, wrapped by scripts/ap_patch.py) and a card index past
 * them is the last given one (ap_card_index). Given: Records of Time and the Prologue (ChapterData 0, 1), the starting
 * chapter and every Chapter Unlock (shown chapter d = ChapterData d + 1). Given chapters are also made open (2) in each
 * difficulty already played. */
#define CHAPTER_TABLE 0x710186d298ull
static uint32_t g_given;                              /* ChapterData bits; 0 = the game's own Chapter Select */
static int g_given_n;
static uint32_t g_card_table[24];
static uint64_t g_game_table;                         /* the game's own table (put back when blocking is off) */

static void chapters_given_frame(uint64_t save) {
    uint32_t given = 0;
    if (ap_net_slot_int("chapter_blocking", 0)) {
        int from = 0, total = 0;
        ApItem it[256];
        given = 1u << 0 | 1u << 1 | 1u << (ap_net_slot_int("starting_chapter", 1) + 1);
        do {
            int k = ap_net_items(from, it, 256, &total);
            if (k <= 0) break;
            for (int i = 0; i < k; i++)
                if (it[i].item >= 220500 && it[i].item < 220516) given |= 1u << (it[i].item - 220500 + 2);
            from += k;
        } while (from < total);
    }
    uint64_t t = 0;
    memcpy(&t, (void *)(uintptr_t)CHAPTER_TABLE, 8);
    if (t && t != (uint64_t)(uintptr_t)g_card_table) g_game_table = t;
    if (!given || !g_game_table) {                    /* the game's own Chapter Select */
        g_given = 0;
        if (g_game_table && t == (uint64_t)(uintptr_t)g_card_table) memcpy((void *)(uintptr_t)CHAPTER_TABLE, &g_game_table, 8);
        return;
    }
    if (given != g_given) {
        uint32_t game[24];
        int n = 0;
        memcpy(game, (void *)(uintptr_t)g_game_table, sizeof game);
        /* the menu counts entries 0..22 (the 24th ends the list): given story chapters, then the game's own bonus
         * chapters (22..26, open as the game says), then the story chapters not given */
        for (int i = 0; i < 23; i++) if (game[i] < 18 && given & 1u << game[i]) g_card_table[n++] = game[i];
        g_given_n = n;
        for (int i = 0; i < 23; i++) if (game[i] >= 18) g_card_table[n++] = game[i];
        for (int i = 0; i < 23; i++) if (game[i] < 18 && !(given & 1u << game[i])) g_card_table[n++] = game[i];
        g_card_table[23] = game[23];
        g_given = given;
        printf("ap: Chapter Select: %d given chapters (mask %x)\n", g_given_n, given);
        fflush(stdout);
    }
    uint64_t mine = (uint64_t)(uintptr_t)g_card_table;
    if (t != mine) memcpy((void *)(uintptr_t)CHAPTER_TABLE, &mine, 8);
    if (!save) return;
    for (int k = 0; k < 5; k++) {
        uint64_t b = save + 0x238 + 0x804 * k;
        if (!rd32h(b)) continue;                      /* a difficulty never played stays as it is */
        for (int c = 0; c < 18; c++)
            if (given & 1u << c && rd32h(b + 0x4c * c) < 2) wr32h(b + 0x4c * c, 2);
    }
}

/* 0x7100d96244(save, chapter) (scripts/ap_patch.py): 1 / 0 = given or not, -1 = ask the game */
EXPORT int ap_chapter_given(uint32_t c) { return g_given && c < 18 ? (g_given >> c & 1) : -1; }

/* 0x7100b2aca0(.., chapter) -> card index (scripts/ap_patch.py): the card of a story chapter not given -> the last given one */
EXPORT int ap_card_index(int i) {
    if (!g_given || g_given_n <= 0 || i < 0 || i >= 23) return i;
    uint32_t c = g_card_table[i];
    return c < 18 && !(g_given >> c & 1) ? g_given_n - 1 : i;
}

/* 0x7100b2aa80(menu, chapter) (scripts/ap_patch.py): the card menu opens on chapter c (CONTINUE's after a clear). One not
 * given would get the game's "new chapter" step (it opens table[card] and adds a card), which breaks the given cards
 * until the menu is built again: open on the last given chapter before it instead. */
EXPORT int ap_chapsel_start(int c) {
    if (!g_given || c < 0 || c >= 18 || g_given >> c & 1) return c;
    while (c > 0 && !(g_given >> c & 1)) c--;
    printf("ap: Chapter Select opens on chapter %d (the one it would open is not given)\n", c);
    fflush(stdout);
    return c;
}

/* the title menu (ap_ui.c ap_menu_frame), twice a second: Chapter Select is built from the save before any stage runs */
void ap_title_frame(void) {
    static int tick;
    uint64_t save;
    if (++tick % 30) return;
    memcpy(&save, (void *)(uintptr_t)0x71018668d0ull, 8);
    chapters_given_frame(save);
}

static void held_frame(void) {                    /* the held checks whose chapter is open now */
    static ULONGLONG last;
    if (!g_file[0] || GetTickCount64() - last < 2000) return;
    last = GetTickCount64();
    char keys[4096];
    DWORD n = GetPrivateProfileStringA("held", NULL, "", keys, sizeof keys, g_file);
    for (char *k = keys; n && *k; k += strlen(k) + 1) {
        int c = GetPrivateProfileIntA("held", k, -1, g_file);
        if (!chapter_open(c)) continue;
        ap_net_check(atoll(k));
        WritePrivateProfileStringA("held", k, NULL, g_file);
    }
}

/* ---------------------------------------------------------------------------------------------- items */

/* apworld consumable (Items.py CONSUMABLES order) -> game item ID: the game's item table [0x710186ee40], 40-byte
 * records {id, ...; ITM_ITEM_NAME_nn hash} (Green Herb 3, Mega Green Herb 4, Purple Magic 5, ... Broken Moon Pearl 18) */
static const int k_consumable[] = {3, 5, 7, 9, 4, 6, 8, 10, 14, 2, 0, 1};
static const uint32_t k_halos[] = {137, 500, 1000, 2500, 5000, 10000, 20000, 50000, 100000};
#define ID_HEART_PIECE 16
#define ID_PEARL_PIECE 18

static uint32_t item_count(uint64_t save, int id) { return rd32h(save + SAVE_ITEMS + 8u * (unsigned)id); }

static void add_item_count(uint64_t save, int id, int n) {
    uint64_t a = save + SAVE_ITEMS + 8u * (unsigned)id;
    int v = (int)rd32h(a) + n;
    wr32h(a, (uint32_t)(v > 99 ? 99 : v < 0 ? 0 : v));
    uint64_t seen = save + SAVE_ITEM_SEEN + 4u * (unsigned)(id >> 5);
    wr32h(seen, rd32h(seen) | 0x80000000u >> (id & 31));
}

/* Broken Witch Hearts / Moon Pearls held = the number of bits in their word (the Items page shows it modulo 4 / 2;
 * a full set reads 0/4 again). While connected the words are exactly the pieces received from AP, rewritten every
 * frame, so a vanilla pickup (which sets a bit) gives nothing.
 * shortcut: whether max HP / magic follow the bit count or a separate counter (+0xa4d4?) is not verified yet. */
static void set_pieces(uint64_t save, int id, uint32_t bits, int n) {
    if (n > 32) n = 32;
    uint32_t want = n ? ~0u << (32 - n) : 0;
    if (rd32h(save + bits) != want) wr32h(save + bits, want);
    if (n && item_count(save, id) != (uint32_t)n) add_item_count(save, id, n - (int)item_count(save, id));
}

#define PLAYERS 0x71018668f8ull                 /* [this] + 48 = Bayonetta (+ 72 = the partner) */
#define PL_HP 0xcedc                           /* current HP; + 4 = max HP */

/* "": trap 10 (Nemesis) was dropped; its ID stays unused */
static const char *const k_trap_name[15] = {"Pickpocket", "Bloodletting", "Fragile Witch", "Magic Drain", "Amnesia",
    "Sticky Fingers", "Squish", "Squash", "Angel Ambush", "Grace & Glory", "", "Alfheim Curse", "Berserk",
    "Gracious & Glorious", "Fairness & Fearless"};
static ULONGLONG g_amnesia_until;                /* the Amnesia trap: techniques forgotten until then */
static int g_shape;                              /* Squish (1) / Squash (2): buttons held off until then (nnhid.c) */
static ULONGLONG g_shape_until;
static int g_curse_hits;                         /* Alfheim Curse: hits left until HP 1, until then */
static ULONGLONG g_curse_until, g_berserk_until; /* Berserk: hits taken count double until then */
static int g_hp_set;                             /* HP changed by a trap / link: not a hit (traps_frame) */
/* player +0x10 world matrix, +0x50 position, +0x60 rotation, +0x70 scale: the scale only reaches the hair (the body's
 * matrix 0x710095b06c leaves it out, and scaling that matrix moved her instead) - so no size traps */

static uint64_t player(void) {
    uint64_t m;
    memcpy(&m, (void *)(uintptr_t)PLAYERS, 8);
    if (!m) return 0;
    uint64_t p;
    memcpy(&p, (void *)(uintptr_t)(m + 48), 8);
    return p;
}

/* ---------------------------------------------------------------------------------------------- enemy spawns */

/* Enemies appear through the game's own object request 0x71009a7740(manager [0x71018668d8], name, ID, set info),
 * the call the stages' scripts make (ap_patch.py GAME block, bit 4 of ap_game_frame's result): it loads the
 * enemy's files if needed, creates it from the factory table (0x71017d7da0: {ID, create, flags, name}; enemy IDs
 * 0x20000 + n = em%04x) and keeps the set-info pointer, so each request gets its own block that stays. Set info
 * (0x78 bytes, as the game's player spawn builds it): +0x10 / +0x20 / +0x30 rotation rows, +0x40 position, +0x58
 * scale, +0x64 -1. Bayonetta's position: player +0x40. */
#define SPAWN_SLOTS 16
static struct { uint32_t id; int at; float pos[3]; } g_spawn_q[SPAWN_SLOTS];   /* at: made at pos (else beside Bayonetta) */
static int g_spawn_n;
static ULONGLONG g_spawn_next;                                      /* one request every 0.4 s */

static int spawn_enemy(uint32_t id) {
    if (g_spawn_n >= SPAWN_SLOTS) return 0;
    g_spawn_q[g_spawn_n].id = id; g_spawn_q[g_spawn_n].at = 0;
    g_spawn_n++;
    return 1;
}

int spawn_object_request(uint32_t id) { return spawn_enemy(id); }   /* story co-op: the partner (coop_game.c) */

int spawn_object_at(uint32_t id, const float pos[3]) {              /* story co-op: the other game's enemies (coop_enemy.c) */
    if (g_spawn_n >= SPAWN_SLOTS) return 0;
    g_spawn_q[g_spawn_n].id = id; g_spawn_q[g_spawn_n].at = 1;
    memcpy(g_spawn_q[g_spawn_n].pos, pos, 12);
    g_spawn_n++;
    return 1;
}

EXPORT int ap_spawn_take(uint32_t *id, uint64_t *name, uint64_t *setinfo) {
    static uint8_t info[SPAWN_SLOTS][0x80];                        /* the game keeps these pointers */
    static char names[SPAWN_SLOTS][8];
    static int slot;
    uint64_t pl = player();
    if (!g_spawn_n || !pl || GetTickCount64() < g_spawn_next) return 0;
    int at = g_spawn_q[0].at;
    float at_pos[3]; memcpy(at_pos, g_spawn_q[0].pos, 12);
    g_spawn_next = GetTickCount64() + (at ? 150 : 400);
    *id = g_spawn_q[0].id;
    memmove(g_spawn_q, g_spawn_q + 1, --g_spawn_n * sizeof g_spawn_q[0]);
    slot = (slot + 1) % SPAWN_SLOTS;
    uint8_t *s = info[slot];
    float f[0x20] = {0};
    f[4] = f[9] = f[14] = 1.0f;                                     /* rotation rows */
    memcpy(&f[16], (void *)(uintptr_t)(pl + 0x40), 12);           /* at Bayonetta, a few steps aside */
    int k = (int)(GetTickCount() / 7 % 4);
    f[16] += (k & 1 ? 4.0f : -4.0f); f[18] += (k & 2 ? 4.0f : -4.0f);
    if (at) memcpy(&f[16], at_pos, 12);
    {   /* TEST ONLY: RECOMPS_AP_TEST_SPAWN_AT=<x>,<y>,<z>: the same place in every game (co-op tests) */
        const char *e = getenv("RECOMPS_AP_TEST_SPAWN_AT");
        if (e && !at) sscanf(e, "%f,%f,%f", &f[16], &f[17], &f[18]);
    }
    f[22] = f[23] = f[24] = 1.0f;                                   /* scale */
    memcpy(s, f, 0x80);
    wr32h((uint64_t)(uintptr_t)s + 0x64, ~0u);
    {   /* TEST ONLY: RECOMPS_AP_TEST_SPAWN_SI=<offset hex>=<word hex>[,...]: words of the set info */
        const char *e = getenv("RECOMPS_AP_TEST_SPAWN_SI");
        for (const char *q = e; q && *q; ) {
            unsigned off, val;
            if (sscanf(q, "%x=%x", &off, &val) != 2 || off > 0x7c) break;
            memcpy(s + off, &val, 4);
            q = strchr(q, ','); if (q) q++;
        }
    }
    {   /* the type's prefix (the factory's type table, 0x71017c0530): 0x1xxxx pl, 0x2xxxx em, 0x3xxxx wp */
        static const char *const pre[] = {"--", "pl", "em", "wp", "et", "ef", "es", "it", "bm", "bh", "ba"};
        unsigned t = *id >> 16;
        snprintf(names[slot], sizeof names[slot], "%s%04x", t < 11 ? pre[t] : "em", *id & 0xffff);
    }
    *name = (uint64_t)(uintptr_t)names[slot];
    *setinfo = (uint64_t)(uintptr_t)s;
    printf("ap: spawn %05x (%s) at %.1f %.1f %.1f\n", *id, names[slot], f[16], f[17], f[18]);
    fflush(stdout);
    return 1;
}

static uint64_t g_spawn_req[8];
EXPORT void ap_spawned(uint64_t request) {
    printf("ap: spawn request %llx\n", (unsigned long long)request); fflush(stdout);
    for (int i = 0; i < 8; i++) if (!g_spawn_req[i]) { g_spawn_req[i] = request; break; }
}

static void spawn_watch(void) {                      /* TEST ONLY (RECOMPS_AP_TEST_SPAWN): each request's words as they change */
    static uint32_t last[8][24];
    for (int i = 0; i < 8 && g_spawn_req[i]; i++) {
        uint32_t now[24];
        memcpy(now, (void *)(uintptr_t)g_spawn_req[i], sizeof now);
        if (memcmp(now, last[i], sizeof now)) {
            printf("ap: request %d:", i);
            for (int k = 0; k < 24; k++) printf(" %x", now[k]);
            printf("\n"); fflush(stdout);
            memcpy(last[i], now, sizeof now);
        }
    }
}

/* traps (Items.py TRAPS order, 220600..). 0 = cannot now (no player): it waits for the next frame.
 * The enemy traps (8, 9, 10, 13, 14) spawn enemies beside Bayonetta. */
static int trap(uint64_t save, int k) {
    uint64_t pl = player();
    switch (k) {
    case 1:                                                      /* Bloodletting: half the HP, never below 1 */
    case 2: {                                                    /* Fragile Witch: 1 HP */
        if (!pl) return 0;
        uint32_t hp = rd32h(pl + PL_HP);
        wr32h(pl + PL_HP, k == 1 ? (hp / 2 ? hp / 2 : 1) : 1);
        g_hp_set = 1;
        break;
    }
    case 0: {                                                    /* Pickpocket: up to 10,000 Halos */
        uint32_t h = rd32h(save + SAVE_HALOS);
        wr32h(save + SAVE_HALOS, h > 10000 ? h - 10000 : 0);
        break;
    }
    case 5: {                                                    /* Sticky Fingers: up to 3 of one consumable you have */
        int have[sizeof k_consumable / sizeof k_consumable[0]], n = 0;
        for (int i = 0; i < (int)(sizeof k_consumable / sizeof k_consumable[0]); i++)
            if (item_count(save, k_consumable[i]) > 0) have[n++] = k_consumable[i];
        if (n) add_item_count(save, have[GetTickCount() % n], -3);
        break;
    }
    case 3: {                                                    /* Magic Drain: the magic gauge empty */
        float zero = 0;
        memcpy((void *)(uintptr_t)(save + 0xa53c), &zero, 4);
        break;
    }
    case 4:                                                      /* Amnesia: only the starting techniques, 60 s */
        g_amnesia_until = GetTickCount64() + 60000;
        break;
    case 6:                                                      /* Squish: flattened - no jump or evade, 20 s */
    case 7:                                                      /* Squash: pinned - no punch or kick, 15 s */
        if (!pl) return 0;
        g_shape = k == 6 ? 1 : 2;
        g_shape_until = GetTickCount64() + (k == 6 ? 20000 : 15000);
        break;
    case 11:                                                     /* Alfheim Curse: 3 hits in 30 s and HP 1 */
        g_curse_hits = 3;
        g_curse_until = GetTickCount64() + 30000;
        break;
    case 12:                                                     /* Berserk: every hit counts double, 30 s */
        g_berserk_until = GetTickCount64() + 30000;
        break;
    /* The enemy traps: B1's spawns, through the game's own object request (spawn_enemy). B2 keeps B1's IDs for
     * Affinity (0x20000) and Fearless / Fairness (0x20050 / 0x20051); Grace / Glory (B1 0x20040 / 0x20041) and
     * Gracious / Glorious do not spawn in B2 (0x20040 is removed at once, the others are not in the factory), so those
     * traps send B2 duos instead. Nemesis (B1: Jeanne) is left out: B2 has no Jeanne enemy. */
    case 8:                                                      /* Angel Ambush: three Affinities */
        if (!pl) return 0;
        for (int i = 0; i < 3; i++) spawn_enemy(0x20000);
        break;
    case 9:                                                      /* Grace & Glory: a demon duo (one class, two IDs) */
        if (!pl) return 0;
        spawn_enemy(0x20030); spawn_enemy(0x20031);
        break;
    case 13:                                                     /* Gracious & Glorious: an angel duo */
        if (!pl) return 0;
        spawn_enemy(0x20060); spawn_enemy(0x20061);
        break;
    case 14:                                                     /* Fairness & Fearless */
        if (!pl) return 0;
        spawn_enemy(0x20051); spawn_enemy(0x20050);
        break;
    default:
        printf("ap: trap %d is not handled yet\n", k);
    }
    return 1;
}

/* 0 = not now (it waits, and so do the items after it) */
static int apply_spendable(uint64_t save, int64_t item) {
    int64_t k = item - ITEM_BASE;
    if (k >= 600 && k < 700) {                                     /* a trap from AP: Trap Link passes it on */
        int done = trap(save, (int)(k - 600));
        if (done && k - 600 < 15) ap_net_link_send(AP_LINK_TRAP, 0, k_trap_name[k - 600]);
        return done;
    }
    if (k >= 300 && k < 300 + (int64_t)(sizeof k_consumable / sizeof k_consumable[0])) add_item_count(save, k_consumable[k - 300], 1);
    else if (k >= 450 && k < 450 + (int64_t)(sizeof k_halos / sizeof k_halos[0])) {
        uint64_t h = (uint64_t)rd32h(save + SAVE_HALOS) + k_halos[k - 450];
        wr32h(save + SAVE_HALOS, (uint32_t)(h > HALOS_MAX ? HALOS_MAX : h));
    } else if (k == 400 || k == 401 || (k >= 100 && k < 169)) ;  /* pieces, accessories, techniques: kept by state */
    else if (k < 100 || (k >= 200 && k < 300) || (k >= 500 && k < 600) || k == 700) ;   /* weapons, LPs, unlocks, fragments:
                                                                 read from the received list where they matter */
    else { printf("ap: item %lld is not handled yet\n", (long long)item); fflush(stdout); }
    return 1;
}

/* ---------------------------------------------------------------------------------------------- locations */

/* Verses: the game names each by CRC32(lowercase name) & 0x7fffffff; ap_verses.txt (made from the player's
 * ChapterData by ap/tools/build_mod.py, in the Archipelago mod folder) gives its chapter and number. */
typedef struct { uint32_t hash; int chapter, number; } Verse;
static Verse *g_verses;
static int g_verses_n = -1;

static FILE *open_mod_file(const char *name, char *path, size_t n) {   /* <mods>\Archipelago\<name> */
    char exe[MAX_PATH];
    const char *mods = getenv("SSBU_MODS");
    if (mods && *mods && strcmp(mods, "none")) snprintf(path, n, "%s\\Archipelago\\%s", mods, name);
    else {
        GetModuleFileNameA(NULL, exe, sizeof exe);
        char *p = strrchr(exe, '\\'); if (p) *p = 0;
        snprintf(path, n, "%s\\..\\mods\\Archipelago\\%s", exe, name);
    }
    return fopen(path, "rb");
}

static void load_verses(void) {
    if (g_verses_n >= 0) return;
    g_verses_n = 0;
    char path[MAX_PATH];
    FILE *f = open_mod_file("ap_verses.txt", path, sizeof path);
    if (!f) { printf("ap: no verse table (%s): verse checks are off\n", path); fflush(stdout); return; }
    unsigned h; int c, n, cap = 0;
    while (fscanf(f, "%x %d %d", &h, &c, &n) == 3) {
        if (g_verses_n == cap) { Verse *v = realloc(g_verses, (cap = cap ? cap * 2 : 256) * sizeof *v); if (!v) break; g_verses = v; }
        g_verses[g_verses_n++] = (Verse){h, c, n};
    }
    fclose(f);
}

/* The game stores a verse's result (0x7100d6f104 -> 0x7100d93fb4; ap_patch.py): that verse is done. */
/* The verse's overall medal, read when the verse ends: its result record [0x71018700f0] (filled by 0x7100d7010c just
 * before) has the category medals at +0..+2 and the overall medal at +3, taken from the game's overall-medal table
 * [0x7101870180] (6 entries of 12 bytes {points limit, medal, ?}, best first: Pure Platinum .. Stone). The medal's
 * place in that table gives 5 (Pure Platinum) .. 1 (Bronze), 0 (Stone or none). */
static int verse_medal(void) {
    uint64_t rec = 0, tab = 0;
    memcpy(&rec, (void *)(uintptr_t)0x71018700f0ull, 8);
    memcpy(&tab, (void *)(uintptr_t)0x7101870180ull, 8);
    if (!rec || !tab) return 0;
    uint8_t m = *(uint8_t *)(uintptr_t)(rec + 3);
    for (int k = 0; k < 5; k++) if (rd32h(tab + 12 * k + 4) == m) return 5 - k;
    return 0;
}

EXPORT void ap_verse_end(uint32_t hash) {
    load_verses();
    if (getenv("RECOMPS_AP_TEST_RANK")) {   /* TEST ONLY: the verse's result record and the overall-medal table */
        uint64_t rec = 0, tab = 0;
        memcpy(&rec, (void *)(uintptr_t)0x71018700f0ull, 8);
        memcpy(&tab, (void *)(uintptr_t)0x7101870180ull, 8);
        if (rec) printf("ap_rank: record %08x time %u score %u +12 %u +16 %u\n", rd32h(rec), rd32h(rec + 4), rd32h(rec + 8), rd32h(rec + 12), rd32h(rec + 16));
        for (int k = 0; tab && k < 6; k++) printf("ap_rank: tier %d: %d %d %d\n", k, (int)rd32h(tab + 12 * k), (int)rd32h(tab + 12 * k + 4), (int)rd32h(tab + 12 * k + 8));
        fflush(stdout);
    }
    for (int i = 0; i < g_verses_n; i++) {
        if (g_verses[i].hash != hash) continue;
        int c = g_verses[i].chapter, v = g_verses[i].number;
        printf("ap: verse %d of chapter %d done\n", v, c);
        fflush(stdout);
        if (c >= 0 && c < 18) {
            chapter_check(VERSE_BASE + c * 32 + v, c);
            int medal = verse_medal();                         /* 0 none / Stone, 1 Bronze .. 5 Pure Platinum */
            printf("ap: verse medal %d\n", medal);
            fflush(stdout);
            for (int t = 0; t <= medal && medal > 0; t++)      /* tier 0 "Any Medal", tiers 1..medal "Bronze+".. */
                chapter_check(RANK_BASE + t * 1000 + c * 32 + v, c);
        }
        else if (c == 20) {                                      /* Tag Climax stages and Witch Trials */
            ap_net_check(TRIAL_BASE + v);
            int medal = verse_medal();
            for (int t = 0; t <= medal && medal > 0; t++) ap_net_check(RANK_BASE + t * 1000 + 20 * 32 + v);
        }
        return;
    }
    printf("ap: verse %08x is not in the table\n", hash);
    fflush(stdout);
}

/* Chests: opening one sets bit No (MSB first) of save +0xadd0, the current chapter's chest word (cleared when a
 * chapter starts). The chapter is the checkpoint phase's (save +0xa7c8) in ap_phases.txt (ChapterData, build_mod.py).
 * shortcut: only chest No 0..31 (+0xadd0); every story chest the apworld lists is below 32. */
#define SAVE_PHASE 0xa7c8
#define SAVE_CHESTS 0xadd0
#define CHEST_BASE 223000

static int phase_chapter(uint32_t phase) {
    static int n = -1;
    static struct { uint32_t phase; int chapter; } t[256];
    if (n < 0) {
        n = 0;
        char path[MAX_PATH];
        FILE *f = open_mod_file("ap_phases.txt", path, sizeof path);
        if (!f) { printf("ap: no phase table (%s): chest checks are off\n", path); fflush(stdout); return -1; }
        while (n < 256 && fscanf(f, "%x %d", &t[n].phase, &t[n].chapter) == 2) n++;
        fclose(f);
    }
    for (int i = 0; i < n; i++) if (t[i].phase == phase) return t[i].chapter;
    return -1;
}

static void check_chests(uint64_t save) {
    static int chapter = -1, loaded;
    static uint32_t seen;
    int c = phase_chapter(rd32h(save + SAVE_PHASE));
    uint32_t now = rd32h(save + SAVE_CHESTS);
    if (c < 0) return;
    if (!loaded) { loaded = 1; chapter = c; seen = 0; }      /* the save as loaded: its opened chests count too */
    else if (c != chapter || (now & seen) != seen) { chapter = c; seen = now; return; }   /* new chapter / cleared */
    uint32_t fresh = now & ~seen;
    seen = now;
    for (int no = 0; fresh && no < 32; no++)
        if (fresh & 0x80000000u >> no) {
            printf("ap: chest %d of chapter %d opened\n", no, c);
            fflush(stdout);
            if (c >= 0 && c < 18) chapter_check(CHEST_BASE + c * 64 + no, c);
        }
}

/* Collectibles kept as save bitsets (MSB first), each bit a location; what the save already holds when it loads
 * counts too (checks repeat harmlessly). */
static const struct { uint32_t off; int first, count, base; } k_bitsets[] = {
    {0xa824, 0, 20, 224000},                  /* Umbran Crows, bit = the crow's UniqueId (SetCrow tables) */
    {0xa820, 0, 29, 224100},                  /* books: bit = the book's Type (SetBook tables); reading one: a hint */
    {0x1b0, 0, 34, 224200},                   /* Umbran Tears of Blood (Bewitchments): 0x7100d9045c sets them (the shop
                                                 calls it with 11, "Prepared for Anything") */
};

static const int k_crow_chapter[20] = {2, 2, 2, 3, 5, 6, 6, 6, 8, 9, 9, 10, 10, 11, 11, 14, 14, 14, 16, 17};
static const int k_book_chapter[29] = {2, -1, 2, -1, -1, 2, -1, 2, 2, 2, 2, 3, 3, 5, 6, 6, 6, 6, 8, 9, 9, 9, 9, 11, 11, 14, 14,
                                       15, 17};   /* -1: no book of that Type in the stages */

static void check_bitsets(uint64_t save) {
    static uint8_t seen[sizeof k_bitsets / sizeof k_bitsets[0]][64];
    static int based[sizeof k_bitsets / sizeof k_bitsets[0]];   /* 1 after the first look (what the save held) */
    for (int s = 0; s < (int)(sizeof k_bitsets / sizeof k_bitsets[0]); s++)
        for (int i = k_bitsets[s].first; i < k_bitsets[s].first + k_bitsets[s].count; i++) {
            int on = (rd32h(save + k_bitsets[s].off + 4u * (unsigned)(i >> 5)) & 0x80000000u >> (i & 31)) != 0;
            int k = i - k_bitsets[s].first;
            if (on && !seen[s][k]) {
                seen[s][k] = 1;
                printf("ap: collectible %d of set %x\n", i, k_bitsets[s].off);
                fflush(stdout);
                if (s == 0) chapter_check(k_bitsets[s].base + i, k_crow_chapter[i]);   /* a crow: its chapter */
                else if (s == 1) {
                    if (k_book_chapter[i] >= 0) chapter_check(k_bitsets[s].base + i, k_book_chapter[i]);
                    if (based[s]) ap_net_hint();                   /* read just now: a hint (books read before: none) */
                } else ap_net_check(k_bitsets[s].base + i);
            }
        }
    for (int s = 0; s < (int)(sizeof k_bitsets / sizeof k_bitsets[0]); s++) based[s] = 1;
}

/* The game commits a chapter's results (0x7100d94274(save, chapter, ...): Chapter Select replays and the like). */
EXPORT void ap_chapter_done(uint64_t save, int chapter) {
    (void)save;
    printf("ap: chapter %d complete\n", chapter);
    fflush(stdout);
    if (chapter >= 0 && chapter < 18) chapter_check(CHAPTER_BASE + chapter, chapter);
}

/* ---------------------------------------------------------------------------------------------- Gates of Hell */

/* Accessory m (1..14): bit m + 1 (MSB first; bit 7 = Gaze of Despair, 06) of save +0xa478 owned, +0xa47c its "new" mark (0x7100d96d44, the shop's
 * purchase). Technique: bit <technique ID> of +0xa4d8 (0x7100d90908). The ID is not the TEC_NAME_SP_nn of its name:
 * 0x71008cf93c(id) picks the name (IDs 3 / 4 / 26 by character: Bayonetta's Crow Within / Bat Within / Umbran Spear,
 * Jeanne's Owl / Moth, Rosa's Falcon / Hornet); the Gates of Hell lists IDs 28 21 10 12 8 9 17 19 18 15 16 27 4 3 26
 * (0x7100b39b74). A bit that turns on without AP having given that item is a purchase: its Gates of Hell location
 * (225100 + k, 225200 + tec_location). While connected those bits are exactly what AP gave; the other bits of the word
 * (story flags) are left alone. */
#define SAVE_ACCESSORIES 0xa478
#define SAVE_ACCESSORIES_NEW 0xa47c
#define SAVE_TECHNIQUES 0xa4d8
#define SHOP_BASE 225000
/* Items.py TECHNIQUES -> technique ID. Moth / Owl (Jeanne's names) are in no pool; the first seeds had Hornet / Falcon in
 * place of Stiletto / Heel Slide, so those give Stiletto / Heel Slide. */
static const int k_tec_no[19] = {10, 12, 8, 9, 17, 19, 18, 15, 16, 27, 4, 3, 4, 3, 10, 12, 26, 28, 21};
/* technique ID -> its location (225200 + n): the numbers the first seeds gave each name, Stiletto / Heel Slide on the
 * Hornet / Falcon ones, After Burner Kick new (206) */
static int tec_location(int id) {
    static const int8_t loc[32] = {[3] = 11, [4] = 10, [8] = 2, [9] = 3, [10] = 16, [12] = 17, [15] = 7, [16] = 8, [17] = 4,
                                   [18] = 6, [19] = 5, [21] = 20, [26] = 12, [27] = 9, [28] = 19};
    return id > 0 && id < 32 && loc[id] ? loc[id] : -1;
}

static void shop_frame(uint64_t save, uint32_t accessories, uint32_t techniques, int connected) {
    static int based;
    static uint32_t prev_acc, prev_tec;
    uint32_t acc_mask = 0, tec_mask = 0;
    for (int k = 1; k <= 14; k++) acc_mask |= 0x80000000u >> (k + 1);
    for (int i = 0; i < 32; i++) if (tec_location(i) >= 0) tec_mask |= 0x80000000u >> i;
    uint32_t acc = rd32h(save + SAVE_ACCESSORIES), tec = rd32h(save + SAVE_TECHNIQUES);
    if (!based) { based = 1; prev_acc = 0; prev_tec = 0; }       /* what the save holds already counts too */
    uint32_t bought = acc & ~prev_acc & acc_mask & ~accessories, learned = tec & ~prev_tec & tec_mask & ~techniques;
    for (int k = 1; k <= 14; k++)
        if (bought & 0x80000000u >> (k + 1)) { printf("ap: accessory %d bought\n", k); ap_net_check(SHOP_BASE + 100 + k); }
    for (int n = 0; n < 32; n++)
        if (learned & 0x80000000u >> n) { printf("ap: technique %d learned\n", n); ap_net_check(SHOP_BASE + 200 + tec_location(n)); }
    if (bought | learned) fflush(stdout);
    if (connected) {
        if (GetTickCount64() < g_amnesia_until) techniques = 0;      /* the Amnesia trap */
        /* only the groups the seed randomizes (slot_data; older seeds: all); the others stay as bought */
        if (!ap_net_slot_int("include_accessories", 1)) acc_mask = 0;
        if (!ap_net_slot_int("include_techniques", 1)) tec_mask = 0;
        uint32_t want_acc = (acc & ~acc_mask) | accessories, want_tec = (tec & ~tec_mask) | techniques;
        if (want_acc != acc) {                                   /* newly given ones get the menu's "new" mark */
            wr32h(save + SAVE_ACCESSORIES, want_acc);
            wr32h(save + SAVE_ACCESSORIES_NEW, rd32h(save + SAVE_ACCESSORIES_NEW) | (want_acc & ~acc));
        }
        if (want_tec != tec) wr32h(save + SAVE_TECHNIQUES, want_tec);
        acc = want_acc; tec = want_tec;
    }
    prev_acc = acc; prev_tec = tec;
}

/* Weapons: bit i (MSB first) of save +0xa46c = weapon i owned, +0xa474 its "new" mark (the game's grant 0x7100d96c68;
 * the subscreen's list 0x7100b3e7e0 reads them). Weapon i for Bayonetta (names 0x71008ca1a4): 0 Love Is Blue,
 * 1 Rakshasa, 2 Takemikazuchi, 3 Alruna, 4 Kafka, 5 Chernobog, 6 Chain Chomp, 7 Undine, 8 Salamandra, 11 Arwing,
 * 13 Scarborough Fair, 14 Shuraba, 15 Rodin, 17 Mistelteinn. While connected the AP weapons are exactly what AP gave;
 * Love Is Blue and the Nintendo weapons stay as the game has them.
 * shortcut: Unforgiven (ITM_WEAPON_NAME_03_01) is not one of Bayonetta's: not given; Mistelteinn as 17 is unverified. */
#define SAVE_WEAPONS 0xa46c
#define SAVE_WEAPONS_NEW 0xa474
static const int k_weapon_bit[13] = {-1, 4, 1, 2, 5, 3, 8, 7, 13, 14, 15, -1, 17};   /* Items.py WEAPONS order */

static void weapons_frame(uint64_t save, uint32_t given) {
    if (!ap_net_slot_int("include_weapons", 1)) return;          /* not randomized: the game's own Golden LPs */
    uint32_t mask = 0;
    for (int i = 0; i < 13; i++) if (k_weapon_bit[i] >= 0) mask |= 0x80000000u >> k_weapon_bit[i];
    uint32_t cur = rd32h(save + SAVE_WEAPONS), want = (cur & ~mask) | (given & mask);
    if (want != cur) {
        wr32h(save + SAVE_WEAPONS, want);
        wr32h(save + SAVE_WEAPONS_NEW, rd32h(save + SAVE_WEAPONS_NEW) | (want & ~cur));
    }
}

/* ---------------------------------------------------------------------------------------------- item popups */

/* The game's "Received a Bewitchment." box (HUD info, top right): header COM_HUD_INFO_02 (mmCOM001), second line
 * COM_ACHIEVE_nn (mmACV000), shown by setting bit nn of the queue [[0x710186dc88]] (0x7100b5ebfc picks it up when
 * the player is free). For an AP popup the two message names are pointed at the mod's own messages for a while
 * (AP_HEAD_RECV / _SENT / _TRAP in mmCOM001, the 64-character slot AP_POPUP_TEXT in mmACV000, ap/tools/build_mod.py),
 * then back. */
#define POPUP_QUEUE 0x710186dc88ull
#define POPUP_ACHIEVE 3                                  /* a Bewitchment number the queue shows in story play */
#define POPUP_FRAMES 420                                 /* the box is gone by then */

static uint8_t *event_of(uint64_t mcd, const char *name) {   /* the event record (40 bytes: +4 message, +8 name) */
    uint32_t ev = rd32h(mcd + 32), nev = rd32h(mcd + 36);
    for (uint32_t k = 0; k < nev; k++) {
        uint8_t *e = (uint8_t *)(uintptr_t)(mcd + ev + 40 * k);
        if (!strncmp((const char *)e + 8, name, 32)) return e;
    }
    return NULL;
}

static void write_message(uint64_t mcd, uint32_t msg, const char *text) {   /* font 20, one line, within its words */
    uint16_t sym[128];
    memset(sym, 0xff, sizeof sym);
    uint32_t so = rd32h(mcd + 8), ns = rd32h(mcd + 12);
    for (uint32_t k = 0; k < ns; k++) {
        uint16_t font, ch;
        memcpy(&font, (void *)(uintptr_t)(mcd + so + 8 * k), 2);
        memcpy(&ch, (void *)(uintptr_t)(mcd + so + 8 * k + 2), 2);
        if (font == 20 && ch < 128 && sym[ch] == 0xffff) sym[ch] = (uint16_t)k;
    }
    uint32_t para = rd32h(mcd + rd32h(mcd) + 16 * msg), line = rd32h(mcd + para), cap = rd32h(mcd + line + 8);
    uint8_t *words = (uint8_t *)(uintptr_t)(mcd + rd32h(mcd + line));
    uint32_t n = 0;
    for (const char *p = text; *p && n + 3 <= cap; p++) {
        uint16_t w[2] = {0x8001, 20};
        if (*p != ' ') {
            unsigned ch = (unsigned char)*p;
            w[0] = ch < 128 && sym[ch] != 0xffff ? sym[ch] : sym['?'];
            w[1] = 0;
            if (w[0] == 0xffff) continue;
        }
        memcpy(words + 2 * n, w, 4);
        n += 2;
    }
    uint16_t end = 0x8000;
    memcpy(words + 2 * n, &end, 2);
    n++;
    wr32h(mcd + line + 8, n);
    wr32h(mcd + line + 12, n);
}

/* The popup's icon: the medal of Bewitchment 3 in ui_core's texture 0. ap_icons.bin (ap/tools/popup_icons.py) holds
 * that texture's data with the item's own icon in place of the medal, one version per item (key 0: the AP logo); the
 * texture's data is found in the guest heap by a unique run of its bytes, the version written over it while the popup
 * is up (the renderer re-reads changed texture memory) and the original back after. */
static struct { uint8_t *file; uint32_t count, size, sig_at; uint8_t *tex, *orig; int loaded; } g_icons;

static void icons_load(void) {
    if (g_icons.loaded) return;
    g_icons.loaded = 1;
    char path[MAX_PATH];
    FILE *f = open_mod_file("ap_icons.bin", path, sizeof path);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    if (d && fread(d, 1, (size_t)n, f) == (size_t)n && n > 80 && !memcmp(d, "APIC", 4)) {
        memcpy(&g_icons.count, d + 4, 4); memcpy(&g_icons.size, d + 8, 4); memcpy(&g_icons.sig_at, d + 12, 4);
        if ((uint64_t)80 + (uint64_t)g_icons.count * (4 + g_icons.size) <= (uint64_t)n) g_icons.file = d;
    }
    if (!g_icons.file) free(d);
    fclose(f);
}

static uint8_t *icon_texture(void) {             /* the medal texture's data in the guest heap (NULL = not found) */
    const uint8_t *sig = g_icons.file + 16;
    if (g_icons.tex && !memcmp(g_icons.tex + g_icons.sig_at, sig, 64)) return g_icons.tex;
    void ap_heap(uint8_t **, uint64_t *);
    uint8_t *base; uint64_t size;
    ap_heap(&base, &size);
    g_icons.tex = NULL;
    for (uint8_t *p = base; p && p + 64 <= base + size; p++) {
        p = memchr(p, sig[0], (size_t)(base + size - 64 - p));
        if (!p) break;
        if (!memcmp(p, sig, 64) && p >= base + g_icons.sig_at) { g_icons.tex = p - g_icons.sig_at; break; }
    }
    return g_icons.tex;
}

static void icon_show(const ApPopup *pop) {      /* the item's icon on the medal (or the AP logo) */
    icons_load();
    uint8_t *tex = g_icons.file ? icon_texture() : NULL;
    printf("ap: icons %s (%u), medal texture %p\n", g_icons.file ? "loaded" : "missing", g_icons.count, (void *)tex);
    if (!tex) return;
    const uint8_t *pick = NULL, *ap = NULL;
    for (uint32_t i = 0; i < g_icons.count; i++) {
        const uint8_t *e = g_icons.file + 80 + (size_t)i * (4 + g_icons.size);
        uint32_t key;
        memcpy(&key, e, 4);
        if (key == 0) ap = e + 4;
        if (pop->ours && key == (uint32_t)pop->item) pick = e + 4;
    }
    if (!pick) pick = ap;
    if (!pick) return;
    if (!g_icons.orig) g_icons.orig = malloc(g_icons.size);
    if (!g_icons.orig) return;
    memcpy(g_icons.orig, g_icons.tex, g_icons.size);
    memcpy(g_icons.tex, pick, g_icons.size);
}

static void icon_restore(void) {
    if (g_icons.tex && g_icons.orig) memcpy(g_icons.tex, g_icons.orig, g_icons.size);
}

static struct { int state, timer; ApPopup cur; uint32_t head_was, text_was, head_now, text_now; } g_pop;
enum { POP_IDLE, POP_SHOW, POP_UP, POP_RESTORE };

static void popup_frame(void) {
    if (g_pop.state == POP_IDLE && player() && ap_net_popup(&g_pop.cur)) g_pop.state = POP_SHOW;
    else if (g_pop.state == POP_UP && ++g_pop.timer >= POPUP_FRAMES) g_pop.state = POP_RESTORE;
}

/* The game's message files of the popup (ap_patch.py looks them up when ap_game_frame asks): show / put back. */
EXPORT void ap_game_mcds(uint64_t acv, uint64_t com) {
    if (!acv || !com) return;
    char achieve[24];
    snprintf(achieve, sizeof achieve, "COM_ACHIEVE_%02d", POPUP_ACHIEVE);
    uint8_t *head = event_of(com, "COM_HUD_INFO_02"), *text = event_of(acv, achieve);
    uint8_t *mine = event_of(acv, "AP_POPUP_TEXT");
    static const char *k_heads[] = {"AP_HEAD_RECV", "AP_HEAD_SENT", "AP_HEAD_TRAP", "AP_HEAD_HINT"};
    uint8_t *h = event_of(com, k_heads[g_pop.cur.kind % 4]);
    if (!head || !text || !mine || !h) { g_pop.state = POP_IDLE; return; }     /* not the mod's files */
    if (g_pop.state == POP_SHOW) {
        write_message(acv, rd32h((uint64_t)(uintptr_t)mine + 4), g_pop.cur.text);
        g_pop.head_was = rd32h((uint64_t)(uintptr_t)head + 4);
        g_pop.text_was = rd32h((uint64_t)(uintptr_t)text + 4);
        g_pop.head_now = rd32h((uint64_t)(uintptr_t)h + 4);
        g_pop.text_now = rd32h((uint64_t)(uintptr_t)mine + 4);
        wr32h((uint64_t)(uintptr_t)head + 4, g_pop.head_now);
        wr32h((uint64_t)(uintptr_t)text + 4, g_pop.text_now);
        icon_show(&g_pop.cur);
        uint64_t q;
        memcpy(&q, (void *)(uintptr_t)POPUP_QUEUE, 8);
        if (q) wr32h(q, rd32h(q) | 0x80000000u >> POPUP_ACHIEVE);
        printf("ap: popup %d: %s\n", g_pop.cur.kind, g_pop.cur.text);
        fflush(stdout);
        g_pop.timer = 0;
        g_pop.state = POP_UP;
    } else if (g_pop.state == POP_RESTORE) {         /* only where our retarget still is (the file was not reloaded) */
        if (rd32h((uint64_t)(uintptr_t)head + 4) == g_pop.head_now) wr32h((uint64_t)(uintptr_t)head + 4, g_pop.head_was);
        if (rd32h((uint64_t)(uintptr_t)text + 4) == g_pop.text_now) wr32h((uint64_t)(uintptr_t)text + 4, g_pop.text_was);
        icon_restore();
        g_pop.state = POP_IDLE;
    }
}

/* ---------------------------------------------------------------------------------------------- chapters, goal */

/* A ChapterData chapter c is complete when the checkpoint (save +0xa7c8, ap_phases.txt) moves on into chapter c + 1
 * (your play log: 120 -> 212 as the Prologue ended), and the last one (17, Chapter XVI) with its Bewitchment
 * ("Chapter XVI Complete", bit 8 of +0x1b0); the Prologue's also with Bewitchment 1. Location 221900 + c. */
static void chapter_done(int c) {
    if (c < 0 || c >= 18 || ap_net_checked(CHAPTER_BASE + c)) return;
    printf("ap: chapter %d complete\n", c);
    fflush(stdout);
    chapter_check(CHAPTER_BASE + c, c);
}

static void chapters_frame(uint64_t save) {
    static int last = -1;
    int c = phase_chapter(rd32h(save + SAVE_PHASE));
    if (c >= 0 && c < 18) {
        if (last >= 0 && c == last + 1) chapter_done(last);
        last = c;
    }
    uint32_t tears = rd32h(save + 0x1b0);
    if (tears & 0x80000000u >> 1) chapter_done(1);              /* Prologue Complete */
    if (tears & 0x80000000u >> 8) chapter_done(17);             /* Chapter XVI Complete */
}

/* The seed's goal (slot_data, Options.py Goal): reported once all it needs is checked. */
static void goal_frame(void) {
    static int sent;
    if (sent) return;
    static const int boss[] = {4, 7, 12, 13, 16, 17};          /* the boss chapters and the last (ChapterData) */
    int goal = ap_net_slot_int("goal", 0), need = ap_net_slot_int("goal_chapter_count", 17), done = 0, all = 1;
    for (int c = 1; c < 18; c++) {
        int d = ap_net_checked(CHAPTER_BASE + c);
        if (goal == 3 && c == 17) continue;                    /* chapter count without the final chapter */
        done += d;
        all &= d;
    }
    int ok;
    if (goal == 1 || goal == 3) ok = done >= need;
    else if (goal == 2) { ok = 1; for (int i = 0; i < 6; i++) ok &= ap_net_checked(CHAPTER_BASE + boss[i]); }
    else if (goal == 4) {
        int have = 0, from = 0, total = 0;
        ApItem it[256];
        do {
            int k = ap_net_items(from, it, 256, &total);
            if (k <= 0) break;
            for (int i = 0; i < k; i++) have += it[i].item == ITEM_BASE + 700;
            from += k;
        } while (from < total);
        ok = have >= ap_net_slot_int("fragments_required", 1)
             && (!ap_net_slot_int("fragments_hunt_requires_final", 1) || ap_net_checked(CHAPTER_BASE + 17));
    } else ok = all;
    if (ok) { sent = 1; printf("ap: goal complete\n"); fflush(stdout); ap_net_goal(); }
}

/* ---------------------------------------------------------------------------------------------- links */

/* Death Link (HP 0 both ways), Damage Link (a quarter of the damage taken goes out; what comes in never takes the
 * last point), Ring Link (Halos picked up in the chapter, save +0xadcc, go out as rings of 50; rings coming in are
 * Halos), Trap Link (traps received from AP go out by name; one coming in fires the trap of that name, or
 * Bloodletting for another game's). The server only sends the links slot_data turned on (ap_net.c link_tags). */
static ULONGLONG g_quiet_until;                  /* a death that came in: not sent back out */
static int g_kill;                               /* a death that came in: the patch calls the game's instant kill
                                                  * 0x7100088c0c (HP 0, HP-lag +0x5f4 0, action 0x11d) - writing HP 0
                                                  * alone is undone by 0x710005d6b8 (HP back to 1) */
void ap_coop_kill(void) { g_quiet_until = GetTickCount64() + 10000; g_kill = 1; }   /* story co-op: the partner fell (not sent to AP) */

static void links_frame(uint64_t save) {
    uint64_t pl = player();
    ApLink l;
    while (ap_net_link(&l)) {
        printf("ap: link %d (%d, %s)\n", l.kind, l.amount, l.text);
        fflush(stdout);
        if (l.kind == AP_LINK_DEATH && pl) { g_quiet_until = GetTickCount64() + 10000; g_kill = 1; }
        else if (l.kind == AP_LINK_DAMAGE && pl && l.amount > 0) {
            uint32_t hp = rd32h(pl + PL_HP);
            wr32h(pl + PL_HP, hp > (uint32_t)l.amount ? hp - (uint32_t)l.amount : (hp ? 1 : 0));
            g_hp_set = 1;
        } else if (l.kind == AP_LINK_RING) {
            int64_t h = (int64_t)rd32h(save + SAVE_HALOS) + (int64_t)l.amount * 50;
            wr32h(save + SAVE_HALOS, (uint32_t)(h < 0 ? 0 : h > HALOS_MAX ? HALOS_MAX : h));
        } else if (l.kind == AP_LINK_TRAP) {
            int k = 1;
            for (int i = 0; i < 15; i++) if (k_trap_name[i][0] && !strcmp(l.text, k_trap_name[i])) k = i;
            trap(save, k);
        }
    }
    static uint64_t pl_was;
    static uint32_t hp_was, rings_was;
    static int rest;
    if (pl) {
        uint32_t hp = rd32h(pl + PL_HP);
        if (pl == pl_was && hp < hp_was && hp_was < 100000) {
            if (hp == 0 && GetTickCount64() > g_quiet_until) ap_net_link_send(AP_LINK_DEATH, 0, "Bayonetta fell.");
            else if (hp) ap_net_link_send(AP_LINK_DAMAGE, (int)(hp_was - hp) / 4, NULL);
        }
        pl_was = pl; hp_was = hp;
    }
    uint32_t got = rd32h(save + 0xadcc);                      /* this chapter's Halos picked up */
    static int rings_based;
    /* what the stage starts with; and while Bayonetta is down (a death writes 2000 here until the Continue clears it) */
    if (!rings_based || !pl || !rd32h(pl + PL_HP)) { rings_based = pl != 0; rings_was = got; return; }
    if (got > rings_was) {
        rest += (int)(got - rings_was);
        if (rest >= 50) { ap_net_link_send(AP_LINK_RING, rest / 50, NULL); rest %= 50; }
    }
    rings_was = got;
}

/* ---------------------------------------------------------------------------------------------- timed traps */

/* Squish / Squash hold off some buttons while they last. Alfheim Curse and Berserk watch HP
 * drops (a hit = at least 10 HP gone at once); Berserk takes the same amount again (never the last point). */
static void traps_frame(void) {
    static uint64_t pl_was;
    static uint32_t hp_was;
    ULONGLONG now = GetTickCount64();
    uint64_t pl = player();
    {   /* Squish: no jump (B) or evade (ZR); Squash: no punch (X) or kick (A) - the default controls (Type A) */
        void ap_hid_block(uint64_t buttons);
        static uint64_t blocked;
        uint64_t want = 0;
        if (g_shape && now < g_shape_until) { if (pl) want = g_shape == 1 ? (1ull << 1 | 1ull << 9) : (1ull << 2 | 1ull << 0); }
        else g_shape = 0;
        if (want != blocked) { ap_hid_block(want); blocked = want; }       /* never kept outside a stage */
    }
    if (!pl) { pl_was = 0; return; }
    uint32_t hp = rd32h(pl + PL_HP);
    if (pl == pl_was && hp + 10 <= hp_was && hp_was < 100000 && !g_hp_set) {
        uint32_t lost = hp_was - hp;
        float taken = ap_cheat_value(AP_CHEAT_DMG_TAKEN);              /* the Damage Taken cheat (Options, Cheats) */
        if (taken != 1.0f && hp > 0) {
            uint32_t want = (uint32_t)(lost * taken + 0.5f);
            hp = hp_was > want ? hp_was - want : 1;
            wr32h(pl + PL_HP, hp);
        }
        if (now < g_berserk_until && hp > 1) { hp = hp > lost ? hp - lost : 1; wr32h(pl + PL_HP, hp); }
        if (g_curse_hits > 0 && now < g_curse_until && --g_curse_hits == 0) { hp = 1; wr32h(pl + PL_HP, 1); }
    }
    g_hp_set = 0;
    if (g_curse_hits > 0 && now >= g_curse_until) g_curse_hits = 0;
    pl_was = pl; hp_was = hp;
}

/* ---------------------------------------------------------------------------------------------- cheats */

/* The Cheats menu (ARCHIPELAGO > CHEATS, runtime/ap_ui.c). God Mode: Bayonetta's HP never drops below the most it
 * has been in this stage. Infinite Halos: the purse full. Infinite Magic: the magic gauge (save +0xa53c, a float the
 * game clamps at the constant at 0x710146246c, 500) kept full. */
#define SAVE_MAGIC 0xa53c
#define MAGIC_MAX_AT 0x710146246cull

static void cheats(uint64_t save) {
    static uint64_t pl_was;
    static uint32_t best;
    uint64_t pl = player();
    if (ap_cheat(AP_CHEAT_GOD) && pl) {
        if (pl != pl_was) { pl_was = pl; best = 0; }
        uint32_t hp = rd32h(pl + PL_HP);
        if (hp > best && hp < 100000) best = hp;
        if (hp < best) wr32h(pl + PL_HP, best);
    }
    {   /* Halo Gain: Halos picked up in a stage (save +0xadcc grows) multiplied; not while Bayonetta is down (a death
         * writes 2000 there) */
        static uint64_t gain_pl;
        static uint32_t picked;
        uint32_t now = rd32h(save + 0xadcc);
        float gain = ap_cheat_value(AP_CHEAT_HALO_GAIN);
        if (pl && pl == gain_pl && now > picked && rd32h(pl + PL_HP) > 0 && gain != 1.0f) {
            int64_t h = (int64_t)rd32h(save + SAVE_HALOS) + (int64_t)((float)(now - picked) * (gain - 1.0f));
            wr32h(save + SAVE_HALOS, (uint32_t)(h < 0 ? 0 : h > HALOS_MAX ? HALOS_MAX : h));
            printf("ap: halo gain x%.0f: %u picked up, Halos now %u\n", gain, now - picked, rd32h(save + SAVE_HALOS));
            fflush(stdout);
        }
        if (!pl || !rd32h(pl + PL_HP)) gain_pl = 0; else gain_pl = pl;
        picked = now;
    }
    if (ap_cheat(AP_CHEAT_HALOS) && rd32h(save + SAVE_HALOS) != HALOS_MAX) wr32h(save + SAVE_HALOS, HALOS_MAX);
    if (ap_cheat(AP_CHEAT_MAGIC)) {
        float max, now;
        memcpy(&max, (void *)(uintptr_t)MAGIC_MAX_AT, 4);
        memcpy(&now, (void *)(uintptr_t)(save + SAVE_MAGIC), 4);
        if (max > 0 && max < 100000 && now < max) memcpy((void *)(uintptr_t)(save + SAVE_MAGIC), &max, 4);
    }
}

/* ---------------------------------------------------------------------------------------------- per frame */

static void game_frame(uint64_t save);

/* Once per game frame (ap_patch.py, the play-time update). 2 = kill Bayonetta (Death Link: the patch calls the game's
 * instant kill 0x7100088c0c). 1 = the popup wants the message files: the patch then
 * looks up mmACV000 / mmCOM001 and calls ap_game_mcds. */
EXPORT uint32_t ap_game_frame(uint64_t save) {
    if (!save) return 0;
    game_frame(save);
    {   /* TEST ONLY: RECOMPS_AP_TEST_POPUP=<kind 0-2>,<item>,<text>: one AP popup 600 frames into a stage */
        static int frames;
        const char *e = getenv("RECOMPS_AP_TEST_POPUP");
        if (e && player() && ++frames == 600 && g_pop.state == POP_IDLE) {
            long long item = 0;                                   /* <kind>,<apworld item id or 0>,<text> */
            sscanf(e, "%d,%lld", &g_pop.cur.kind, &item);
            g_pop.cur.item = item;
            g_pop.cur.ours = item != 0;
            const char *t = strchr(e, ',') ? strchr(strchr(e, ',') + 1, ',') : NULL;
            snprintf(g_pop.cur.text, sizeof g_pop.cur.text, "%s", t ? t + 1 : "");
            g_pop.state = POP_SHOW;
        }
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_HINT=1 asks for a hint 1500 frames into a stage, as reading a book does */
        static int frames;
        const char *e = getenv("RECOMPS_AP_TEST_HINT");
        if (e && atoi(e) && player() && ++frames == 1500) ap_net_hint();
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_DUMP=<pointer address hex>,<offset>,<length>,<file>: 600 frames into a stage,
         * [pointer] + offset, length bytes into the file (research of the game's tables) */
        static int frames;
        const char *e = getenv("RECOMPS_AP_TEST_DUMP");
        unsigned long long at; unsigned off, len; char file[260];
        if (e && player() && ++frames == 600 && sscanf(e, "%llx,%u,%u,%259s", &at, &off, &len, file) == 4) {
            uint64_t p;
            if (at) memcpy(&p, (void *)(uintptr_t)at, 8); else p = player();     /* pointer 0: the player object */
            FILE *f = p ? fopen(file, "wb") : NULL;
            if (f) { fwrite((void *)(uintptr_t)(p + off), 1, len, f); fclose(f); }
            printf("ap: dumped %u bytes of %llx + %u to %s\n", len, (unsigned long long)p, off, f ? file : "(nothing)");
        }
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_CHAT=<frames>: sample chat lines that many frames into a stage */
        static int frames;
        const char *e = getenv("RECOMPS_AP_TEST_CHAT");
        if (e && player() && ++frames == atoi(e)) {
            static const char *const parts[] = {     /* \1..\7 start a colour, \x1f goes back to plain text */
                "\1Boi\x1f sent \3Unicorn Horn\x1f to \2Tester\x1f (\7Chapter I - Verse 1 Rank (Gold+)\x1f)",
                "\2Tester\x1f sent \4Map Width\x1f to \1Boi\x1f (\7Check 12\x1f)",
                "(Hint): \1Boi\x1f's \3Rodin\x1f is at \7Witch Trial II - Part 4\x1f in \2Tester\x1f's World. (not found)",
                "\2Tester\x1f: good luck with the trials!",
                "\2Tester\x1f (Team 1) has completed their goal."};
            for (int i = 0; i < 5; i++) {
                char t[200]; uint8_t c[200]; int n = 0, col = 0;
                for (const char *p = parts[i]; *p; p++) {
                    if (*p == 0x1f) { col = 0; continue; }
                    if (*p >= 1 && *p <= 7) { col = *p; continue; }
                    if (n < 199) { t[n] = *p; c[n++] = (uint8_t)col; }
                }
                t[n] = 0;
                void ap_chat_add(const char *, const uint8_t *);
                ap_chat_add(t, c);
            }
        }
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_PLAYERS=1: the player list's slots (+48 .. +96, 24 apart) and their positions, 1/s */
        static int frames;
        uint64_t m = 0;
        memcpy(&m, (void *)(uintptr_t)PLAYERS, 8);
        if (getenv("RECOMPS_AP_TEST_PLAYERS") && m && ++frames % 60 == 0) {
            printf("ap_players:");
            for (int k = 0; k < 4; k++) {
                uint64_t p = 0; memcpy(&p, (void *)(uintptr_t)(m + 48 + 24 * k), 8);
                float x[3] = {0}; if (p) memcpy(x, (void *)(uintptr_t)(p + 0x40), 12);
                printf(" [%d] %llx (%.1f %.1f %.1f)", k, (unsigned long long)p, x[0], x[1], x[2]);
            }
            printf(" | +0..40:"); for (int k = 0; k < 10; k++) printf(" %x", rd32h(m + 4 * k));
            printf("\n"); fflush(stdout);
            uint64_t p0 = 0, p1 = 0;
            memcpy(&p0, (void *)(uintptr_t)(m + 48), 8); memcpy(&p1, (void *)(uintptr_t)(m + 72), 8);
            {   /* TEST ONLY: RECOMPS_AP_TEST_P2POKE=<off hex>=<value hex>[,...]: written into player 2 once a second */
                const char *e = getenv("RECOMPS_AP_TEST_P2POKE");
                for (const char *q = e; p1 && q && *q; ) {
                    unsigned off, val;
                    if (sscanf(q, "%x=%x", &off, &val) != 2) break;
                    wr32h(p1 + off, val);
                    q = strchr(q, ','); if (q) q++;
                }
            }
            static int vt_done;
            if (p0 && !vt_done) {                            /* player 1's method table (vtable), once */
                vt_done = 1;
                uint64_t vt = 0; memcpy(&vt, (void *)(uintptr_t)p0, 8);
                printf("ap_vtable: %llx:", (unsigned long long)vt);
                for (int k = 0; k < 64 && vt; k++) { uint64_t f = 0; memcpy(&f, (void *)(uintptr_t)(vt + 8 * k), 8); printf(" %d=%llx", k * 8, (unsigned long long)f); }
                printf("\n"); fflush(stdout);
            }
            static int dumped;
            if (p0 && p1 && frames >= 600 && !dumped) {      /* the words that differ between player 1 and 2 (first 0x5000) */
                dumped = 1;
                for (int o = 0x40; o < 0x5000; o += 4) {
                    uint32_t a = rd32h(p0 + o), b = rd32h(p1 + o);
                    if (a != b && !((a >> 20) == 0x1ee || (a >> 20) == 0x1ef || (b >> 20) == 0x1ee || (b >> 20) == 0x1ef)) printf("ap_pdiff: +%x %08x %08x\n", o, a, b);
                }
                fflush(stdout);
            }
        }
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_P2KEEP=1: a player-1-type Bayonetta spawned later takes the player slots
         * (+32/+40/+48, +80/+88/+96) and the camera's target ([[0x71018668f0]+2704]); put player 1 back in them and the
         * newcomer into the partner slot (+56/+64/+72) */
        static uint64_t keep[6], cam_keep, newcomer;
        static int have;
        uint64_t m = 0;
        memcpy(&m, (void *)(uintptr_t)PLAYERS, 8);
        if (getenv("RECOMPS_AP_TEST_P2KEEP") && m) {
            uint64_t cam = 0; memcpy(&cam, (void *)(uintptr_t)0x71018668f0ull, 8);
            uint64_t now48 = 0; memcpy(&now48, (void *)(uintptr_t)(m + 48), 8);
            if (!have && now48) {                                  /* player 1 as she is before any spawn */
                memcpy(&keep[0], (void *)(uintptr_t)(m + 32), 24); memcpy(&keep[3], (void *)(uintptr_t)(m + 80), 24);
                if (cam) cam_keep = rd32h(cam + 2704);
                have = 1;
            } else if (have && now48 && now48 != keep[2] && now48 != newcomer) {
                newcomer = now48;
                uint64_t s[3]; memcpy(s, (void *)(uintptr_t)(m + 32), 24);
                memcpy((void *)(uintptr_t)(m + 56), s, 24);          /* the newcomer: player 2's slot */
                memcpy((void *)(uintptr_t)(m + 32), &keep[0], 24); memcpy((void *)(uintptr_t)(m + 80), &keep[3], 24);
                if (cam) wr32h(cam + 2704, (uint32_t)cam_keep);
                printf("ap_p2keep: player 1 %llx back, the newcomer %llx in the partner slot\n", (unsigned long long)keep[2], (unsigned long long)newcomer);
                fflush(stdout);
            }
        }
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_POS=1: the player's floats +0x40..+0x120 every 150 frames (position research) */
        static int frames;
        uint64_t pl = player();
        if (getenv("RECOMPS_AP_TEST_POS") && pl && ++frames % 150 == 0) {
            printf("ap_pos:");
            for (int o = 0x40; o < 0x120; o += 4) { float f; memcpy(&f, (void *)(uintptr_t)(pl + o), 4); printf(" %x=%.2f", o, f); }
            printf("\n"); fflush(stdout);
        }
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_KILL=<frames>[,own]: a Death Link comes in that many frames into a stage
         * (",own": the death counts as Bayonetta's own, so it goes out) */
        static int frames;
        const char *e = getenv("RECOMPS_AP_TEST_KILL");
        if (e && player() && ++frames == atoi(e)) { if (!strchr(e, ',')) g_quiet_until = GetTickCount64() + 10000; g_kill = 1; }
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_WATCHPL=<offset hex>: a write watch (watch.c) on player + offset, 600 frames in */
        static int frames;
        const char *e = getenv("RECOMPS_AP_TEST_WATCHPL");
        void watch_arm_at(uint64_t addr, int len);
        if (e && player() && ++frames == 600) watch_arm_at(player() + strtoull(e, NULL, 16), 4);
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_RANK=1: the overall-medal table and what verse_medal makes of each entry, once in a stage */
        static int done;
        uint64_t tab = 0;
        if (!done && getenv("RECOMPS_AP_TEST_RANK") && player() && (memcpy(&tab, (void *)(uintptr_t)0x7101870180ull, 8), tab)) {
            uint64_t rec = 0;
            memcpy(&rec, (void *)(uintptr_t)0x71018700f0ull, 8);
            done = 1;
            for (int k = 0; k < 6; k++) {
                int medal = -1;
                if (rec) { *(uint8_t *)(uintptr_t)(rec + 3) = (uint8_t)rd32h(tab + 12 * k + 4); medal = verse_medal(); }
                printf("ap_rank: tier %d: points <= %d -> medal %d (%d), verse_medal %d\n", k, (int)rd32h(tab + 12 * k),
                       (int)rd32h(tab + 12 * k + 4), (int)rd32h(tab + 12 * k + 8), medal);
            }
            if (rec) *(uint8_t *)(uintptr_t)(rec + 3) = 7;
            printf("ap_rank: record %llx\n", (unsigned long long)rec);
            unsigned hash, m;                       /* RECOMPS_AP_TEST_VERSE=<hash hex>,<medal byte>: that verse ends */
            const char *e = getenv("RECOMPS_AP_TEST_VERSE");
            if (rec && e && sscanf(e, "%x,%u", &hash, &m) == 2) { *(uint8_t *)(uintptr_t)(rec + 3) = (uint8_t)m; ap_verse_end(hash); }
            fflush(stdout);
        }
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_HITS=<first frame>,<amount>: a "hit" of that much HP every 60 frames from then */
        static int frames;
        int at, amount;
        const char *e = getenv("RECOMPS_AP_TEST_HITS");
        uint64_t pl = player();
        if (e && pl && sscanf(e, "%d,%d", &at, &amount) == 2 && ++frames >= at && (frames - at) % 60 == 0 && frames - at < 600) {
            uint32_t hp = rd32h(pl + PL_HP);
            wr32h(pl + PL_HP, hp > (uint32_t)amount ? hp - (uint32_t)amount : 1);
        }
        if (e && pl && frames >= at && (frames - at) % 60 == 30 && frames - at < 600) printf("ap: test hp %u\n", rd32h(pl + PL_HP));
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_TIMEW=<offset hex>,<float>: that float into the time object [0x7101867708] each frame */
        unsigned off; float v;
        const char *e = getenv("RECOMPS_AP_TEST_TIMEW");
        uint64_t t; memcpy(&t, (void *)(uintptr_t)0x7101867708ull, 8);
        if (e && t && sscanf(e, "%x,%f", &off, &v) == 2) memcpy((void *)(uintptr_t)(t + off), &v, 4);
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_PTRACE=<file>,<first frame>,<last frame>: the player object's first 0x10000 bytes
         * every frame of that range, appended to the file with the frame number and player 1's buttons (research) */
        static int frames;
        static char file[260];
        static int a = -1, z;
        const char *e = getenv("RECOMPS_AP_TEST_PTRACE");
        uint64_t pl = player();
        if (e && a < 0) { const char *c1 = strchr(e, ','); if (c1) { snprintf(file, sizeof file, "%.*s", (int)(c1 - e), e); sscanf(c1 + 1, "%d,%d", &a, &z); } }
        if (e && pl && ++frames >= a && frames <= z) {
            uint64_t ap_hid_buttons(void);
            FILE *f = fopen(file, "ab");
            if (f) { uint32_t hdr[2] = {(uint32_t)frames, (uint32_t)ap_hid_buttons()}; fwrite(hdr, 4, 2, f); fwrite((void *)(uintptr_t)pl, 1, 0x10000, f); fclose(f); }
        }
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_PICK=<frames>,<n>: n Halos "picked up" (save +0xadcc) that many frames into a stage */
        static int frames;
        int at, n;
        const char *e = getenv("RECOMPS_AP_TEST_PICK");
        if (e && player() && sscanf(e, "%d,%d", &at, &n) == 2 && ++frames == at) wr32h(save + 0xadcc, rd32h(save + 0xadcc) + (uint32_t)n);
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_TRAP=<frames>,<trap 0-14>: that trap fires that many frames into a stage */
        static int frames;
        int at, k;
        const char *e = getenv("RECOMPS_AP_TEST_TRAP");
        if (e && player() && sscanf(e, "%d,%d", &at, &k) == 2 && ++frames == at) { printf("ap: test trap %d\n", k); trap(save, k); }
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_SPAWN=<frames>,<gap>,<ID hex>[,<ID hex>...]: the k-th enemy at frames + k * gap */
        static int frames;
        int at, gap, k = 0;
        const char *e = getenv("RECOMPS_AP_TEST_SPAWN");
        if (e && player() && sscanf(e, "%d,%d", &at, &gap) == 2 && ++frames >= at && (frames - at) % gap == 0) {
            const char *q = strchr(strchr(e, ',') + 1, ',');
            for (; q && k < (frames - at) / gap; k++) q = strchr(q + 1, ',');
            if (q) { printf("ap: test spawn %s\n", q + 1); spawn_enemy((uint32_t)strtoul(q + 1, NULL, 16)); }
        }
        if (e) spawn_watch();
    }
    { void coop_game_frame(void); coop_game_frame(); }         /* story co-op: the partner (coop_game.c) */
    uint32_t want = g_spawn_n && player() ? 4 : 0;
    if (g_kill && player() && (g_kill = 0, rd32h(player() + PL_HP) > 0)) { want |= 2; printf("ap: death link: Bayonetta killed\n"); }
    popup_frame();
    return want | (g_pop.state == POP_SHOW || g_pop.state == POP_RESTORE);
}

static void game_frame(uint64_t save) {
    static int god = -1;                                     /* TEST ONLY: RECOMPS_AP_TEST_GOD=1 keeps Bayonetta's HP up */
    if (god < 0) { const char *e = getenv("RECOMPS_AP_TEST_GOD"); god = e && atoi(e); }
    if (god) { uint64_t pl = player(); if (pl && rd32h(pl + PL_HP) < 1000) wr32h(pl + PL_HP, 2000); }
    traps_frame();
    cheats(save);
    {   /* TEST ONLY: RECOMPS_AP_TEST_GRANT=<apworld item id>,... applies those items once Bayonetta is in a stage */
        static const char *grant;
        static int granted, test_pieces[2];
        static uint32_t test_acc, test_tec, test_wep;
        if (!granted && player()) {
            granted = 1;
            grant = getenv("RECOMPS_AP_TEST_GRANT");
            for (const char *s = grant; s && *s; s = strchr(s, ',') ? strchr(s, ',') + 1 : "") {
                int64_t it = atoll(s);
                if (it == ITEM_BASE + 400 || it == ITEM_BASE + 401) test_pieces[it - ITEM_BASE - 400]++;
                if (it >= ITEM_BASE + 100 && it < ITEM_BASE + 114) test_acc |= 0x80000000u >> (it - ITEM_BASE - 98);
                if (it >= ITEM_BASE + 150 && it < ITEM_BASE + 169) test_tec |= 0x80000000u >> k_tec_no[it - ITEM_BASE - 150];
                if (it >= ITEM_BASE + 1 && it <= ITEM_BASE + 13 && k_weapon_bit[it - ITEM_BASE - 1] >= 0) test_wep |= 0x80000000u >> k_weapon_bit[it - ITEM_BASE - 1];
                apply_spendable(save, it);
            }
        }
        if (test_acc || test_tec) shop_frame(save, test_acc, test_tec, 1);
        if (test_wep) weapons_frame(save, test_wep);
        if (test_pieces[0] || test_pieces[1]) {
            set_pieces(save, ID_HEART_PIECE, SAVE_HEART_PIECES, test_pieces[0]);
            set_pieces(save, ID_PEARL_PIECE, SAVE_PEARL_PIECES, test_pieces[1]);
        }
    }
    if (player()) { check_chests(save); check_bitsets(save); }   /* in a stage (checks queue until connected) */
    chapters_frame(save);
    progress_open();
    if (!g_session[0]) { shop_frame(save, 0, 0, 0); return; }     /* not logged in: purchases still count, nothing given */
    {   /* permanent items: recounted from everything received */
        int pieces[2] = {0, 0}, from = 0, total = 0;
        uint32_t accessories = 0, techniques = 0, weapons = 0;
        ApItem all[256];
        do {
            int k = ap_net_items(from, all, 256, &total);
            if (k <= 0) break;
            for (int i = 0; i < k; i++) {
                int64_t it = all[i].item - ITEM_BASE;
                if (it == 400 || it == 401) pieces[it - 400]++;
                else if (it >= 100 && it < 114) accessories |= 0x80000000u >> (it - 98);          /* accessory m = bit m + 1 */
                else if (it >= 150 && it < 169) techniques |= 0x80000000u >> k_tec_no[it - 150];
                else if (it >= 1 && it <= 13 && k_weapon_bit[it - 1] >= 0) weapons |= 0x80000000u >> k_weapon_bit[it - 1];
            }
            from += k;
        } while (from < total);
        set_pieces(save, ID_HEART_PIECE, SAVE_HEART_PIECES, pieces[0]);
        set_pieces(save, ID_PEARL_PIECE, SAVE_PEARL_PIECES, pieces[1]);
        shop_frame(save, accessories, techniques, 1);
        weapons_frame(save, weapons);
        chapters_given_frame(save);
    }
    /* Halos and consumables only in a stage: on the title the game's save object is not yet the save LOAD GAME picks
     * (items given there were lost when it loaded) */
    ApItem got[32];
    int total, n = player() ? ap_net_items(g_applied, got, 32, &total) : 0;
    int done = 0;
    while (done < n && apply_spendable(save, got[done].item)) {
        printf("ap: got item %lld (from player %d)\n", (long long)got[done].item, got[done].player);
        done++;
    }
    if (done) { g_applied += done; progress_save(); fflush(stdout); }
    goal_frame();
    held_frame();
    links_frame(save);
}
