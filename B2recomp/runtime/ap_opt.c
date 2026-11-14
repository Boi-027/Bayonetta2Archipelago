/* [Archipelago] Cheats pages on the Options screen's Display tab (cUIOptMenu). ap_patch.py calls ap_opt_frame once per
 * frame while the screen is open (before the menu's update 0x7100c439e8) with the menu object and the loaded mmOPT000;
 * bit 0 of the result asks it to rebuild the Display rows (0x7100c5a178(controller, row)).
 *
 * On the Display tab, L / R turn the page: page 0 is the game's Display settings, pages 1.. are cheats. A page is the
 * Display controller's own six rows (their widgets are fixed by row: 0 a slider, 1 and 2 language selectors, 3 and 4
 * ON/OFF, 5 Restore Default); a cheats page uses the slider and the two ON/OFF rows and blanks the others.
 *
 * Menu object: +124 focus (1 tabs, 2 rows, 3 editing a row), +120 2 = the "Apply changes?" question, +216 the tab
 * (3 = Display), +192 the Display controller. Display controller: +1752 its row list (24-byte entries: type, enabled,
 * label message hash), +1672 the row cursor, slider value +320 (float, 0.1 a step), ON/OFF rows 3 / 4 at +1024 /
 * +1288 (0 = ON, 1 = OFF). The game builds the rows from the list when the tab gets the focus or the cursor moves;
 * leaving the screen it writes the settings from those widget states, so the Display values are put back first.
 *
 * Help lines: the game picks OPT_DISPLAY_HELP_01 / _02 / _05 (slider, row 3, row 4), OPT_COMMON_HELP_00 (the locked
 * language rows), OPT_DISPLAY_HELP_00 (Restore) and OPT_TITLE_HELP_04 (the tab); the mod (ap/tools/build_mod.py)
 * gave them font 20 and room, and they are rewritten here. Cheat values: config\archipelago.ini [cheats]. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ap.h"

#define EXPORT __declspec(dllexport)

static uint32_t rd32h(uint64_t a) { uint32_t v; memcpy(&v, (void *)(uintptr_t)a, 4); return v; }
static uint64_t rd64h(uint64_t a) { uint64_t v; memcpy(&v, (void *)(uintptr_t)a, 8); return v; }
static void wr32h(uint64_t a, uint32_t v) { memcpy((void *)(uintptr_t)a, &v, 4); }
static uint32_t crc_name(const char *s) {          /* a message's hash: CRC32 of the lowercase name */
    uint32_t c = ~0u;
    for (; *s; s++) { c ^= (uint8_t)(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s); for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xedb88320u & -(c & 1)); }
    return ~c;
}

/* ---------------------------------------------------------------------------------------------- the cheats */

enum { KIND_TOGGLE, KIND_SLIDER };
typedef struct {
    const char *key, *label, *help;               /* ini key, label message (mod), help line */
    int kind;
    float def;                                    /* the default */
    float steps[7];                               /* sliders: the value at each of the slider's 7 positions */
} Cheat;
static const Cheat k_cheats[AP_CHEAT_COUNT] = {
    [AP_CHEAT_GOD] = {"god_mode", "AP_CHT_GOD", "Bayonetta takes no damage.", KIND_TOGGLE},
    [AP_CHEAT_HALOS] = {"infinite_halos", "AP_CHT_HALOS", "Your Halos stay full.", KIND_TOGGLE},
    [AP_CHEAT_MAGIC] = {"infinite_magic", "AP_CHT_MAGIC", "The magic gauge stays full.", KIND_TOGGLE},
    [AP_CHEAT_DMG_TAKEN] = {"damage_taken", "AP_OPT_SLIDER", "How much of the damage you take counts.", KIND_SLIDER, 1.0f, {0, 0.25f, 0.5f, 1, 2, 3, 5}},
    [AP_CHEAT_HALO_GAIN] = {"halo_gain", "AP_OPT_SLIDER", "Halos you pick up are multiplied.", KIND_SLIDER, 1.0f, {1, 2, 3, 5, 10, 20, 50}},
    [AP_CHEAT_EASY_SKIP] = {"easy_skip", "AP_CHT_SKIP", "Pausing a cutscene skips it.", KIND_TOGGLE},
    [AP_CHEAT_AUTO_QTE] = {"auto_qte", "AP_CHT_QTE", "Button prompts in cutscenes succeed by themselves.", KIND_TOGGLE},
};
/* each page: the slider row's cheat, then rows 3 and 4 (-1 = blank) */
static const int k_pages[][3] = {
    {AP_CHEAT_DMG_TAKEN, AP_CHEAT_GOD, AP_CHEAT_HALOS},
    {AP_CHEAT_HALO_GAIN, AP_CHEAT_MAGIC, -1},
    {-1, AP_CHEAT_EASY_SKIP, AP_CHEAT_AUTO_QTE},
};
#define PAGES ((int)(sizeof k_pages / sizeof k_pages[0]))

