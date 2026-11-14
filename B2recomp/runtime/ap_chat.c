/* [Archipelago] The chat box: the server's messages (every item sent in the multiworld, hints, player chat, joins /
 * leaves / goals) as coloured lines in a see-through panel in the bottom left, lettered with the game's font-20 glyphs
 * (ap_chat_font.bin, ap/tools/build_mod.py chat_font). The AP client (ap_net.c) adds lines from any thread; the renderer
 * (nvn_vk_draw.c chat_draw) asks for the panel each frame: it is composed here on the CPU when a message arrives
 * (rounded, a dark gradient, a thin accent edge, text with a drop shadow) and blended over the picture, fading out
 * CHAT_FADE_MS before CHAT_SHOW_MS has passed since the newest message. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ap_chat.h"

#define CHAT_KEEP 32
#define CHAT_LINES 6                                   /* messages shown */
#define CHAT_ROWS 9                                    /* rows of text at most (long messages wrap) */
static ApChatLine g_lines[CHAT_KEEP];
static int g_head, g_n;
static unsigned g_added;                               /* messages added so far: the panel is composed again when it moves */
static SRWLOCK g_chat_lock = SRWLOCK_INIT;

static const uint8_t k_color[CHAT_COLORS][3] = {
    {236, 236, 240}, {255, 112, 226}, {246, 214, 92}, {186, 160, 255}, {120, 156, 255}, {104, 226, 236}, {255, 132, 116},
    {112, 236, 150}};

void ap_chat_add(const char *text, const uint8_t *colors) {
    AcquireSRWLockExclusive(&g_chat_lock);
    if (g_n == CHAT_KEEP) { g_head = (g_head + 1) % CHAT_KEEP; g_n--; }
    ApChatLine *l = &g_lines[(g_head + g_n++) % CHAT_KEEP];
    size_t n = strlen(text);
    if (n >= sizeof l->text) n = sizeof l->text - 1;
    memcpy(l->text, text, n); l->text[n] = 0;
    memcpy(l->color, colors, n);
    l->t = GetTickCount64();
    g_added++;
    ReleaseSRWLockExclusive(&g_chat_lock);
    printf("ap chat: %s\n", l->text);
    fflush(stdout);
}

/* ---------------------------------------------------------------------------------------------- the font */

static struct { uint8_t *data; const uint8_t *alpha; const uint16_t *gl; int w, cell, tried; } F;

static int font_load(void) {
    if (F.tried) return F.data != NULL;
    F.tried = 1;
    char path[MAX_PATH], exe[MAX_PATH];
    const char *mods = getenv("SSBU_MODS");
    if (mods && *mods && strcmp(mods, "none")) snprintf(path, sizeof path, "%s\\Archipelago\\ap_chat_font.bin", mods);
    else {
        GetModuleFileNameA(NULL, exe, sizeof exe);
        char *q = strrchr(exe, '\\'); if (q) *q = 0;
        snprintf(path, sizeof path, "%s\\..\\mods\\Archipelago\\ap_chat_font.bin", exe);
    }
    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        F.data = n > 12 + 95 * 4 ? malloc((size_t)n) : NULL;
        if (F.data && fread(F.data, 1, (size_t)n, f) != (size_t)n) { free(F.data); F.data = NULL; }
        fclose(f);
        uint32_t hd[3];
        if (F.data) memcpy(hd, F.data, sizeof hd);
        if (F.data && (memcmp(F.data, "APCA", 4) || 12 + 95 * 4 + (long)hd[1] * hd[2] != n)) { free(F.data); F.data = NULL; }
        if (F.data) { F.w = (int)hd[1]; F.cell = (int)hd[2]; F.gl = (const uint16_t *)(F.data + 12); F.alpha = F.data + 12 + 95 * 4; }
    }
    printf("ap chat: font %s%s\n", path, F.data ? "" : " missing or old: the chat box is off");
    fflush(stdout);
    return F.data != NULL;
}

static float glyph_cov(int gx, int gw, float sx, float sy) {    /* the glyph's coverage at (sx, sy) of its cell, bilinear */
    if (sx < 0 || sy < 0 || sx > gw - 1 || sy > F.cell - 1) return 0;
    int x0 = (int)sx, y0 = (int)sy, x1 = x0 + 1 < gw ? x0 + 1 : x0, y1 = y0 + 1 < F.cell ? y0 + 1 : y0;
    float fx = sx - x0, fy = sy - y0;
    const uint8_t *a = F.alpha;
    float t = a[y0 * F.w + gx + x0] * (1 - fx) + a[y0 * F.w + gx + x1] * fx;
    float b = a[y1 * F.w + gx + x0] * (1 - fx) + a[y1 * F.w + gx + x1] * fx;
    return (t * (1 - fy) + b * fy) / 255.0f;
}

/* ---------------------------------------------------------------------------------------------- the panel */

