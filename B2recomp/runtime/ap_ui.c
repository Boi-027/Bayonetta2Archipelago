/* [Archipelago] The connect menu, drawn by the game's own title menu wheel (see scripts/ap_patch.py).
 *
 * The main menu wheel gets two entries more: ARCHIPELAGO and CO-OP. ARCHIPELAGO turns the same wheel into the connect
 * menu: ADDRESS, SLOT, PASSWORD, CONNECT, BACK. CO-OP (story co-op, runtime/coop_net.c) into the lobby menu: HOST,
 * CONNECT, ADDRESS, NAME, PASSWORD, LOBBY (public / private), CHARACTER, BACK. The labels are messages the mod adds to mmTIT001 (TIT_AP_*); the help line
 * under the wheel is one of the text slots TIT_AP_TXT_0..7, which this file rewrites in the loaded message file (the
 * game keeps the file's bytes in memory; a guest pointer is a host pointer), in the help font (font 20, which the mod
 * completed with all of printable ASCII).
 *
 * The game calls in here from the patched lifted code (ap_patch.py finds these exports with GetProcAddress):
 *   ap_menu_label   which message an entry of the wheel shows
 *   ap_menu_pick    the A press on the wheel
 *   ap_menu_frame   once per title-controller frame, on the game's thread: B, text editing, rewriting the slots
 *   ap_menu_active  whether the connect menu is open
 * Editing a field: the keyboard types (Backspace, Enter = done, Esc = cancel, Ctrl+V = paste); on a controller
 * Up / Down change the last character, Right adds one, X removes one, A = done, B = cancel. While a field is edited
 * the keyboard does not press the game's buttons (nnhid.c key_for asks ap_kb_captured).
 * The fields are kept in config\archipelago.ini.
 * Test switch: RECOMPS_AP_TYPE=<text>[|<text>...] types each text into the next field opened for editing. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ap.h"
#include "coop.h"

#define EXPORT __declspec(dllexport)
enum { MAIN, MENU, EDIT };
enum { E_ADDRESS, E_SLOT, E_PASSWORD, E_CONNECT, E_BACK, E_COUNT };   /* (cheats: the Options' Display tab, ap_opt.c) */
enum { C_HOST, C_CONNECT, C_ADDRESS, C_NAME, C_PASSWORD, C_LOBBY, C_CHARACTER, C_BACK, C_COUNT };
enum { F_COOP_ADDRESS = 3, F_COOP_NAME, F_COOP_PASSWORD, F_COUNT };     /* the co-op menu's text fields, after the AP ones */
enum { MAIN_ENTRIES = 7, MAIN_AP = 5, MAIN_COOP = 6 };                   /* the game's five, ARCHIPELAGO, CO-OP */
enum { FRAME_SKIP_GAME = 1, FRAME_REFRESH = 2 };
enum { BTN_A = 0, BTN_B = 1, BTN_X = 2, BTN_LEFT = 12, BTN_UP = 13, BTN_RIGHT = 14, BTN_DOWN = 15,
       BTN_SL_LEFT = 16, BTN_SL_UP = 17, BTN_SL_RIGHT = 18, BTN_SL_DOWN = 19 };
#define BIT(b) (1ull << (b))
#define FIELD_MAX 95

static const char *const k_label[E_COUNT] = {"TIT_AP_ADDRESS", "TIT_AP_SLOT", "TIT_AP_PASSWORD", "TIT_AP_CONNECT",
                                            "TIT_AP_BACK"};
static const char *const k_clabel[C_COUNT] = {"TIT_CO_HOST", "TIT_AP_CONNECT", "TIT_AP_ADDRESS", "TIT_CO_NAME",
                                              "TIT_AP_PASSWORD", "TIT_CO_LOBBY", "TIT_CO_CHARACTER", "TIT_AP_BACK"};