static float g_value[AP_CHEAT_COUNT];
static char g_ini[MAX_PATH];
static int g_loaded;

static void load(void) {
    if (g_loaded) return;
    g_loaded = 1;
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, sizeof exe);
    char *s = strrchr(exe, '\\');
    if (s) *s = 0;
    snprintf(g_ini, sizeof g_ini, "%s\\..\\config\\archipelago.ini", exe);    /* build\game_native.exe -> config\ */
    for (int i = 0; i < AP_CHEAT_COUNT; i++) {
        char v[32];
        snprintf(v, sizeof v, "%g", k_cheats[i].def);
        GetPrivateProfileStringA("cheats", k_cheats[i].key, v, v, sizeof v, g_ini);
        g_value[i] = (float)atof(v);
    }
}
static void save(int i) {
    char v[32], dir[MAX_PATH];
    snprintf(v, sizeof v, "%g", g_value[i]);
    snprintf(dir, sizeof dir, "%s", g_ini);
    char *s = strrchr(dir, '\\');
    if (s) { *s = 0; CreateDirectoryA(dir, NULL); }
    WritePrivateProfileStringA("cheats", k_cheats[i].key, v, g_ini);
}

/* A toggle cheat is on (runtime/ap_game.c applies it each frame). */
int ap_cheat(int which) { load(); return which >= 0 && which < AP_CHEAT_COUNT && g_value[which] != 0; }
/* A slider cheat's value. */
float ap_cheat_value(int which) { load(); return which >= 0 && which < AP_CHEAT_COUNT ? g_value[which] : 0; }

/* ---------------------------------------------------------------------------------------------- message text */

/* Rewrites the first line of message `name` in the loaded mcd with `text`, in that paragraph's font; the room is the
 * line's word count the first time it is seen (the mod padded these messages). */
typedef struct { uint64_t mcd; uint32_t hash, line, cap, font, orig_n; uint16_t orig[160]; } Msg;   /* orig: the first words, for msg_restore */
static Msg g_msgs[48];

static Msg *msg_find(uint64_t mcd, const char *name) {
    static uint64_t cached;
    if (cached != mcd) { memset(g_msgs, 0, sizeof g_msgs); cached = mcd; }   /* another load of the file */
    uint32_t h = crc_name(name) & 0x7fffffff;
    for (int i = 0; i < 48; i++) if (g_msgs[i].mcd == mcd && g_msgs[i].hash == h) return &g_msgs[i];
    uint32_t ev = rd32h(mcd + 32), nev = rd32h(mcd + 36), msgs = rd32h(mcd), nmsg = rd32h(mcd + 4);
    for (uint32_t k = 0; k < nev; k++) {
        if (rd32h(mcd + ev + 40 * k) != h) continue;
        uint32_t m = rd32h(mcd + ev + 40 * k + 4);
        if (m >= nmsg) return NULL;
        uint32_t para = rd32h(mcd + msgs + 16 * m);
        for (int i = 0; i < 48; i++)
            if (!g_msgs[i].mcd) {
                g_msgs[i] = (Msg){mcd, h, rd32h(mcd + para), 0, rd32h(mcd + para + 16)};
                g_msgs[i].cap = rd32h(mcd + g_msgs[i].line + 8);
                g_msgs[i].orig_n = g_msgs[i].cap < 160 ? g_msgs[i].cap : 160;
                memcpy(g_msgs[i].orig, (void *)(uintptr_t)(mcd + rd32h(mcd + g_msgs[i].line)), 2 * g_msgs[i].orig_n);
                return &g_msgs[i];
            }
        return NULL;
    }
    return NULL;
}