static ApChatPanel g_panel;
static uint8_t *g_px;
static size_t g_px_cap;

/* colour r, g, b at coverage a over the panel pixel; the panel is kept premultiplied (it starts transparent) */
static void put(int x, int y, int r, int g, int b, float a) {
    if (x < 0 || y < 0 || x >= g_panel.w || y >= g_panel.h || a <= 0) return;
    if (a > 1) a = 1;
    uint8_t *p = g_px + ((size_t)y * g_panel.w + x) * 4;
    float k = 1 - a;
    p[0] = (uint8_t)(r * a + p[0] * k); p[1] = (uint8_t)(g * a + p[1] * k); p[2] = (uint8_t)(b * a + p[2] * k);
    p[3] = (uint8_t)(255 * a + p[3] * k);
}

static int char_w(unsigned ch, float k) { return ch >= 32 && ch < 127 ? (int)(F.gl[(ch - 32) * 2 + 1] * k + 0.5f) : 0; }

static void compose(int W, int H, const ApChatLine *lines, int n) {
    int lh = H / 30 > 10 ? H / 30 : 10, pad = lh * 2 / 3, maxw = W * 2 / 5;
    float k = lh * 1.12f / (float)F.cell;             /* the glyph cells have room above and below the letters */
    struct { int line, from, to, w; } rows[64];
    int nr = 0, widest = 0;
    for (int i = 0; i < n; i++) {                       /* each message wrapped at the last space that fits */
        const char *t = lines[i].text;
        int len = (int)strlen(t), from = 0;
        while (from < len && nr < 64) {
            int w = 0, end = from, brk = -1;
            for (; end < len; end++) {
                int cw = char_w((unsigned char)t[end], k);
                if (w + cw > maxw && end > from) break;
                if (t[end] == ' ') brk = end;
                w += cw;
            }
            if (end < len && brk > from) end = brk + 1;
            int rw = 0;
            for (int q = from; q < end; q++) rw += char_w((unsigned char)t[q], k);
            if (t[end - 1] == ' ') rw -= char_w(' ', k);
            rows[nr].line = i; rows[nr].from = from; rows[nr].to = end; rows[nr].w = rw; nr++;
            if (rw > widest) widest = rw;
            from = end;
            while (from < len && t[from] == ' ') from++;
        }
    }
    int first = nr > CHAT_ROWS ? nr - CHAT_ROWS : 0, shown = nr - first;
    g_panel.w = widest + 2 * pad + lh / 6 + lh / 3;   /* the ink and its shadow reach past the last advance */
    g_panel.h = shown * lh + 2 * pad * 3 / 4;
    g_panel.x = W / 64;
    g_panel.y = H - H / 8 - g_panel.h;
    size_t need = (size_t)g_panel.w * g_panel.h * 4;
    if (need > g_px_cap) { free(g_px); g_px = malloc(need); g_px_cap = g_px ? need : 0; }
    if (!g_px) { g_panel.w = g_panel.h = 0; return; }
    memset(g_px, 0, need);
    /* the panel: rounded, a dark blue-black gradient, a thin light edge and an accent along the left */
    float rad = lh * 0.45f;
    for (int y = 0; y < g_panel.h; y++)
        for (int x = 0; x < g_panel.w; x++) {
            float dx = x + 0.5f < rad ? rad - (x + 0.5f) : x + 0.5f > g_panel.w - rad ? x + 0.5f - (g_panel.w - rad) : 0;
            float dy = y + 0.5f < rad ? rad - (y + 0.5f) : y + 0.5f > g_panel.h - rad ? y + 0.5f - (g_panel.h - rad) : 0;
            float d = sqrtf(dx * dx + dy * dy) - rad;    /* < 0 inside the rounded rectangle */
            float in = d < -0.5f ? 1 : d > 0.5f ? 0 : 0.5f - d;
            if (in <= 0) continue;
            float v = (float)y / (float)(g_panel.h - 1);
            float a = (0.50f + 0.18f * v) * in;
            put(x, y, (int)(14 + 6 * (1 - v)), (int)(12 + 6 * (1 - v)), (int)(26 + 10 * (1 - v)), a);
            if (d > -1.5f) put(x, y, 150, 140, 210, 0.35f * in);                                 /* the edge */
            if (x < lh / 6 + 1 && y > rad * 0.5f && y < g_panel.h - rad * 0.5f) put(x, y, 196, 120, 255, 0.85f * in);   /* the accent */
        }
    /* the text: a soft shadow, then the letters in their colours */
    int base_y = pad * 3 / 8;
    for (int pass = 0; pass < 2; pass++)
        for (int r = first; r < nr; r++) {
            const ApChatLine *l = &lines[rows[r].line];
            int x = pad + lh / 6, y = base_y + (r - first) * lh - (int)(lh * 0.06f);
            for (int q = rows[r].from; q < rows[r].to; q++) {
                unsigned ch = (unsigned char)l->text[q];
                if (ch < 32 || ch >= 127) continue;
                int gx = F.gl[(ch - 32) * 2], gw = F.gl[(ch - 32) * 2 + 1], dw = char_w(ch, k), dh = (int)(F.cell * k + 0.5f);
                const uint8_t *c = k_color[l->color[q] < CHAT_COLORS ? l->color[q] : 0];
                if (ch != ' ')
                    for (int py = 0; py < dh; py++)
                        for (int px = 0; px < dw; px++) {
                            float cov = glyph_cov(gx, gw, (px + 0.5f) / k - 0.5f, (py + 0.5f) / k - 0.5f);
                            if (pass == 0) { if (cov > 0) put(x + px + 1 + lh / 24, y + py + 1 + lh / 24, 0, 0, 0, cov * 0.75f); }
                            else put(x + px, y + py, c[0], c[1], c[2], cov);
                        }
                x += dw;
            }
        }
}