/* CHARACTER: who the partner sees you as (the outfits of the game's figure gallery, GLY_BAYO / GLY_JANNNE) */
const char *const k_coop_skins[] = {"Bayonetta", "Bayonetta: Umbran Gekka", "Bayonetta: Police Woman",
    "Bayonetta: Schoolgirl", "Bayonetta: Witch Apprentice", "Bayonetta: Metal Witch", "Bayonetta: Star Mercenary",
    "Bayonetta: Mushroom Kingdom Princess", "Bayonetta: Sarasaland Princess", "Bayonetta: Hero of Hyrule",
    "Bayonetta: Galactic Bounty Hunter", "Bayonetta: Old Hairstyle", "Jeanne", "Jeanne: Umbran Gekka",
    "Jeanne: Police Woman", "Jeanne: Schoolgirl", "Jeanne: Uniformed", "Jeanne: Elegant Outfit", "Jeanne: Star Mercenary",
    "Jeanne: Hero of Hyrule", "Jeanne: Cutie J (Old Hairstyle)"};
const int k_coop_skin_count = (int)(sizeof k_coop_skins / sizeof k_coop_skins[0]);
static const char k_cycle[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:-_/@ !?&+'(),;";

static CRITICAL_SECTION g_lock;
static volatile LONG g_init;
static int g_mode;                                  /* MAIN, MENU, EDIT */
static int g_menu;                                  /* which menu MENU / EDIT are in: 0 ARCHIPELAGO, 1 CO-OP */
static int g_coop_public = 1, g_coop_skin;          /* LOBBY, CHARACTER */
static int g_edit;                                  /* the field being edited */
static char g_field[F_COUNT][FIELD_MAX + 1];        /* address, slot, password; co-op: address, name, password */
static char g_buf[FIELD_MAX + 1];                   /* the field while it is edited */
static int g_cursor_pending;                        /* move the wheel to g_cursor_to at the next frame */
static int32_t g_cursor_to;
static int g_text_dirty = 1;
static uint64_t g_prev_buttons;
static int g_hold_frames;
static unsigned g_typed[64];                        /* characters from the window thread (WM_CHAR) */
static int g_typed_n;
static char g_ini[MAX_PATH];

static void lock(void) {
    if (InterlockedCompareExchange(&g_init, 1, 0) == 0) { InitializeCriticalSection(&g_lock); InterlockedExchange(&g_init, 2); }
    while (g_init != 2) Sleep(0);
    EnterCriticalSection(&g_lock);
}
static void unlock(void) { LeaveCriticalSection(&g_lock); }

static void ini_path(void) {
    if (g_ini[0]) return;
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, sizeof exe);
    char *s = strrchr(exe, '\\');
    if (s) *s = 0;
    snprintf(g_ini, sizeof g_ini, "%s\\..\\config\\archipelago.ini", exe);    /* build\game_native.exe -> config\ */
}
static void load_fields(void) {
    static int done;
    if (done) return;
    done = 1;
    ini_path();
    GetPrivateProfileStringA("connection", "address", "archipelago.gg:38281", g_field[E_ADDRESS], FIELD_MAX + 1, g_ini);
    GetPrivateProfileStringA("connection", "slot", "", g_field[E_SLOT], FIELD_MAX + 1, g_ini);
    GetPrivateProfileStringA("connection", "password", "", g_field[E_PASSWORD], FIELD_MAX + 1, g_ini);
    GetPrivateProfileStringA("coop", "address", "", g_field[F_COOP_ADDRESS], FIELD_MAX + 1, g_ini);
    GetPrivateProfileStringA("coop", "name", "", g_field[F_COOP_NAME], FIELD_MAX + 1, g_ini);
    GetPrivateProfileStringA("coop", "password", "", g_field[F_COOP_PASSWORD], FIELD_MAX + 1, g_ini);
    g_coop_public = GetPrivateProfileIntA("coop", "public", 1, g_ini) != 0;
    g_coop_skin = GetPrivateProfileIntA("coop", "character", 0, g_ini);
    if (g_coop_skin < 0 || g_coop_skin >= k_coop_skin_count) g_coop_skin = 0;
}
static void save_fields(void) {
    ini_path();
    char dir[MAX_PATH];
    snprintf(dir, sizeof dir, "%s", g_ini);
    char *s = strrchr(dir, '\\');
    if (s) { *s = 0; CreateDirectoryA(dir, NULL); }     /* WritePrivateProfileString makes no folders */
    WritePrivateProfileStringA("connection", "address", g_field[E_ADDRESS], g_ini);
    WritePrivateProfileStringA("connection", "slot", g_field[E_SLOT], g_ini);
    WritePrivateProfileStringA("connection", "password", g_field[E_PASSWORD], g_ini);
    WritePrivateProfileStringA("coop", "address", g_field[F_COOP_ADDRESS], g_ini);
    WritePrivateProfileStringA("coop", "name", g_field[F_COOP_NAME], g_ini);
    WritePrivateProfileStringA("coop", "password", g_field[F_COOP_PASSWORD], g_ini);
    char v[16];
    snprintf(v, sizeof v, "%d", g_coop_public); WritePrivateProfileStringA("coop", "public", v, g_ini);
    snprintf(v, sizeof v, "%d", g_coop_skin); WritePrivateProfileStringA("coop", "character", v, g_ini);
}