static void msg_write(uint64_t mcd, const char *name, const char *text) {
    Msg *m = mcd ? msg_find(mcd, name) : NULL;
    if (!m) return;
    static uint16_t sym[128];
    static uint64_t sym_mcd;
    static uint32_t sym_font = ~0u;
    if (sym_mcd != mcd || sym_font != m->font) {
        memset(sym, 0xff, sizeof sym);
        uint32_t so = rd32h(mcd + 8), n = rd32h(mcd + 12);
        for (uint32_t k = 0; k < n; k++) {
            uint16_t font, ch;
            memcpy(&font, (void *)(uintptr_t)(mcd + so + 8 * k), 2); memcpy(&ch, (void *)(uintptr_t)(mcd + so + 8 * k + 2), 2);
            if (font == m->font && ch < 128 && sym[ch] == 0xffff) sym[ch] = (uint16_t)k;
        }
        sym_mcd = mcd; sym_font = m->font;
    }
    uint64_t words = mcd + rd32h(mcd + m->line);
    uint32_t n = 0;
    for (const char *p = text; *p && n + 3 <= m->cap; p++) {
        uint16_t w[2] = {0x8001, 20};                /* a space: 0x8001 and its width */
        if (*p != ' ') {
            unsigned ch = (unsigned char)*p;
            w[0] = ch < 128 && sym[ch] != 0xffff ? sym[ch] : sym['?'];
            w[1] = 0;
            if (w[0] == 0xffff) continue;
        }
        memcpy((void *)(uintptr_t)(words + 2 * n), w, 4);
        n += 2;
    }
    uint16_t end = 0x8000;
    memcpy((void *)(uintptr_t)(words + 2 * n), &end, 2);
    n++;
    wr32h(mcd + m->line + 8, n);                       /* the line's word count, twice */
    wr32h(mcd + m->line + 12, n);
}

static void msg_restore(uint64_t mcd, const char *name) {   /* a message back as the file had it */
    Msg *m = mcd ? msg_find(mcd, name) : NULL;
    if (!m) return;
    memcpy((void *)(uintptr_t)(mcd + rd32h(mcd + m->line)), m->orig, 2 * m->orig_n);
    wr32h(mcd + m->line + 8, m->cap);
    wr32h(mcd + m->line + 12, m->cap);
}

/* ---------------------------------------------------------------------------------------------- the pages */

#define D_ROWS 1752
#define D_CURSOR 1672
#define D_SLIDER 320
#define D_ON3 1024
#define D_ON4 1288
/* The brightness slider has 7 positions, its step table (widget +656, count +652): 0.7 .. 1.3. A cheat slider's
 * position k means its steps[k]. The dial (0x7100d0638c) puts an exact step value one position to the right, and
 * anything between steps somewhere else: position k is written as the exact value of step k - 1 (position 0 as 0). */
static int slider_steps(uint64_t disp, float out[7]) {
    uint64_t w = rd64h(disp + 208), tab = w ? rd64h(w + 656) : 0;
    float nf = 0;
    if (w) memcpy(&nf, (void *)(uintptr_t)(w + 652), 4);
    if (!tab || nf != 7.0f) return 0;
    memcpy(out, (void *)(uintptr_t)tab, 7 * 4);
    return 1;
}

static int g_page;                                    /* 0 = the game's Display settings */
static int g_live;                                    /* the widgets show the cheats' values (read them back) */
static uint64_t g_menu;
static uint8_t g_rows_saved[6 * 24];                  /* the game's own rows and widget states, while a cheats page is up */
static float g_slider_saved;
static uint32_t g_on_saved[2];

static float slider_get(uint64_t disp) {              /* the slider widget's value ([+208] + 732; +320 follows it) */
    float s;
    uint64_t w = rd64h(disp + 208);
    memcpy(&s, (void *)(uintptr_t)(w ? w + 732 : disp + D_SLIDER), 4);
    return s;
}
static void slider_set(uint64_t disp, float s) {
    uint64_t w = rd64h(disp + 208);
    if (w) { memcpy((void *)(uintptr_t)(w + 732), &s, 4); memcpy((void *)(uintptr_t)(w + 744), &s, 4); }   /* value, the dial */
    memcpy((void *)(uintptr_t)(disp + D_SLIDER), &s, 4);
}

static int g_k_prev = -1;                             /* the slider position last seen while its row is edited */

static int value_index(const Cheat *c, float v) {     /* the step nearest to a slider cheat's value */
    int k = 0;
    for (int i = 1; i < 7; i++) if (fabsf(c->steps[i] - v) < fabsf(c->steps[k] - v)) k = i;
    return k;
}

