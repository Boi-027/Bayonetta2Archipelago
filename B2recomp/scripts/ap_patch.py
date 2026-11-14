"""Archipelago: an ARCHIPELAGO entry on the title's main menu wheel. Patches the lifted C in place; setup.py's lift step
runs it after prompt_fix.py.

    python scripts/ap_patch.py [--gen=build/gen_eh]

The main menu (STORY, TAG CLIMAX, UMBRAN TEARS OF BLOOD, ??? / GALLERY, OPTIONS) is one wheel of five. Its cursor is an
int that is never wrapped ([0x710186dd08] = 0x710195e9c8; up / down add -1 / +1 in the title controller 0x7100b6132c);
three places take it modulo 5:
  - 0x7100d5959c (wheel part, kind, cursor + offset): the message ID of an entry's label (TIT_BASE_01 ...), hashed by
    0x7100dd220c, looked up in the menu's message file (mm/mmTIT001_us.dat);
  - 0x7100d58ae0 (same arguments): the help line under the wheel (TIT_HELP_01 ...);
  - 0x7100b61800, in the title controller's A press: cursor modulo 5 -> the jump table at 0x710152daf8.
Kind 0 is the main menu (the same wheel shows the other title menus with kinds 1 and 2: left as they are).
The menu itself is the runtime's (runtime/ap_ui.c, found with GetProcAddress on the exe):
  - the label and help lookups ask ap_menu_label which message an entry shows: the game's entries keep their own
    number (so every modulo 5 of the game is unchanged), ARCHIPELAGO and the connect menu's entries are the mod's
    messages (ap/tools/build_mod.py adds them to mmTIT001_us.dat; without the mod those entries are blank);
  - the A press asks ap_menu_pick: an entry of the game's goes on to the game's table, one of ours leaves the title
    controller the way a press the game ignores does;
  - the title controller 0x7100b6132c first calls ap_menu_frame (B in the connect menu, text editing, the help line's
    text); it may skip the game's controller for that frame and set the wheel again (0x7100d58950 with a new cursor).
Without those exports in the runtime, or with RECOMPS_AP_MENU=0, the main menu is the game's own five entries.
Gameplay: the save object's play-time update 0x7100d9293c(save, dt), called once per game frame, first calls the
runtime's ap_game_frame(save) (runtime/ap_game.c: items received), and the chapter result commit
0x7100d94274(save, chapter, ...) first calls ap_chapter_done(save, chapter) (the chapter and its verses are checks).
A lift these sites do not match is left as it is, with a message.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MARK = '/* [ap_patch.py] ARCHIPELAGO on the main menu'

LABEL, HELPFN, TITLE = '7100d5959c', '7100d58ae0', '7100b6132c'
PLAYTIME = '7100d9293c'          # the save object's play-time update: once per game frame (the gameplay client)
CHAPTER = '7100d94274'           # the save object's chapter result commit (save, chapter, results, verses, n)
VERSEFN = '7100d6f104'           # a verse ends: reads the verse's hash, then stores its result (0x7100d93fb4)
OPTFN = '7100c439e8'             # the Options screen's update (cUIOptMenu, once per frame while it is open)
PAUSEFN = '7100c63c4c'           # the pause menu's update (a cinematic's pause menu lists Play / Skip)
CONDFN = '71007b5d80'            # an event script's condition (ACT_SUCCES, RAPID_SUCCES, ... for button prompts)
COOPINPUT = ['7100025c68']   # the player copies its controller into its own input copies (+2112, +2608); then the co-op partner's are rewritten
COOPNEW = ['71009cee34']   # the object factory (pool, request, ID): co-op enemy sync is told of each object made
COOPREQ = ['71009a7740']   # the object request (manager, name, ID, set info): co-op enemy sync keeps each request's set info
COOPVERSE = ['7100d6eb14']   # a verse starts (x0 its hash): co-op waits for both players there
COOPFREEZE = ['71009cfa6c', '71009cfb1c', '71009cfb54', '71009cfc28', '71009cfc60', '71009cfc98']   # the object manager's update passes (0x71009923b4): skipped while co-op waits
COOPSKIP = ['710003e8f4']   # player functions not run for the co-op partner (0x710003e8f4: writes the camera's follow block)
VERSE_OLD = 'L_7100d6f288: {  uint64_t a = c->x[8] + 8ULL; c->x[1] = (uint64_t)(rd32(a));  }'
VERSE_NEW = VERSE_OLD + ' ap_verse_end((uint32_t)c->x[1]);   /* [ap_patch.py] */'
PICK_OLD = 'L_7100b61800: {  uint64_t a = c->x[8] + 0ULL; c->x[8] = (uint64_t)(rd32(a));  }'
PICK_NEW = ('L_7100b61800: {  uint64_t a = c->x[8] + 0ULL; c->x[8] = (uint64_t)(ap_menu_pick((int32_t)rd32(a)));  }\n'
            '  if ((uint32_t)c->x[8] == AP_MENU_MINE) goto L_7100b61474;   /* [ap_patch.py] not the game\'s entry */')
EXIT = 'L_7100b61474:'

COMMON = r'''
/* [ap_patch.py] ARCHIPELAGO on the main menu (see scripts/ap_patch.py) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
__declspec(dllimport) void *__stdcall GetModuleHandleA(const char *);
__declspec(dllimport) void *__stdcall GetProcAddress(void *, const char *);
#define AP_MENU_MINE 0x41500000u
typedef struct {
    const char *(*label)(int32_t cursor, int help, int32_t *game_entry);
    int32_t (*pick)(int32_t cursor);
    uint32_t (*frame)(uint64_t tit_mcd, int32_t *cursor);
    int (*active)(void);
} ApHost;
static const ApHost *ap_host(void) {          /* the runtime's connect menu (runtime/ap_ui.c); NULL = the game's own menu */
    static ApHost h;
    static int state;
    if (!state) {
        const char *e = getenv("RECOMPS_AP_MENU");
        void *exe = GetModuleHandleA(0);
        h.label = (const char *(*)(int32_t, int, int32_t *))GetProcAddress(exe, "ap_menu_label");
        h.pick = (int32_t (*)(int32_t))GetProcAddress(exe, "ap_menu_pick");
        h.frame = (uint32_t (*)(uint64_t, int32_t *))GetProcAddress(exe, "ap_menu_frame");
        h.active = (int (*)(void))GetProcAddress(exe, "ap_menu_active");
        state = (!e || atoi(e)) && h.label && h.pick && h.frame && h.active ? 1 : 2;
        if (state == 2) { printf("ap_patch: no connect menu in the runtime, or RECOMPS_AP_MENU=0: the game's own main menu\n"); fflush(stdout); }
    }
    return state == 1 ? &h : NULL;
}
'''
VERSE = r'''
/* a verse ends: the runtime turns its hash into a location (runtime/ap_game.c) */
static void ap_verse_end(uint32_t hash) {
    static void (*end)(uint32_t);
    static int looked;
    if (!looked) { looked = 1; if (ap_host()) end = (void (*)(uint32_t))GetProcAddress(GetModuleHandleA(0), "ap_verse_end"); }
    if (end) end(hash);
}
'''
GAME = r'''
void f_7100d9293c_game(Ctx *c);
/* once per game frame: the runtime's gameplay client sees the save object first (runtime/ap_game.c) */
void f_7100d9293c(Ctx *c) {
    static uint32_t (*frame)(uint64_t);
    static void (*mcds)(uint64_t, uint64_t);
    static int (*spawn)(uint32_t *, uint64_t *, uint64_t *);
    static void (*spawned)(uint64_t);
    static int looked;
    if (!looked) {
        looked = 1;
        if (ap_host()) {
            frame = (uint32_t (*)(uint64_t))GetProcAddress(GetModuleHandleA(0), "ap_game_frame");
            mcds = (void (*)(uint64_t, uint64_t))GetProcAddress(GetModuleHandleA(0), "ap_game_mcds");
            spawn = (int (*)(uint32_t *, uint64_t *, uint64_t *))GetProcAddress(GetModuleHandleA(0), "ap_spawn_take");
            spawned = (void (*)(uint64_t))GetProcAddress(GetModuleHandleA(0), "ap_spawned");
        }
    }
    uint32_t want = frame ? frame(c->x[0]) : 0;
    uint64_t players = rd64(0x71018668f8ULL), bayo = players ? rd64(players + 48) : 0;
    if ((want & 2) && bayo) {                       /* Death Link: the game's own instant kill (HP 0, action 0x11d) */
        uint64_t keep[32];
        memcpy(keep, c->x, sizeof keep);
        c->x[0] = bayo;
        f_7100088c0c(c);
        f_7100b0f884(c);                            /* then the game over, as the enemies' instant kills do */
        memcpy(c->x, keep, sizeof keep);
    }
    uint32_t id; uint64_t name, info;
    if ((want & 4) && spawn && spawn(&id, &name, &info)) {   /* an enemy: the game's own object request (trap) */
        uint64_t keep[32];
        memcpy(keep, c->x, sizeof keep);
        c->x[0] = rd64(0x71018668d8ULL); c->x[1] = name; c->x[2] = id; c->x[3] = info;
        if (c->x[0]) f_71009a7740(c); else c->x[0] = 0;
        if (spawned) spawned(c->x[0]);
        memcpy(c->x, keep, sizeof keep);
    }
    if ((want & 1) && mcds) {                       /* an item popup: the Bewitchment box's message files */
        uint64_t keep[8], acv, com;
        memcpy(keep, c->x, sizeof keep);
        c->x[0] = (uint64_t)(uintptr_t)"ACV"; c->x[1] = 0;
        f_7100bbefe0(c);
        acv = c->x[0] ? rd64(c->x[0] + 8) : 0;
        c->x[0] = (uint64_t)(uintptr_t)"COM"; c->x[1] = 1;
        f_7100bbefe0(c);
        com = c->x[0] ? rd64(c->x[0] + 8) : 0;
        memcpy(c->x, keep, sizeof keep);
        mcds(acv, com);
    }
    f_7100d9293c_game(c);
}
void f_7100d94274_game(Ctx *c);
/* a chapter's results are committed: the runtime sends that chapter and its verses (runtime/ap_game.c) */
void f_7100d94274(Ctx *c) {
    static void (*done)(uint64_t, int);
    static int looked;
    if (!looked) { looked = 1; if (ap_host()) done = (void (*)(uint64_t, int))GetProcAddress(GetModuleHandleA(0), "ap_chapter_done"); }
    if (done) done(c->x[0], (int)(uint32_t)c->x[1]);
    f_7100d94274_game(c);
}
'''
WHEEL = r'''
void f_7100d5959c_game(Ctx *c);
void f_7100d58ae0_game(Ctx *c);
/* kind 0 (the main menu): the runtime says which message an entry shows; the game's entries keep their own number */
static int ap_menu_text(Ctx *c, int help) {
    const ApHost *h = ap_host();
    if (!h || (uint32_t)c->x[1] != 0) return 0;
    int32_t e = 0;
    const char *id = h->label((int32_t)(uint32_t)c->x[2], help, &e);
    if (id) { c->x[0] = (uint64_t)(uintptr_t)id; f_7100dd220c(c); return 1; }   /* a guest pointer is a host pointer */
    c->x[2] = (uint32_t)e;
    return 0;
}
void f_7100d5959c(Ctx *c) { if (!ap_menu_text(c, 0)) f_7100d5959c_game(c); }
void f_7100d58ae0(Ctx *c) { if (!ap_menu_text(c, 1)) f_7100d58ae0_game(c); }
'''
PICK = r'''
static uint32_t ap_menu_pick(int32_t cursor) {
    const ApHost *h = ap_host();
    if (!h) return (uint32_t)cursor;
    int32_t e = h->pick(cursor);
    return e >= 0 ? (uint32_t)e : AP_MENU_MINE;
}
void f_7100b6132c_game(Ctx *c);
/* the title controller: the runtime's connect menu sees each frame first (and may keep the game's from it) */
void f_7100b6132c(Ctx *c) {
    const ApHost *h = ap_host();
    if (h) {
        uint64_t keep[8];
        memcpy(keep, c->x, sizeof keep);
        uint64_t mcd = 0;
        if (h->active()) {                          /* the title menu's messages ("TIT", 1 = mmTIT001): the help line's text */
            c->x[0] = (uint64_t)(uintptr_t)"TIT"; c->x[1] = 1;
            f_7100bbefe0(c);
            mcd = c->x[0] ? rd64(c->x[0] + 8) : 0;
        }
        uint64_t cur_at = rd64(0x710186dd08ULL);
        int32_t cur = (int32_t)rd32(cur_at);
        uint32_t r = h->frame(mcd, &cur);
        if (r & 2) {                                /* the wheel again, at the runtime's cursor */
            uint64_t wheel = rd64(rd64(0x710186dce0ULL));
            wr32(cur_at, (uint32_t)cur);
            wr32(wheel + 516, (uint32_t)cur);
            c->x[0] = wheel; c->x[1] = (uint32_t)cur;
            f_7100d58950(c);
        }
        memcpy(c->x, keep, sizeof keep);
        if (r & 1) return;
    }
    f_7100b6132c_game(c);
}
'''
OPT = r'''
void f_7100c439e8_game(Ctx *c);
/* the Options screen, once per frame: the runtime's Cheats pages on the Display tab see it first (runtime/ap_opt.c) */
void f_7100c439e8(Ctx *c) {
    static uint32_t (*opt)(uint64_t, uint64_t);
    static int looked;
    if (!looked) { looked = 1; if (ap_host()) opt = (uint32_t (*)(uint64_t, uint64_t))GetProcAddress(GetModuleHandleA(0), "ap_opt_frame"); }
    if (opt) {
        uint64_t keep[32], menu = c->x[0], mcd;
        memcpy(keep, c->x, sizeof keep);
        c->x[0] = (uint64_t)(uintptr_t)"OPT"; c->x[1] = 0;      /* mmOPT000, the Options messages */
        f_7100bbefe0(c);
        mcd = c->x[0] ? rd64(c->x[0] + 8) : 0;
        memcpy(c->x, keep, sizeof keep);
        if (opt(menu, mcd) & 2) {                   /* the slider's value was set: its dial moved there (0x7100d0638c) */
            uint64_t disp = rd64(menu + 192), w = disp ? rd64(disp + 208) : 0;
            if (w) { c->x[0] = w; f_7100d0638c(c); }
            memcpy(c->x, keep, sizeof keep);
        }
        f_7100c439e8_game(c);
        opt(menu, 1);                               /* after the game's update (mcd 1): what it rewrote, held */
        return;
    }
    f_7100c439e8_game(c);
}
'''
PAUSE = r'''
void f_7100c63c4c_game(Ctx *c);
/* the pause menu, once per frame: the runtime's Easy Cutscene Skip sees it first (runtime/ap_opt.c) */
void f_7100c63c4c(Ctx *c) {
    static void (*pause)(uint64_t);
    static int looked;
    if (!looked) { looked = 1; if (ap_host()) pause = (void (*)(uint64_t))GetProcAddress(GetModuleHandleA(0), "ap_pause_frame"); }
    if (pause) pause(c->x[0]);
    f_7100c63c4c_game(c);
}
'''
COND = r'''
void f_71007b5d80_game(Ctx *c);
/* an event script's condition (x1 the record): the runtime's Auto QTE may answer the button
   prompts' ACT_SUCCES / ACT_FAILED / RAPID_SUCCES / RAPID_FAILED first (runtime/ap_opt.c; -1 = the game's answer) */
void f_71007b5d80(Ctx *c) {
    static int (*ask)(uint64_t);
    static int looked;
    if (!looked) { looked = 1; if (ap_host()) ask = (int (*)(uint64_t))GetProcAddress(GetModuleHandleA(0), "ap_event_cond"); }
    if (ask) {
        int r = ask(c->x[1]);
        if (r >= 0) { c->x[0] = (uint32_t)r; return; }
    }
    f_71007b5d80_game(c);
}
'''

COOP_HEAD = r"""
/* story co-op (runtime/coop_game.c): player functions that give the co-op partner her own input or skip her */
static int (*ap_coop_skip)(uint64_t);
static void (*ap_coop_input)(uint64_t);
static void (*ap_coop_new)(uint64_t, uint32_t, uint64_t);
static void (*ap_coop_req)(uint64_t, uint32_t, uint64_t);
static void (*ap_coop_verse)(uint32_t);
static int (*ap_coop_frozen)(void);
static int ap_coop_looked;
static void ap_coop_look(void) {
    if (ap_coop_looked) return;
    ap_coop_looked = 1;
    if (!ap_host()) return;
    ap_coop_skip = (int (*)(uint64_t))GetProcAddress(GetModuleHandleA(0), "coop_obj_skip");
    ap_coop_input = (void (*)(uint64_t))GetProcAddress(GetModuleHandleA(0), "coop_obj_input");
    ap_coop_new = (void (*)(uint64_t, uint32_t, uint64_t))GetProcAddress(GetModuleHandleA(0), "coop_obj_new");
    ap_coop_req = (void (*)(uint64_t, uint32_t, uint64_t))GetProcAddress(GetModuleHandleA(0), "coop_obj_req");
    ap_coop_verse = (void (*)(uint32_t))GetProcAddress(GetModuleHandleA(0), "coop_verse_start");
    ap_coop_frozen = (int (*)(void))GetProcAddress(GetModuleHandleA(0), "coop_frozen");
}
"""
COOP_VERSE = r"""
void f_%(fn)s_game(Ctx *c);
void f_%(fn)s(Ctx *c) {
    ap_coop_look();
    if (ap_coop_verse) ap_coop_verse((uint32_t)c->x[0]);
    f_%(fn)s_game(c);
}
"""
COOP_FREEZE = r"""
void f_%(fn)s_game(Ctx *c);
void f_%(fn)s(Ctx *c) {
    ap_coop_look();
    if (ap_coop_frozen && ap_coop_frozen()) return;
    f_%(fn)s_game(c);
}
"""
COOP_REQ = r"""
void f_%(fn)s_game(Ctx *c);
void f_%(fn)s(Ctx *c) {
    uint32_t id = (uint32_t)c->x[2];
    uint64_t info = c->x[3];
    ap_coop_look();
    if (ap_coop_req) ap_coop_req(0, id, info);      /* made inside the call when its files are loaded already */
    f_%(fn)s_game(c);
    if (ap_coop_req && c->x[0]) ap_coop_req(c->x[0], id, info);
}
"""
COOP_NEW = r"""
void f_%(fn)s_game(Ctx *c);
void f_%(fn)s(Ctx *c) {
    uint32_t id = (uint32_t)c->x[2];
    uint64_t req = c->x[1];
    ap_coop_look();
    f_%(fn)s_game(c);
    if (ap_coop_new && c->x[0]) ap_coop_new(c->x[0], id, req);
}
"""
COOP_INPUT = r"""
void f_%(fn)s_game(Ctx *c);
void f_%(fn)s(Ctx *c) {
    uint64_t obj = c->x[0];
    ap_coop_look();
    f_%(fn)s_game(c);
    if (ap_coop_input) ap_coop_input(obj);
}
"""
COOP_SKIP = r"""
void f_%(fn)s_game(Ctx *c);
void f_%(fn)s(Ctx *c) {
    ap_coop_look();
    if (ap_coop_skip && ap_coop_skip(c->x[0])) return;
    f_%(fn)s_game(c);
}
"""


# Chapter Select shows only the chapters Archipelago gave (runtime/ap_game.c ap_chapter_given / ap_card_index): the card
# menu's "is chapter c open" (0x7100d96244(save, c)) and "card of chapter c" (0x7100b2aca0) are wrapped. Applied on its
# own (marker CHAPSEL_MARK), so a gen patched before this was added gets it too.
CHAPSEL = {'7100d96244': r"""
void f_7100d96244_game(Ctx *c);
void f_7100d96244(Ctx *c) {
    ap_chapsel_look();
    int r = ap_chapsel_given ? ap_chapsel_given((uint32_t)c->x[1]) : -1;
    if (r >= 0) { c->x[0] = (uint32_t)r; return; }
    f_7100d96244_game(c);
}
""", '7100b2aca0': r"""
void f_7100b2aca0_game(Ctx *c);
void f_7100b2aca0(Ctx *c) {
    ap_chapsel_look();
    f_7100b2aca0_game(c);
    if (ap_chapsel_card) c->x[0] = (uint32_t)ap_chapsel_card((int)(uint32_t)c->x[0]);
}
"""}
CHAPSEL_MARK = '/* [ap_patch.py] chapsel */'
CHAPSEL_HEAD = CHAPSEL_MARK + r"""
__declspec(dllimport) void *__stdcall GetModuleHandleA(const char *);
__declspec(dllimport) void *__stdcall GetProcAddress(void *, const char *);
static int (*ap_chapsel_given)(uint32_t);
static int (*ap_chapsel_card)(int);
static int ap_chapsel_looked;
static void ap_chapsel_look(void) {
    if (ap_chapsel_looked) return;
    ap_chapsel_looked = 1;
    ap_chapsel_given = (int (*)(uint32_t))GetProcAddress(GetModuleHandleA(0), "ap_chapter_given");
    ap_chapsel_card = (int (*)(int))GetProcAddress(GetModuleHandleA(0), "ap_card_index");
}
"""


def patch_chapsel(gen):
    for fn, wrap in CHAPSEL.items():
        for name in sorted(n for n in os.listdir(gen) if n.startswith('c_') and n.endswith('.c')):
            path = os.path.join(gen, name)
            with open(path, encoding='utf-8', newline='') as f:
                text = f.read()
            head = 'void f_%s(Ctx *c) {' % fn
            if 'void f_%s_game(Ctx *c) {' % fn in text:
                break                                    # done before
            if text.count(head) != 1:
                continue
            nl = '\r\n' if '\r\n' in text else '\n'
            inc = '#include "funcs.h"' + nl
            if text.count(inc) != 1:
                print('ap_patch.py: no single #include "funcs.h" in %s; Chapter Select is the game\'s own' % name)
                return
            body = ('' if CHAPSEL_MARK in text else CHAPSEL_HEAD) + wrap
            text = text.replace(head, 'void f_%s_game(Ctx *c) {' % fn).replace(inc, inc + body.replace('\n', nl))
            with open(path, 'w', encoding='utf-8', newline='') as f:
                f.write(text)
            print('ap_patch.py: Chapter Select: f_%s wrapped in %s' % (fn, name))
            break
        else:
            print('ap_patch.py: f_%s not found; Chapter Select is the game\'s own' % fn)


def main():
    opt = dict(a[2:].split('=', 1) for a in sys.argv[1:] if a.startswith('--') and '=' in a)
    gen = os.path.abspath(opt.get('gen', os.path.join(ROOT, 'build', 'gen_eh')))
    patch_chapsel(gen)
    want = (LABEL, HELPFN, TITLE, PLAYTIME, CHAPTER, VERSEFN, OPTFN, PAUSEFN, CONDFN) + tuple(COOPSKIP) + tuple(COOPINPUT) + tuple(COOPNEW) + tuple(COOPREQ) + tuple(COOPVERSE) + tuple(COOPFREEZE)
    where, texts = {}, {}
    for name in sorted(n for n in os.listdir(gen) if n.startswith('c_') and n.endswith('.c')):
        with open(os.path.join(gen, name), encoding='utf-8', newline='') as f:
            text = f.read()
        found = [fn for fn in want if ('void f_%s(Ctx *c) {' % fn) in text or ('void f_%s_game(Ctx *c) {' % fn) in text]
        for fn in found:
            where[fn] = name
        if found:
            texts[name] = text
    missing = set(want) - set(where)
    if missing:
        print('ap_patch.py: not found in %s: %s; the main menu keeps its five entries'
              % (gen, ', '.join('f_' + m for m in sorted(missing))))
        return
    if any(MARK in texts[n] for n in set(where.values())):
        print('ap_patch.py: already patched (%s)' % ', '.join(sorted(set(where.values()))))
        return
    out = dict(texts)
    for fn in (LABEL, HELPFN, TITLE, PLAYTIME, CHAPTER, OPTFN, PAUSEFN, CONDFN) + tuple(COOPSKIP) + tuple(COOPINPUT) + tuple(COOPNEW) + tuple(COOPREQ) + tuple(COOPVERSE) + tuple(COOPFREEZE):
        name = where[fn]
        head = 'void f_%s(Ctx *c) {' % fn
        if out[name].count(head) != 1:
            print('ap_patch.py: f_%s in %s is not as expected; the main menu keeps its five entries' % (fn, name))
            return
        out[name] = out[name].replace(head, 'void f_%s_game(Ctx *c) {' % fn)
    name = where[TITLE]
    nl = '\r\n' if '\r\n' in out[name] else '\n'
    if out[name].count(PICK_OLD + nl) != 1 or EXIT not in out[name]:
        print('ap_patch.py: f_%s in %s is not as expected at 7100b61800 (another version or a code mod?); the main menu '
              'keeps its five entries' % (TITLE, name))
        return
    out[name] = out[name].replace(PICK_OLD + nl, PICK_NEW.replace('\n', nl) + nl)
    name = where[VERSEFN]
    if out[name].count(VERSE_OLD) != 1:
        print('ap_patch.py: f_%s in %s is not as expected at 7100d6f288; the main menu keeps its five entries' % (VERSEFN, name))
        return
    out[name] = out[name].replace(VERSE_OLD, VERSE_NEW)
    for name in sorted(set(where.values())):
        text = out[name]
        nl = '\r\n' if '\r\n' in text else '\n'
        inc = '#include "funcs.h"' + nl
        if text.count(inc) != 1:
            print('ap_patch.py: no single #include "funcs.h" in %s; the main menu keeps its five entries' % name)
            return
        body = (COMMON + (WHEEL if name in (where[LABEL], where[HELPFN]) else '') + (PICK if name == where[TITLE] else '')
                + (GAME if name == where[PLAYTIME] else '') + (VERSE if name == where[VERSEFN] else '')
                + (OPT if name == where[OPTFN] else '') + (PAUSE if name == where[PAUSEFN] else '')
                + (COND if name == where[CONDFN] else '')
                + (COOP_HEAD + ''.join(COOP_SKIP % {'fn': f} for f in COOPSKIP if where[f] == name)
                   + ''.join(COOP_INPUT % {'fn': f} for f in COOPINPUT if where[f] == name)
                   + ''.join(COOP_NEW % {'fn': f} for f in COOPNEW if where[f] == name)
                   + ''.join(COOP_REQ % {'fn': f} for f in COOPREQ if where[f] == name)
                   + ''.join(COOP_VERSE % {'fn': f} for f in COOPVERSE if where[f] == name)
                   + ''.join(COOP_FREEZE % {'fn': f} for f in COOPFREEZE if where[f] == name)
                   if any(where[f] == name for f in COOPSKIP + COOPINPUT + COOPNEW + COOPREQ + COOPVERSE + COOPFREEZE) else ''))
        assert where[CHAPTER] == where[PLAYTIME], 'the chapter commit is expected beside the play-time update'
        out[name] = text.replace(inc, inc + body.replace('\n', nl))
    for name in sorted(set(where.values())):
        with open(os.path.join(gen, name), 'w', encoding='utf-8', newline='') as f:
            f.write(out[name])
    print('ap_patch.py: ARCHIPELAGO on the main menu (label and help in %s, choice in %s), gameplay client in %s'
          % (where[LABEL], where[TITLE], where[PLAYTIME]))


if __name__ == '__main__':
    main()