static int mod(int32_t v, int n) { return (int)(((v % n) + n) % n); }

/* ---------------------------------------------------------------------------------------------- the wheel */

/* The message ID an entry shows (label, or with help = 1 the line under the wheel), or NULL for one of the game's
 * entries: *game_entry is then its number (0..4) for the game's own lookup. */
EXPORT const char *ap_menu_label(int32_t cursor, int help, int32_t *game_entry) {
    static char slot_id[32];
    load_fields();
    if (g_mode == MAIN) {
        int e = mod(cursor, MAIN_ENTRIES);
        if (e < MAIN_AP) { *game_entry = e; return NULL; }
        if (e == MAIN_COOP) return help ? "TIT_HELP_COOP" : "TIT_BASE_COOP";
        return help ? "TIT_HELP_AP" : "TIT_BASE_AP";
    }
    int e = mod(cursor, g_menu ? C_COUNT : E_COUNT);
    if (!help) return g_menu ? k_clabel[e] : k_label[e];
    snprintf(slot_id, sizeof slot_id, "TIT_AP_TXT_%d", e);
    return slot_id;
}

static void enter_menu(int mode, int32_t cursor) {
    g_mode = mode;
    g_cursor_to = cursor;
    g_cursor_pending = 1;
    g_text_dirty = 1;
}

static void start_edit(int field) {
    g_edit = field;
    strcpy(g_buf, g_field[field]);
    g_mode = EDIT;
    g_text_dirty = 1;
    static const char *script;                       /* RECOMPS_AP_TYPE (tests): the text of this edit */
    static int script_read;
    if (!script_read) { script_read = 1; script = getenv("RECOMPS_AP_TYPE"); }
    if (script && *script) {
        const char *bar = strchr(script, '|');
        size_t n = bar ? (size_t)(bar - script) : strlen(script);
        g_buf[0] = 0;
        for (size_t i = 0; i < n && g_typed_n < (int)(sizeof g_typed / sizeof g_typed[0]) - 1; i++) g_typed[g_typed_n++] = (unsigned char)script[i];
        g_typed[g_typed_n++] = '\r';
        script = bar ? bar + 1 : script + n;
    }
}