/* The widgets -> the page's cheats, while a row is edited. The slider: only the positions the player moves it change
 * the value (the dial's own snapping can sit a step off from what was written). */
static void read_page(uint64_t disp) {
    const int *pg = k_pages[g_page - 1];
    if (pg[0] >= 0) {
        const Cheat *c = &k_cheats[pg[0]];
        float t[7], s = slider_get(disp);
        if (!slider_steps(disp, t)) return;
        int k = 0;
        for (int i = 1; i < 7; i++) if (fabsf(t[i] - s) < fabsf(t[k] - s)) k = i;
        if (g_k_prev >= 0 && k != g_k_prev) {
            int j = value_index(c, g_value[pg[0]]) + (k - g_k_prev);
            j = j < 0 ? 0 : j > 6 ? 6 : j;
            float v = c->steps[j];
            if (getenv("RECOMPS_AP_TEST_OPT")) { printf("ap_opt: %s %.2f -> %.2f\n", c->key, g_value[pg[0]], v); fflush(stdout); }
            if (v != g_value[pg[0]]) { g_value[pg[0]] = v; save(pg[0]); }
        }
        g_k_prev = k;
    }
    for (int r = 0; r < 2; r++) {
        int i = pg[1 + r];
        float v = rd32h(disp + (r ? D_ON4 : D_ON3)) == 0;
        if (i >= 0 && v != g_value[i]) { g_value[i] = v; save(i); }
    }
}

static void write_rows(uint64_t disp) {               /* the page's rows (label, enabled) into the row list */
    const int *pg = k_pages[g_page - 1];
    uint64_t rows = rd64h(disp + D_ROWS);
    uint32_t blank = crc_name("AP_CHT_NONE") & 0x7fffffff;
    for (int r = 0; r < 6; r++) {
        int cheat = r == 0 ? pg[0] : r == 3 ? pg[1] : r == 4 ? pg[2] : -1;
        uint32_t e[3] = {(uint32_t)r, cheat >= 0, cheat >= 0 ? crc_name(k_cheats[cheat].label) & 0x7fffffff : blank};
        memcpy((void *)(uintptr_t)(rows + 24 * r), e, sizeof e);
    }
}

/* An ON/OFF row (r 0 = row 3, 1 = row 4): its value (0 = ON) and the lit button: the buttons are [+992] / [+1000]
 * (row 3) and [+1256] / [+1264] (row 4); +520 and +524 are 1 on the chosen one. */
static void onoff_set(uint64_t disp, int r, int on) {
    wr32h(disp + (r ? D_ON4 : D_ON3), !on);
    for (int k = 0; k < 2; k++) {
        uint64_t b = rd64h(disp + (r ? 1256 : 992) + 8 * k);
        if (!b) continue;
        uint32_t lit = k == 0 ? on : !on;
        wr32h(b + 520, lit); wr32h(b + 524, lit);
    }
}

static void write_values(uint64_t disp) {             /* the cheats' values -> the widgets */
    const int *pg = k_pages[g_page - 1];
    if (pg[0] >= 0) {
        const Cheat *c = &k_cheats[pg[0]];
        float t[7];
        int k = value_index(c, g_value[pg[0]]);
        /* the dial shows an exact step value one position to the right: position k is the step before's value */
        if (slider_steps(disp, t)) slider_set(disp, k > 0 ? t[k - 1] : 0.0f);      /* 0: the leftmost position */
    }
    for (int r = 0; r < 2; r++) if (pg[1 + r] >= 0) onoff_set(disp, r, g_value[pg[1 + r]] != 0);
}

static void to_page(uint64_t disp, int page) {        /* the rows show after the next focus of the tab */
    if (page == g_page) return;
    if (g_page == 0) {                                /* leaving the game's page: keep its rows and values */
        memcpy(g_rows_saved, (void *)(uintptr_t)rd64h(disp + D_ROWS), sizeof g_rows_saved);
        g_slider_saved = slider_get(disp);
        g_on_saved[0] = rd32h(disp + D_ON3); g_on_saved[1] = rd32h(disp + D_ON4);
    }
    g_page = page;
    g_live = 0;
    if (g_page == 0) {
        memcpy((void *)(uintptr_t)rd64h(disp + D_ROWS), g_rows_saved, sizeof g_rows_saved);
        slider_set(disp, g_slider_saved);
        onoff_set(disp, 0, !g_on_saved[0]); onoff_set(disp, 1, !g_on_saved[1]);
    } else write_rows(disp);
}