const ApChatPanel *ap_chat_panel(int W, int H) {
    static unsigned made_added = ~0u;
    static int made_w, made_h;
    if (!font_load()) return NULL;
    ApChatLine lines[CHAT_LINES];
    int n = 0;
    ULONGLONG age = ~0ull;
    unsigned added;
    AcquireSRWLockShared(&g_chat_lock);
    added = g_added;
    if (g_n) age = GetTickCount64() - g_lines[(g_head + g_n - 1) % CHAT_KEEP].t;
    if (age < CHAT_SHOW_MS) {
        int first = g_n > CHAT_LINES ? g_n - CHAT_LINES : 0;
        for (int i = first; i < g_n; i++) lines[n++] = g_lines[(g_head + i) % CHAT_KEEP];
    }
    ReleaseSRWLockShared(&g_chat_lock);
    g_panel.alpha = n ? (age > CHAT_SHOW_MS - CHAT_FADE_MS ? (float)(CHAT_SHOW_MS - age) / CHAT_FADE_MS : 1.0f) : 0;
    if (n && (added != made_added || W != made_w || H != made_h)) {
        compose(W, H, lines, n);
        made_added = added; made_w = W; made_h = H;
        g_panel.version++;
    }
    g_panel.px = g_px;
    if (!g_panel.w || !g_panel.h) g_panel.alpha = 0;
    return &g_panel;
}

/* ---------------------------------------------------------------------------------------------- the banner */

/* One line at the top middle (co-op: "Waiting for your partner 1/2", "Skip 1/2 ..."), in the chat box's style; any
 * thread sets it, "" hides it. Composed with compose() into its own panel (the chat box's buffers are swapped out). */
static char g_banner[120];
static unsigned g_banner_ver;
static ApChatPanel g_bpanel;
static uint8_t *g_bpx;
static size_t g_bpx_cap;

void ap_banner_set(const char *text) {
    AcquireSRWLockExclusive(&g_chat_lock);
    if (strncmp(g_banner, text, sizeof g_banner - 1)) {
        strncpy(g_banner, text, sizeof g_banner - 1); g_banner[sizeof g_banner - 1] = 0;
        g_banner_ver++;
        if (text[0]) { printf("ap banner: %s\n", text); fflush(stdout); }
    }
    ReleaseSRWLockExclusive(&g_chat_lock);
}

const ApChatPanel *ap_banner_panel(int W, int H) {
    static unsigned made_ver = ~0u;
    static int made_w, made_h;
    if (!font_load()) return NULL;
    ApChatLine line;
    unsigned ver;
    memset(&line, 0, sizeof line);
    AcquireSRWLockShared(&g_chat_lock);
    ver = g_banner_ver;
    strncpy(line.text, g_banner, sizeof line.text - 1);
    ReleaseSRWLockShared(&g_chat_lock);
    if (!line.text[0]) { g_bpanel.alpha = 0; return &g_bpanel; }
    if (ver != made_ver || W != made_w || H != made_h) {
        ApChatPanel keep = g_panel; uint8_t *kpx = g_px; size_t kcap = g_px_cap;   /* compose() draws into g_panel / g_px */
        g_panel = g_bpanel; g_px = g_bpx; g_px_cap = g_bpx_cap;
        compose(W, H, &line, 1);
        g_bpanel = g_panel; g_bpx = g_px; g_bpx_cap = g_px_cap;
        g_panel = keep; g_px = kpx; g_px_cap = kcap;
        g_bpanel.x = (W - g_bpanel.w) / 2;
        g_bpanel.y = H / 18;
        g_bpanel.version++;
        made_ver = ver; made_w = W; made_h = H;
    }
    g_bpanel.px = g_bpx;
    g_bpanel.alpha = g_bpanel.w && g_bpanel.h ? 1.0f : 0;
    return &g_bpanel;
}