/* The A press on the wheel: the game's entry number (0..4), or -1 when it is ours (handled here). */
EXPORT int32_t ap_menu_pick(int32_t cursor) {
    lock();
    load_fields();
    int32_t r = -1;
    if (g_mode == MAIN) {
        int e = mod(cursor, MAIN_ENTRIES);
        if (e < MAIN_AP) r = e;
        else if (e == MAIN_AP) { printf("ap: the connect menu\n"); fflush(stdout); g_menu = 0; enter_menu(MENU, E_ADDRESS); }
        else { printf("coop: the lobby menu\n"); fflush(stdout); g_menu = 1; enter_menu(MENU, C_HOST); }
    } else if (g_mode == MENU && !g_menu) {
        int e = mod(cursor, E_COUNT);
        if (e <= E_PASSWORD) start_edit(e);
        else if (e == E_CONNECT) { ap_net_connect(g_field[E_ADDRESS], g_field[E_SLOT], g_field[E_PASSWORD]); g_text_dirty = 1; }
        else enter_menu(MAIN, MAIN_AP - MAIN_ENTRIES);      /* BACK: the wheel on ARCHIPELAGO */
    } else if (g_mode == MENU) {
        int e = mod(cursor, C_COUNT);
        const char *name = g_field[F_COOP_NAME][0] ? g_field[F_COOP_NAME] : "Bayonetta";
        if (e == C_ADDRESS) start_edit(F_COOP_ADDRESS);
        else if (e == C_NAME) start_edit(F_COOP_NAME);
        else if (e == C_PASSWORD) start_edit(F_COOP_PASSWORD);
        else if (e == C_LOBBY) { g_coop_public = !g_coop_public; save_fields(); }
        else if (e == C_CHARACTER) { g_coop_skin = (g_coop_skin + 1) % k_coop_skin_count; save_fields(); }
        else if (e == C_HOST) {
            if (coop_role() == COOP_HOSTING || (coop_role() == COOP_CONNECTED && coop_is_host())) coop_leave();   /* again: close it */
            else coop_host(g_field[F_COOP_ADDRESS], name, g_field[F_COOP_PASSWORD], g_coop_public, g_coop_skin);
        } else if (e == C_CONNECT) {
            if (coop_role() == COOP_JOINING || (coop_role() == COOP_CONNECTED && !coop_is_host())) coop_leave();
            else coop_join(g_field[F_COOP_ADDRESS], name, g_field[F_COOP_PASSWORD], g_coop_skin);
        } else enter_menu(MAIN, MAIN_COOP - MAIN_ENTRIES);    /* BACK: the wheel on CO-OP */
        g_text_dirty = 1;
    }
    unlock();
    return r;
}

/* ---------------------------------------------------------------------------------------------- editing */

EXPORT int ap_kb_captured(void) { return g_mode == EDIT; }
EXPORT int ap_menu_active(void) { return g_mode != MAIN; }      /* the connect menu is open (the game then looks up mmTIT001) */

/* WM_CHAR from the window thread: 1 = taken */
EXPORT int ap_kb_char(unsigned ch) {
    if (g_mode != EDIT) return 0;
    lock();
    if (g_typed_n < (int)(sizeof g_typed / sizeof g_typed[0])) g_typed[g_typed_n++] = ch;
    unlock();
    return 1;
}

static void finish_edit(int keep) {                  /* back to the menu the field is in (g_menu) */
    if (keep) { strcpy(g_field[g_edit], g_buf); save_fields(); }
    g_mode = MENU;
    g_text_dirty = 1;
}

static void paste(void) {
    if (!OpenClipboard(NULL)) return;
    HANDLE h = GetClipboardData(CF_TEXT);
    const char *s = h ? (const char *)GlobalLock(h) : NULL;
    for (size_t n = strlen(g_buf); s && *s && n < FIELD_MAX; s++)
        if ((unsigned char)*s >= 32 && (unsigned char)*s < 127) { g_buf[n++] = *s; g_buf[n] = 0; }
    if (h) GlobalUnlock(h);
    CloseClipboard();
}

static void edit_typed(void) {
    for (int i = 0; i < g_typed_n && g_mode == EDIT; i++) {
        unsigned ch = g_typed[i];
        size_t n = strlen(g_buf);
        if (ch == '\b') { if (n) g_buf[n - 1] = 0; }
        else if (ch == '\r') finish_edit(1);
        else if (ch == 27) finish_edit(0);
        else if (ch == 22) paste();                   /* Ctrl+V */
        else if (ch >= 32 && ch < 127 && n < FIELD_MAX) { g_buf[n] = (char)ch; g_buf[n + 1] = 0; }
        g_text_dirty = 1;
    }
    g_typed_n = 0;
}