static void texts(uint64_t mcd) {                     /* the help lines (and the slider's label with its value) */
    static int was = -1;
    char t[96];
    static const char *const langs[] = {"OPT_LANG_00", "OPT_LANG_01", "OPT_LANG_02", "OPT_LANG_03", "OPT_LANG_04",
        "OPT_LANG_05", "OPT_LANG_06", "OPT_LANG_07", "OPT_LANG_08", "OPT_LANG_09", "OPT_LANG_10", "OPT_LANG_11",
        "OPT_LANG_FIXED_01", "OPT_LANG_FIXED_02", "OPT_LANG_FIXED_03", "OPT_LANG_FIXED_04", "OPT_LANG_FIXED_05",
        "OPT_LANG_FIXED_06", "OPT_LANG_FIXED_07", "OPT_LANG_FIXED_08"};
    if (g_page == 0) {
        if (was == 0) return;
        for (int i = 0; i < (int)(sizeof langs / sizeof langs[0]); i++) msg_restore(mcd, langs[i]);
        msg_write(mcd, "OPT_DISPLAY_HELP_00", "Return display settings to their defaults.");
        msg_write(mcd, "OPT_DISPLAY_HELP_01", "Adjust screen brightness.");
        msg_write(mcd, "OPT_DISPLAY_HELP_02", "Turn cutscene subtitles ON/OFF.");
        msg_write(mcd, "OPT_DISPLAY_HELP_05", "Turn in-game control hint display ON/OFF.");
        msg_write(mcd, "OPT_COMMON_HELP_00", "This option can only be changed from the Title Menu.");
        msg_write(mcd, "OPT_TITLE_HELP_04", "Adjust display settings. L/R: cheats.");
        msg_write(mcd, "OPT_TITLE_BASE_04", "Display");
        was = 0;
        return;
    }
    const int *pg = k_pages[g_page - 1];
    for (int i = 0; i < (int)(sizeof langs / sizeof langs[0]); i++) msg_write(mcd, langs[i], " ");   /* the unused selectors */
    snprintf(t, sizeof t, "Cheats, page %d of %d. L/R turns the page.", g_page, PAGES);
    msg_write(mcd, "OPT_TITLE_HELP_04", t);
    msg_write(mcd, "OPT_COMMON_HELP_00", t);
    msg_write(mcd, "OPT_DISPLAY_HELP_00", t);
    msg_write(mcd, "OPT_TITLE_BASE_04", "Cheats");
    msg_write(mcd, "OPT_DISPLAY_HELP_01", pg[0] >= 0 ? k_cheats[pg[0]].help : t);
    msg_write(mcd, "OPT_DISPLAY_HELP_02", pg[1] >= 0 ? k_cheats[pg[1]].help : t);
    msg_write(mcd, "OPT_DISPLAY_HELP_05", pg[2] >= 0 ? k_cheats[pg[2]].help : t);
    if (pg[0] >= 0) {
        snprintf(t, sizeof t, pg[0] == AP_CHEAT_DMG_TAKEN ? "Damage Taken x%g" : "Halo Gain x%g", g_value[pg[0]]);
        msg_write(mcd, "AP_OPT_SLIDER", t);
    }
    was = g_page;
}

#define BTN_B 1
#define BTN_L 6
#define BTN_R 7
#define BTN_UP 13
#define BTN_DOWN 15

/* A page turn shows through the game's own path: the tab gets the focus again (the cursor up to Sound and back down,
 * pressed for the player), which rebuilds the rows from the list; then the widgets get the cheats' values. */
static int g_refocus;                                 /* 0 idle, 1 press up, 2 up pressed, 3 down pressed, 4 settling */