static void edit_pad(uint64_t down, uint64_t held) {
    size_t n = strlen(g_buf);
    int up = (int)((down | held) >> BTN_UP & 1) | (int)((down | held) >> BTN_SL_UP & 1);
    int dn = (int)((down | held) >> BTN_DOWN & 1) | (int)((down | held) >> BTN_SL_DOWN & 1);
    if (down & BIT(BTN_A)) { finish_edit(1); return; }
    if (down & BIT(BTN_B)) { finish_edit(0); return; }
    if ((down & (BIT(BTN_X) | BIT(BTN_LEFT) | BIT(BTN_SL_LEFT))) && n) { g_buf[n - 1] = 0; g_text_dirty = 1; return; }
    if ((down & (BIT(BTN_RIGHT) | BIT(BTN_SL_RIGHT))) && n < FIELD_MAX) { g_buf[n] = 'a'; g_buf[n + 1] = 0; g_text_dirty = 1; return; }
    if (up || dn) {                                    /* Up / Down: the last character; held = repeats */
        if (!(down & (BIT(BTN_UP) | BIT(BTN_SL_UP) | BIT(BTN_DOWN) | BIT(BTN_SL_DOWN))) && (g_hold_frames < 18 || g_hold_frames % 4)) return;
        if (!n) { g_buf[0] = 'a'; g_buf[1] = 0; n = 1; }
        const char *p = strchr(k_cycle, g_buf[n - 1]);
        int i = p ? (int)(p - k_cycle) : 0, len = (int)strlen(k_cycle);
        g_buf[n - 1] = k_cycle[mod(i + (up ? -1 : 1), len)];
        g_text_dirty = 1;
    }
}

/* ---------------------------------------------------------------------------------------------- the text slots */

typedef struct { uint8_t *mcd; uint32_t line; uint32_t cap; } Slot;
static Slot g_slots[C_COUNT];                        /* TIT_AP_TXT_0..7 (the co-op menu has 8 entries) */
static uint16_t g_sym[128];                          /* character -> symbol index of font 20; 0xffff = none */
static uint8_t *g_sym_mcd;

static uint32_t u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static int find_slot(uint8_t *mcd, int i, Slot *s) {
    char name[32];
    snprintf(name, sizeof name, "TIT_AP_TXT_%d", i);
    uint32_t ev = u32(mcd + 32), nev = u32(mcd + 36), msgs = u32(mcd), nmsg = u32(mcd + 4);
    for (uint32_t k = 0; k < nev; k++) {
        const uint8_t *e = mcd + ev + 40 * k;
        if (strncmp((const char *)e + 8, name, 32)) continue;
        uint32_t m = u32(e + 4);
        if (m >= nmsg) return 0;
        uint32_t para = u32(mcd + msgs + 16 * m);
        s->line = u32(mcd + para);
        s->cap = u32(mcd + s->line + 8);             /* the words the mod reserved */
        s->mcd = mcd;
        return 1;
    }
    return 0;
}

static void write_slot(uint8_t *mcd, int i, const char *text) {
    Slot *s = &g_slots[i];
    if (s->mcd != mcd && !find_slot(mcd, i, s)) return;
    if (g_sym_mcd != mcd) {
        memset(g_sym, 0xff, sizeof g_sym);
        uint32_t so = u32(mcd + 8), n = u32(mcd + 12);
        for (uint32_t k = 0; k < n; k++) {
            uint16_t font, ch;
            memcpy(&font, mcd + so + 8 * k, 2); memcpy(&ch, mcd + so + 8 * k + 2, 2);
            if (font == 20 && ch < 128 && g_sym[ch] == 0xffff) g_sym[ch] = (uint16_t)k;
        }
        g_sym_mcd = mcd;
    }
    uint8_t *words = mcd + u32(mcd + s->line);
    uint32_t n = 0;
    for (const char *p = text; *p && n + 3 <= s->cap; p++) {
        uint16_t w[2] = {0x8001, 20};                /* a space: 0x8001 and its width */
        if (*p != ' ') {
            unsigned ch = (unsigned char)*p;
            w[0] = ch < 128 && g_sym[ch] != 0xffff ? g_sym[ch] : g_sym['?'];
            w[1] = 0;                                /* kerning */
            if (w[0] == 0xffff) continue;
        }
        memcpy(words + 2 * n, w, 4);
        n += 2;
    }
    uint16_t end = 0x8000;
    memcpy(words + 2 * n, &end, 2);
    n++;
    memcpy(mcd + s->line + 8, &n, 4);                /* the line's word count, twice */
    memcpy(mcd + s->line + 12, &n, 4);
}

static void coop_texts(uint8_t *mcd) {
    char t[200], status[128], ips[96];
    const char *cur = g_mode == EDIT ? "_" : "";
#define FIELD(f) (g_mode == EDIT && g_edit == (f) ? g_buf : g_field[f])
    int role = coop_role(), host = coop_is_host();
    coop_status(status, sizeof status);
    coop_local_ips(ips, sizeof ips);
    if (role == COOP_HOSTING || (role == COOP_CONNECTED && host)) snprintf(t, sizeof t, "%s (A: close the lobby)", status);
    else snprintf(t, sizeof t, "Host a lobby (UDP port %d). Your address: %s", COOP_PORT, ips[0] ? ips : "?");
    write_slot(mcd, C_HOST, t);
    if (role == COOP_JOINING || (role == COOP_CONNECTED && !host)) snprintf(t, sizeof t, "%s (A: leave)", status);
    else if (role == COOP_OFF && strcmp(status, "Not connected.")) snprintf(t, sizeof t, "%s", status);
    else snprintf(t, sizeof t, "Join the address below; empty: a public lobby on your network.");
    write_slot(mcd, C_CONNECT, t);
    snprintf(t, sizeof t, "Address: %s%s", *FIELD(F_COOP_ADDRESS) || (g_mode == EDIT && g_edit == F_COOP_ADDRESS) ? FIELD(F_COOP_ADDRESS) : "(find one on my network)",
             g_mode == EDIT && g_edit == F_COOP_ADDRESS ? cur : "");
    write_slot(mcd, C_ADDRESS, t);
    snprintf(t, sizeof t, "Your name: %s%s", *FIELD(F_COOP_NAME) || (g_mode == EDIT && g_edit == F_COOP_NAME) ? FIELD(F_COOP_NAME) : "Bayonetta",
             g_mode == EDIT && g_edit == F_COOP_NAME ? cur : "");
    write_slot(mcd, C_NAME, t);
    if (g_mode == EDIT && g_edit == F_COOP_PASSWORD) snprintf(t, sizeof t, "Lobby password: %s_", g_buf);
    else if (g_field[F_COOP_PASSWORD][0]) { size_t n = strlen(g_field[F_COOP_PASSWORD]); strcpy(t, "Lobby password: "); memset(t + 16, '*', n > 40 ? 40 : n); t[16 + (n > 40 ? 40 : n)] = 0; }
    else strcpy(t, "Lobby password: (none)");
    write_slot(mcd, C_PASSWORD, t);
    write_slot(mcd, C_LOBBY, g_coop_public ? "Lobby: Public - anyone with your address can join; it shows on your network."
                                           : "Lobby: Private - only with the password; hidden on your network.");
    snprintf(t, sizeof t, "Character: %s (A: next)", k_coop_skins[g_coop_skin]);
    write_slot(mcd, C_CHARACTER, t);
    write_slot(mcd, C_BACK, "Return to the main menu.");
#undef FIELD
}