EXPORT uint32_t ap_opt_frame(uint64_t menu, uint64_t mcd) {
    uint64_t ap_hid_buttons(void);
    void ap_hid_press(uint64_t buttons, int polls);
    load();
    if (!menu) return 0;
    uint64_t disp = rd64h(menu + 192);
    if (!disp || !rd64h(disp + D_ROWS)) return 0;
    if (mcd == 1) {                                   /* after the game's update: the brightness the slider set, back */
        uint64_t save = rd64h(0x71018668d0ull);
        if (g_page && save) memcpy((void *)(uintptr_t)(save + 0x13c), &g_slider_saved, 4);
        return 0;
    }
    if (menu != g_menu) { g_menu = menu; g_page = 0; g_refocus = 0; g_live = 0; }  /* a new screen starts on the game's page */
    uint32_t focus = rd32h(menu + 124), tab = rd32h(menu + 216), asking = rd32h(menu + 120) == 2;
    uint32_t ret = 0;                                 /* 2: the patch moves the slider's dial to its value */
    static uint64_t prev;
    uint64_t b = ap_hid_buttons(), down = b & ~prev;
    prev = b;
    static int wait;
    if (g_refocus == 1) { ap_hid_press(1ull << BTN_UP, 12); g_refocus = 2; wait = 0; }
    else if (g_refocus == 2 && tab == 2 && ++wait > 30) { ap_hid_press(1ull << BTN_DOWN, 12); g_refocus = 3; wait = 0; }
    else if (g_refocus == 3 && tab == 3) { g_refocus = 4; wait = 0; }
    else if (g_refocus == 4 && ++wait > 15) {         /* the widgets were loaded from the save: the cheats' values in, once */
        g_refocus = 0;
        if (g_page) { write_values(disp); g_live = 1; ret |= 2; }
    }
    else if (g_refocus && ++wait > 120) g_refocus = 0;  /* the presses did not take: give up */
    else if (g_refocus == 0) {
        if (g_page && (tab != 3 || asking || (focus == 1 && (down & 1ull << BTN_B))))
            to_page(disp, 0);                         /* leaving the tab or the screen: the game's values back first */
        else if (tab == 3 && focus == 1 && (down & (1ull << BTN_L | 1ull << BTN_R))) {
            to_page(disp, (g_page + (down & 1ull << BTN_R ? 1 : PAGES)) % (PAGES + 1));
            g_refocus = 1;
        } else if (g_page && g_live && focus >= 2) read_page(disp);
        else if (g_page && g_live && focus == 1) g_k_prev = -1;
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_OPTWATCH=<offset>: a watch (watch.c) on the slider widget + offset, once */
        static int armed;
        const char *e = getenv("RECOMPS_AP_TEST_OPTWATCH");
        uint64_t w = rd64h(disp + 208);
        if (e && w && !armed && tab == 3) { void watch_arm_at(uint64_t, int); armed = 1; watch_arm_at(w + (uint64_t)atoi(e), 4); }
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_OPTW=1 logs the slider widget's words that change (research) */
        static int on = -1; static uint32_t last[0x400 / 4]; static uint64_t was;
        if (on < 0) { const char *e = getenv("RECOMPS_AP_TEST_OPTW"); on = e ? atoi(e) : 0; }
        uint64_t w = rd64h(disp + 208 + 8 * (on - 1));
        if (on && w) {
            uint32_t now[0x400 / 4];
            memcpy(now, (void *)(uintptr_t)w, sizeof now);
            if (was == w) for (int i = 0; i < 0x100; i++) if (now[i] != last[i]) printf("ap_optw: +%d %08x -> %08x\n", i * 4, last[i], now[i]);
            was = w; memcpy(last, now, sizeof now);
        }
    }
    if (g_page) {
        write_rows(disp);                             /* any rebuild of the rows takes the cheats' */
        uint64_t save = rd64h(0x71018668d0ull);       /* the slider sets the brightness live (save +0x13c): keep it */
        if (save) memcpy((void *)(uintptr_t)(save + 0x13c), &g_slider_saved, 4);
    }
    {   /* TEST ONLY: RECOMPS_AP_TEST_OPT=1 logs the page state */
        static int dbg = -1; static uint32_t last;
        if (dbg < 0) { const char *e = getenv("RECOMPS_AP_TEST_OPT"); dbg = e && atoi(e); }
        uint32_t st = (uint32_t)g_page | (uint32_t)g_refocus << 4 | (uint32_t)g_live << 8 | tab << 12 | focus << 16;
        if (dbg && st != last) { printf("ap_opt: page %d refocus %d live %d tab %u focus %u on3 %u on4 %u\n", g_page, g_refocus, g_live, tab, focus, rd32h(disp + D_ON3), rd32h(disp + D_ON4)); fflush(stdout); last = st; }
    }
    texts(mcd);
    return ret;
}