static void slot_texts(uint8_t *mcd) {
    if (g_menu) { coop_texts(mcd); return; }
    char t[160], status[96];
    const char *addr = g_mode == EDIT && g_edit == E_ADDRESS ? g_buf : g_field[E_ADDRESS];
    const char *slot = g_mode == EDIT && g_edit == E_SLOT ? g_buf : g_field[E_SLOT];
    const char *cur = g_mode == EDIT ? "_" : "";
    snprintf(t, sizeof t, "Server: %s%s", addr, g_mode == EDIT && g_edit == E_ADDRESS ? cur : "");
    write_slot(mcd, E_ADDRESS, t);
    snprintf(t, sizeof t, "Slot name: %s%s", *slot || (g_mode == EDIT && g_edit == E_SLOT) ? slot : "(not set)",
             g_mode == EDIT && g_edit == E_SLOT ? cur : "");
    write_slot(mcd, E_SLOT, t);
    if (g_mode == EDIT && g_edit == E_PASSWORD) snprintf(t, sizeof t, "Password: %s_", g_buf);
    else if (g_field[E_PASSWORD][0]) { size_t n = strlen(g_field[E_PASSWORD]); memset(t, 0, sizeof t); strcpy(t, "Password: "); memset(t + 10, '*', n > 40 ? 40 : n); }
    else strcpy(t, "Password: (none)");
    write_slot(mcd, E_PASSWORD, t);
    ap_net_status(status, sizeof status);
    write_slot(mcd, E_CONNECT, status);
    write_slot(mcd, E_BACK, "Return to the main menu.");
}

/* ---------------------------------------------------------------------------------------------- per frame */

/* Once per title-controller frame, on the game's thread. tit_mcd = the loaded mmTIT001 message file (0 = unknown).
 * *cursor = the wheel's cursor (set it with FRAME_REFRESH). Returns FRAME_* bits. */
EXPORT uint32_t ap_menu_frame(uint64_t tit_mcd, int32_t *cursor) {
    uint64_t b = ap_hid_buttons();
    uint64_t down = b & ~g_prev_buttons;
    g_hold_frames = (b & g_prev_buttons) ? g_hold_frames + 1 : 0;
    g_prev_buttons = b;
    uint32_t r = 0;
    { void ap_title_frame(void); ap_title_frame(); }   /* ap_game.c: Chapter Select opened up to the unlocks */
    lock();
    load_fields();
    {   /* TEST ONLY: RECOMPS_AP_TEST_CONNECT=1 connects with the saved fields at the first title frame */
        static int once;
        if (!once) { once = 1; const char *e = getenv("RECOMPS_AP_TEST_CONNECT"); if (e && atoi(e)) ap_net_connect(g_field[E_ADDRESS], g_field[E_SLOT], g_field[E_PASSWORD]); }
    }
    {   /* TEST ONLY: RECOMPS_COOP_TEST=join:<address> (or host) at the first title frame, with the saved name / password */
        static int once;
        const char *e = getenv("RECOMPS_COOP_TEST");
        if (!once && e) {
            once = 1;
            if (!strncmp(e, "join:", 5)) coop_join(e + 5, "Tester", g_field[F_COOP_PASSWORD], 12);
            else if (!strcmp(e, "host")) coop_host("", "Host", g_field[F_COOP_PASSWORD], g_coop_public, g_coop_skin);
        }
    }
    if (g_mode == EDIT) {
        edit_typed();
        if (g_mode == EDIT) edit_pad(down, b & ~down);
        r |= FRAME_SKIP_GAME;                         /* nothing of the game's menu reacts while a field is edited */
    } else if (g_mode == MENU && (down & BIT(BTN_B))) {
        enter_menu(MAIN, (g_menu ? MAIN_COOP : MAIN_AP) - MAIN_ENTRIES);   /* B: back to the main menu, on the entry */
        r |= FRAME_SKIP_GAME;
    }
    if (ap_net_status_changed()) g_text_dirty = 1;
    if (coop_status_changed()) g_text_dirty = 1;
    if (g_mode != MAIN && g_text_dirty && tit_mcd) {
        slot_texts((uint8_t *)(uintptr_t)tit_mcd);
        g_text_dirty = 0;
        r |= FRAME_REFRESH;                           /* the help line takes its text when the wheel is set again */
    }
    if (g_cursor_pending) { *cursor = g_cursor_to; g_cursor_pending = 0; r |= FRAME_REFRESH; }
    unlock();
    return r;
}