/* ---------------------------------------------------------------------------------------------- the pause menu */

/* Auto QTEs: an event script asks how its button prompt (ACT) or mash (RAPID) went by name (ap_patch.py COND block,
 * 0x71007b5d80, rec = the condition record: +0 its type, 3 = a name at +4); with the cheat on the answer is success.
 * -1: the game answers. */
EXPORT int ap_event_cond(uint64_t rec) {
    const char *name = (const char *)(uintptr_t)(rec + 4);   /* type 3: a name */
    uint8_t type = *(const uint8_t *)(uintptr_t)rec;
    {   /* TEST ONLY: RECOMPS_AP_TEST_QTE=2 logs every condition asked (type, and the name of type 3) */
        static char last[40]; char now[40]; const char *e = getenv("RECOMPS_AP_TEST_QTE");
        if (e && atoi(e) == 2) {
            snprintf(now, sizeof now, "%u %.32s", type, type == 3 ? name : "");
            if (strcmp(last, now)) { strcpy(last, now); printf("ap_cond: %s\n", now); fflush(stdout); }
        }
    }
    if (type != 3 || !ap_cheat(AP_CHEAT_AUTO_QTE)) return -1;
    int r = !strcmp(name, "ACT_SUCCES") || !strcmp(name, "RAPID_SUCCES") ? 1
          : !strcmp(name, "ACT_FAILED") || !strcmp(name, "RAPID_FAILED") ? 0 : -1;
    if (r >= 0 && getenv("RECOMPS_AP_TEST_QTE")) { printf("ap_qte: %s -> %d\n", name, r); fflush(stdout); }
    return r;
}

/* Once per frame while the pause menu is up (ap_patch.py PAUSE block, the menu's update 0x7100c63c4c).
 * Pause object: +204.. the item ids (a cinematic's menu: 0 Play, 10 Skip; 13 = none), +248 the cursor, +188 the state
 * (0 the list, 1 the "Skip?" YES/NO question, YES first, 4 closing). Easy Cutscene Skip: on a cinematic's menu the
 * cursor goes to Skip and A is pressed, then A again on the question. */
EXPORT void ap_pause_frame(uint64_t menu) {
    void ap_hid_press(uint64_t buttons, int polls);
    static ULONGLONG seen;                             /* the last frame the menu was up: a gap = a new opening */
    static int frames, stage;
    int coop_pause_frame(uint64_t menu, int easy_skip);     /* coop_sync.c: in co-op a skip waits for both players */
    if (coop_pause_frame(menu, ap_cheat(AP_CHEAT_EASY_SKIP))) return;
    ULONGLONG now = GetTickCount64();
    if (now - seen > 300) { frames = 0; stage = 0; }
    seen = now;
    frames++;
    if (menu && ap_cheat(AP_CHEAT_EASY_SKIP)) {
        uint32_t state = rd32h(menu + 188);
        int skip = -1;
        for (int i = 0; i < 6; i++) if (rd32h(menu + 204 + 4 * i) == 10) skip = i;
        if (skip >= 0 && stage == 0 && state == 0 && frames > 10) { wr32h(menu + 248, (uint32_t)skip); ap_hid_press(1ull << 0, 6); stage = 1; frames = 0; }
        else if (stage == 1 && state == 1 && frames > 10) { ap_hid_press(1ull << 0, 6); stage = 2; }
    }
    static int research = -1;
    if (research < 0) { const char *e = getenv("RECOMPS_AP_TEST_PAUSE"); research = e ? atoi(e) : 0; }
    if (research && menu) {                            /* TEST ONLY: the pause object's words 0..0x200 that change */
        static uint32_t last[0x80]; static uint64_t was;
        uint32_t now[0x80];
        memcpy(now, (void *)(uintptr_t)menu, sizeof now);
        if (was != menu) { printf("ap_pause: menu %llx\n", (unsigned long long)menu); for (int i = 0; i < 0x80; i++) if (now[i]) printf("ap_pause: +%d %08x\n", i * 4, now[i]); }
        else for (int i = 0; i < 0x80; i++) if (now[i] != last[i] && i != 103) printf("ap_pause: +%d %08x -> %08x\n", i * 4, last[i], now[i]);
        fflush(stdout);
        was = menu; memcpy(last, now, sizeof now);
    }
}
