/* [Archipelago] The connection to the Archipelago server: WebSocket over Winsock, the AP handshake, the status line
 * of the connect menu. One background thread per connection attempt; the game's thread only reads the status.
 *
 * Address: "host[:port]" (port 38281 when left out), optionally "ws://..." (or "wss://...": TLS, ap_tls.c).
 * Without a scheme the client tries TLS first and plain WebSocket second, as Archipelago's own clients do.
 * Handshake (https://github.com/ArchipelagoMW/Archipelago/blob/main/docs/network%20protocol.md): the server sends
 * RoomInfo, we send Connect (game "Bayonetta 2", items_handling 0b111), the server answers Connected or
 * ConnectionRefused. Then the connection stays open (ReceivedItems etc. are for the gameplay part). */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ap.h"
#include "ap_json.h"
#include "ap_chat.h"

#define GAME_NAME "Bayonetta 2"
#define DEFAULT_PORT "38281"

static CRITICAL_SECTION g_lock, g_send;              /* g_send: one frame at a time on the wire, from any thread */
static volatile LONG g_lock_init;
static char g_status[96] = "Not connected. Press A to connect.";
static volatile LONG g_changed;
static volatile LONG g_generation;                  /* a new attempt ends the old thread */
static HANDLE g_thread;
static SOCKET g_sock = INVALID_SOCKET;               /* the open connection: a new attempt shuts it to end its recv */

static void lock(void) {
    if (InterlockedCompareExchange(&g_lock_init, 1, 0) == 0) { InitializeCriticalSection(&g_lock); InitializeCriticalSection(&g_send); InterlockedExchange(&g_lock_init, 2); }
    while (g_lock_init != 2) Sleep(0);
    EnterCriticalSection(&g_lock);
}
static void unlock(void) { LeaveCriticalSection(&g_lock); }

static void set_status(LONG gen, const char *fmt, ...) {
    if (gen != g_generation) return;                /* an old attempt says nothing any more */
    char t[sizeof g_status];
    va_list a;
    va_start(a, fmt);
    vsnprintf(t, sizeof t, fmt, a);
    va_end(a);
    lock();
    strcpy(g_status, t);
    unlock();
    InterlockedExchange(&g_changed, 1);
    printf("ap: %s\n", t);
    fflush(stdout);
}

void ap_net_status(char *out, size_t n) { lock(); snprintf(out, n, "%s", g_status); unlock(); }
int ap_net_status_changed(void) { return InterlockedExchange(&g_changed, 0) != 0; }

/* ---------------------------------------------------------------------------------------------- the stream */

typedef struct {
    SOCKET s;
    void *tls;                                       /* ap_tls.c session, or NULL for plain TCP */
    LONG gen;
    int deflate;                                     /* the server compresses its messages (permessage-deflate) */
} Conn;

static int raw_send(Conn *c, const void *p, int n) {
    if (c->tls) return ap_tls_send(c->tls, p, n);
    const char *b = p;
    while (n > 0) { int k = send(c->s, b, n, 0); if (k <= 0) return -1; b += k; n -= k; }
    return 0;
}
static int raw_recv(Conn *c, void *p, int n) {     /* exactly n bytes; -1 = closed or error */
    char *b = p;
    while (n > 0) {
        int k = c->tls ? ap_tls_recv(c->tls, b, n) : recv(c->s, b, n, 0);
        if (k <= 0 || c->gen != g_generation) return -1;
        b += k; n -= k;
    }
    return 0;
}

static SOCKET tcp_connect(const char *host, const char *port) {
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res)) return INVALID_SOCKET;
    SOCKET s = INVALID_SOCKET;
    for (struct addrinfo *a = res; a && s == INVALID_SOCKET; a = a->ai_next) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        connect(s, a->ai_addr, (int)a->ai_addrlen);
        fd_set w, e; FD_ZERO(&w); FD_ZERO(&e); FD_SET(s, &w); FD_SET(s, &e);
        struct timeval tv = {8, 0};
        if (select(0, NULL, &w, &e, &tv) != 1 || FD_ISSET(s, &e)) { closesocket(s); s = INVALID_SOCKET; continue; }
        nb = 0;
        ioctlsocket(s, FIONBIO, &nb);
    }
    freeaddrinfo(res);
    if (s != INVALID_SOCKET) { DWORD t = 0; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof t); }
    return s;
}

/* ---------------------------------------------------------------------------------------------- WebSocket */

static void random_bytes(uint8_t *p, int n) {
    static uint64_t x;
    if (!x) { LARGE_INTEGER t; QueryPerformanceCounter(&t); x = (uint64_t)t.QuadPart ^ ((uint64_t)GetCurrentProcessId() << 32) ^ 0x9e3779b97f4a7c15ull; }
    for (int i = 0; i < n; i++) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; p[i] = (uint8_t)x; }
}

static void base64(const uint8_t *in, int n, char *out) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = t[v >> 18 & 63]; out[o++] = t[v >> 12 & 63];
        out[o++] = i + 1 < n ? t[v >> 6 & 63] : '='; out[o++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[o] = 0;
}

static int ws_handshake(Conn *c, const char *host, const char *port) {
    uint8_t key[16];
    char k64[32], req[512], resp[2048];
    random_bytes(key, sizeof key);
    base64(key, sizeof key, k64);
    int n = snprintf(req, sizeof req, "GET / HTTP/1.1\r\nHost: %s:%s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nUser-Agent: B2recomp-Archipelago\r\n"
                     "Sec-WebSocket-Extensions: permessage-deflate; client_no_context_takeover; server_no_context_takeover\r\n\r\n",
                     host, port, k64);
    if (raw_send(c, req, n)) return -1;
    int got = 0;
    while (got < (int)sizeof resp - 1) {             /* the response, up to the empty line */
        if (raw_recv(c, resp + got, 1)) return -1;
        got++;
        if (got >= 4 && !memcmp(resp + got - 4, "\r\n\r\n", 4)) break;
    }
    resp[got] = 0;
    for (char *q = resp; *q; q++) if (*q >= 'A' && *q <= 'Z') *q = (char)(*q + 32);
    /* each message on its own (server_no_context_takeover): ap_inflate needs no window across messages */
    c->deflate = strstr(resp, "permessage-deflate") && strstr(resp, "server_no_context_takeover");
    return strncmp(resp, "http/1.1 101", 12) ? -1 : 0;
}

static int ws_send_frame(Conn *c, int opcode, const char *p, size_t n) {
    uint8_t h[14], mask[4];
    int k = 0;
    h[k++] = (uint8_t)(0x80 | opcode);
    if (n < 126) h[k++] = (uint8_t)(0x80 | n);
    else if (n < 65536) { h[k++] = 0x80 | 126; h[k++] = (uint8_t)(n >> 8); h[k++] = (uint8_t)n; }
    else { h[k++] = 0x80 | 127; for (int i = 7; i >= 0; i--) h[k++] = (uint8_t)((uint64_t)n >> (8 * i)); }
    random_bytes(mask, 4);
    memcpy(h + k, mask, 4); k += 4;
    char *b = malloc(n + k);
    if (!b) return -1;
    memcpy(b, h, k);
    for (size_t i = 0; i < n; i++) b[k + i] = (char)(p[i] ^ mask[i & 3]);
    lock(); unlock();                                /* (g_send exists) */
    EnterCriticalSection(&g_send);
    int r = raw_send(c, b, (int)(n + k));
    LeaveCriticalSection(&g_send);
    free(b);
    return r;
}
static int ws_send_text(Conn *c, const char *s) { return ws_send_frame(c, 1, s, strlen(s)); }

/* the next whole text message (fragments joined); pings answered on the way. NULL = closed. Free it. */
static char *ws_recv_text(Conn *c) {
    char *msg = NULL;
    size_t len = 0;
    int compressed = -1;                             /* RSV1 of the message's first frame */
    for (;;) {
        uint8_t h[2];
        if (raw_recv(c, h, 2)) break;
        int fin = h[0] >> 7, op = h[0] & 15, masked = h[1] >> 7;
        if (op < 8 && compressed < 0) compressed = c->deflate && (h[0] & 0x40);
        uint64_t n = h[1] & 127;
        if (n == 126) { uint8_t e[2]; if (raw_recv(c, e, 2)) break; n = (uint64_t)e[0] << 8 | e[1]; }
        else if (n == 127) { uint8_t e[8]; if (raw_recv(c, e, 8)) break; n = 0; for (int i = 0; i < 8; i++) n = n << 8 | e[i]; }
        uint8_t mask[4] = {0};
        if (masked && raw_recv(c, mask, 4)) break;
        if (n > (64u << 20)) break;                  /* 64 MB: not a message we expect */
        char *p = malloc((size_t)n + 1);
        if (!p || (n && raw_recv(c, p, (int)n))) { free(p); break; }
        for (uint64_t i = 0; masked && i < n; i++) p[i] ^= mask[i & 3];
        if (op == 9) { ws_send_frame(c, 10, p, (size_t)n); free(p); continue; }      /* ping -> pong */
        if (op == 10) { free(p); continue; }
        if (op == 8) { free(p); break; }                                            /* close */
        char *m = realloc(msg, len + (size_t)n + 1);
        if (!m) { free(p); break; }
        msg = m;
        memcpy(msg + len, p, (size_t)n);
        len += (size_t)n;
        msg[len] = 0;
        free(p);
        if (!fin) continue;
        if (!compressed) return msg;
        char *z = realloc(msg, len + 4);              /* the sync flush the server leaves out */
        if (!z) break;
        memcpy(z + len, "\x00\x00\xff\xff", 4);
        size_t plain_len;
        char *plain = (char *)ap_inflate((const uint8_t *)z, len + 4, &plain_len);
        free(z);
        return plain;                                 /* NULL (broken data) ends the connection */
    }
    free(msg);
    return NULL;
}

/* ---------------------------------------------------------------------------------------------- the AP session */

typedef struct { char host[128], port[8], address[160], slot[96], password[96]; int scheme; LONG gen; } Attempt;   /* scheme 0 any, 1 ws, 2 wss */

static const char *refusal(const char *e) {
    if (!strcmp(e, "InvalidSlot")) return "no slot of that name in this room";
    if (!strcmp(e, "InvalidGame")) return "that slot is not Bayonetta 2";
    if (!strcmp(e, "IncompatibleVersion")) return "the server wants a newer client";
    if (!strcmp(e, "InvalidPassword")) return "wrong password";
    if (!strcmp(e, "InvalidItemsHandling")) return "item handling refused";
    return e;
}

static void uuid(char *out) {                        /* one per install, kept in the ini (the server's "uuid") */
    char ini[MAX_PATH], exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, sizeof exe);
    char *s = strrchr(exe, '\\'); if (s) *s = 0;
    snprintf(ini, sizeof ini, "%s\\..\\config\\archipelago.ini", exe);
    GetPrivateProfileStringA("connection", "uuid", "", out, 40, ini);
    if (out[0]) return;
    uint8_t r[16];
    random_bytes(r, 16);
    for (int i = 0; i < 16; i++) sprintf(out + 2 * i, "%02x", r[i]);
    WritePrivateProfileStringA("connection", "uuid", out, ini);
}

/* ---------------------------------------------------------------------------------------------- items and checks */

static char g_seed[64], g_slot_name[96];
static int g_slot;
static Conn *g_conn;                                 /* the logged-in connection (under g_lock) */
static ApItem *g_items;                              /* every item the server sent, by index */
static int g_items_n, g_items_cap;
static int64_t *g_checks;                            /* locations found; sent ones flagged */
static uint8_t *g_checks_sent;
static int g_checks_n, g_checks_cap;

static void add_item(J *k) {
    if (g_items_n == g_items_cap) {
        int cap = g_items_cap ? g_items_cap * 2 : 256;
        ApItem *n = realloc(g_items, cap * sizeof *n);
        if (!n) return;
        g_items = n; g_items_cap = cap;
    }
    ApItem *it = &g_items[g_items_n++];
    it->item = (int64_t)j_num(j_get(k, "item"), 0);
    it->location = (int64_t)j_num(j_get(k, "location"), 0);
    it->player = (int)j_num(j_get(k, "player"), 0);
    it->flags = (int)j_num(j_get(k, "flags"), 0);
}

static int find_check(int64_t loc) { for (int i = 0; i < g_checks_n; i++) if (g_checks[i] == loc) return i; return -1; }
static void mark_sent(int64_t loc) {                 /* (under g_lock) */
    int i = find_check(loc);
    if (i < 0) {
        if (g_checks_n == g_checks_cap) {
            int cap = g_checks_cap ? g_checks_cap * 2 : 256;
            int64_t *n = realloc(g_checks, cap * sizeof *n);
            if (n) g_checks = n;
            uint8_t *s = realloc(g_checks_sent, cap);
            if (s) g_checks_sent = s;
            if (!n || !s) return;                    /* out of memory: this check is retried when found again */
            g_checks_cap = cap;
        }
        i = g_checks_n++;
        g_checks[i] = loc;
    }
    g_checks_sent[i] = 1;
}

static void flush_checks(Conn *c) {                  /* send every location not sent yet */
    char buf[8192];
    JBuf b = {buf, sizeof buf, 0};
    int any = 0;
    lock();
    jb_puts(&b, "[{\"cmd\":\"LocationChecks\",\"locations\":[");
    for (int i = 0; i < g_checks_n && b.len < sizeof buf - 64; i++) {
        if (g_checks_sent[i]) continue;
        char t[24];
        snprintf(t, sizeof t, "%s%lld", any ? "," : "", (long long)g_checks[i]);
        jb_puts(&b, t);
        g_checks_sent[i] = 1;
        any = 1;
    }
    jb_puts(&b, "]}]");
    unlock();
    if (any) ws_send_text(c, buf);
}

/* A location found in the game (any thread). Sent now when logged in, else at the next login. */
void ap_net_check(int64_t location) {
    lock();
    int known = find_check(location) >= 0;
    if (!known) {
        mark_sent(location);
        g_checks_sent[find_check(location)] = 0;
    }
    Conn *c = g_conn;
    unlock();
    if (!known && c) flush_checks(c);
}

/* A location is checked (found here or by the server's list). */
int ap_net_checked(int64_t location) {
    lock();
    int i = find_check(location);
    unlock();
    return i >= 0;
}

/* The goal is done. */
void ap_net_goal(void) {
    lock();
    Conn *c = g_conn;
    unlock();
    if (c) ws_send_text(c, "[{\"cmd\":\"StatusUpdate\",\"status\":30}]");
}

/* Items from index `from` on (into out, at most max); returns how many. *total = the server's item count. */
int ap_net_items(int from, ApItem *out, int max, int *total) {
    lock();
    int n = 0;
    for (int i = from; i < g_items_n && n < max; i++) out[n++] = g_items[i];
    *total = g_items_n;
    unlock();
    return n;
}

/* ---------------------------------------------------------------------------------------------- names and popups */

/* Slot -> player name and game (Connected), (game, item ID) -> item name (DataPackage), and the popups the game
 * shows for items sent and received (PrintJSON ItemSend), taken by ap_net_popup (runtime/ap_game.c). */
#define MAX_SLOTS 256
static char *g_player[MAX_SLOTS], *g_game[MAX_SLOTS];
typedef struct { char *game; int64_t id; char *name; } ItemName;
static ItemName *g_names;
static int g_names_n, g_names_cap;
#define POPUPS 32
static ApPopup g_popups[POPUPS];
static int g_pop_head, g_pop_n;

static char *dup(const char *s) { size_t n = strlen(s) + 1; char *d = malloc(n); if (d) memcpy(d, s, n); return d; }

static const char *item_name(const char *game, int64_t id, char *buf, size_t n) {   /* (under g_lock) */
    for (int i = 0; i < g_names_n; i++)
        if (g_names[i].id == id && game && !strcmp(g_names[i].game, game)) return g_names[i].name;
    snprintf(buf, n, "Item %lld", (long long)id);
    return buf;
}
static const char *player_name(int slot) {
    return slot >= 0 && slot < MAX_SLOTS && g_player[slot] ? g_player[slot] : slot == 0 ? "Archipelago" : "someone";
}

static char g_slot_data[2048];                       /* the Connected packet's slot_data, "key=value;" pairs */

/* A number from the seed's slot_data (Connected), or def. */
int ap_net_slot_int(const char *key, int def) {
    char want[72];
    snprintf(want, sizeof want, ";%s=", key);
    lock();
    const char *at = strstr(g_slot_data, want);
    int v = at ? atoi(at + strlen(want)) : def;
    unlock();
    return v;
}

static void on_connected(J *p, Conn *c) {
    {   /* slot_data: the numbers and booleans, flattened */
        JBuf b = {g_slot_data, sizeof g_slot_data - 1, 0};
        g_slot_data[0] = 0;
        jb_puts(&b, ";");
        for (J *k = j_get(p, "slot_data") ? j_get(p, "slot_data")->kid : NULL; k; k = k->next) {
            if (!k->key || (k->type != J_NUM && k->type != J_BOOL)) continue;
            char t[96];
            snprintf(t, sizeof t, "%s=%d;", k->key, (int)k->num);
            jb_puts(&b, t);
        }
        g_slot_data[b.len] = 0;
    }
    for (J *k = j_get(p, "players") ? j_get(p, "players")->kid : NULL; k; k = k->next) {
        int s = (int)j_num(j_get(k, "slot"), -1);
        if (s < 0 || s >= MAX_SLOTS) continue;
        free(g_player[s]);
        g_player[s] = dup(j_str(j_get(k, "alias"), j_str(j_get(k, "name"), "?")));
    }
    char buf[4096];
    JBuf b = {buf, sizeof buf, 0};
    jb_puts(&b, "[{\"cmd\":\"GetDataPackage\",\"games\":[");
    int any = 0;
    for (J *k = j_get(p, "slot_info") ? j_get(p, "slot_info")->kid : NULL; k; k = k->next) {
        int s = k->key ? atoi(k->key) : -1;
        const char *game = j_str(j_get(k, "game"), "");
        if (s < 0 || s >= MAX_SLOTS || !*game) continue;
        int seen = 0;
        for (int i = 0; i < MAX_SLOTS; i++) if (g_game[i] && !strcmp(g_game[i], game)) seen = 1;
        free(g_game[s]);
        g_game[s] = dup(game);
        if (!seen) { if (any) jb_puts(&b, ","); jb_str(&b, game); any = 1; }
    }
    jb_puts(&b, "]}]");
    if (any) ws_send_text(c, buf);
}

typedef struct { int64_t id; char *name; } LocName;  /* this game's locations (DataPackage) */
static LocName *g_locs;
static int g_locs_n;
static int64_t *g_missing;                            /* this seed's locations not found yet (Connected): hints pick from these */
static int g_missing_n;                               /* (a location of the game that is not in the seed kills the connection) */
typedef struct { char *game; int64_t id; char *name; } OtherLoc;   /* every game's locations, for the chat box */
static OtherLoc *g_alocs;
static int g_alocs_n, g_alocs_cap;
static const char *location_name(const char *game, int64_t id, char *buf, size_t n) {   /* (under g_lock) */
    for (int i = 0; i < g_alocs_n; i++)
        if (g_alocs[i].id == id && game && !strcmp(g_alocs[i].game, game)) return g_alocs[i].name;
    snprintf(buf, n, "Location %lld", (long long)id);
    return buf;
}

static void on_data_package(J *p) {                  /* (under g_lock) */
    J *games = j_get(j_get(p, "data"), "games");
    for (J *g = games ? games->kid : NULL; g; g = g->next)
        if (g->key && !strcmp(g->key, GAME_NAME))
            for (J *l = j_get(g, "location_name_to_id") ? j_get(g, "location_name_to_id")->kid : NULL; l; l = l->next) {
                LocName *n = realloc(g_locs, (g_locs_n + 1) * sizeof *n);
                if (!n) break;
                g_locs = n;
                g_locs[g_locs_n++] = (LocName){(int64_t)j_num(l, 0), dup(l->key ? l->key : "?")};
            }
    for (J *g = games ? games->kid : NULL; g; g = g->next)
        for (J *l = j_get(g, "location_name_to_id") ? j_get(g, "location_name_to_id")->kid : NULL; l; l = l->next) {
            if (g_alocs_n == g_alocs_cap) {
                int cap = g_alocs_cap ? g_alocs_cap * 2 : 1024;
                OtherLoc *n = realloc(g_alocs, cap * sizeof *n);
                if (!n) break;
                g_alocs = n; g_alocs_cap = cap;
            }
            g_alocs[g_alocs_n++] = (OtherLoc){dup(g->key ? g->key : ""), (int64_t)j_num(l, 0), dup(l->key ? l->key : "?")};
        }
    for (J *g = games ? games->kid : NULL; g; g = g->next)
        for (J *it = j_get(g, "item_name_to_id") ? j_get(g, "item_name_to_id")->kid : NULL; it; it = it->next) {
            if (g_names_n == g_names_cap) {
                int cap = g_names_cap ? g_names_cap * 2 : 1024;
                ItemName *n = realloc(g_names, cap * sizeof *n);
                if (!n) return;
                g_names = n; g_names_cap = cap;
            }
            g_names[g_names_n++] = (ItemName){dup(g->key ? g->key : ""), (int64_t)j_num(it, 0), dup(it->key ? it->key : "?")};
        }
}

__attribute__((unused)) static void popup(int kind, int64_t item, int ours, const char *fmt, ...) {  /* (under g_lock); unused: the chat box shows these now */
    if (g_pop_n == POPUPS) { g_pop_head = (g_pop_head + 1) % POPUPS; g_pop_n--; }   /* full: the oldest goes */
    ApPopup *q = &g_popups[(g_pop_head + g_pop_n++) % POPUPS];
    q->kind = kind;
    q->item = item;
    q->ours = ours;
    va_list v;
    va_start(v, fmt);
    vsnprintf(q->text, sizeof q->text, fmt, v);
    va_end(v);
}

/* A chat line, coloured part by part (the chat box replaced the game's popups for these messages). */
typedef struct { char text[200]; uint8_t color[200]; int n; } ChatBuild;
static void chat_put(ChatBuild *b, const char *t, int color) {
    for (; *t && b->n < (int)sizeof b->text - 1; t++) {
        unsigned char ch = (unsigned char)*t;
        if (ch >= 0x80 && (ch & 0xc0) == 0x80) continue;        /* UTF-8: one '?' per character */
        {   /* the game's font has none of these: the nearest it has ('#' is dropped) */
            static const char from[] = "[]{}<>=*$^~|`\\", to[] = "()()()-+S   '/";
            const char *f = ch ? strchr(from, ch) : NULL;
            if (f) ch = (unsigned char)to[f - from];
            if (ch == '#') continue;
        }
        b->text[b->n] = ch == '\n' || ch == '\t' ? ' ' : ch < 32 || ch > 126 ? '?' : (char)ch;
        b->color[b->n++] = (uint8_t)color;
    }
    b->text[b->n] = 0;
}
static int item_color(int flags) { return flags & 4 ? CHAT_TRAP : flags & 1 ? CHAT_PROGRESSION : flags & 2 ? CHAT_USEFUL : CHAT_FILLER; }
static const char *slot_game(int s) { return s >= 0 && s < MAX_SLOTS ? g_game[s] : NULL; }

static void on_print(J *p) {                         /* (under g_lock) */
    ChatBuild b = {{0}, {0}, 0};
    for (J *d = j_get(p, "data") ? j_get(p, "data")->kid : NULL; d; d = d->next) {
        const char *type = j_str(j_get(d, "type"), "text"), *t = j_str(j_get(d, "text"), "");
        int owner = (int)j_num(j_get(d, "player"), -1), color = CHAT_TEXT;
        char buf[48];
        if (!strcmp(type, "player_id")) { int s = atoi(t); t = player_name(s); color = s == g_slot ? CHAT_ME : CHAT_PLAYER; }
        else if (!strcmp(type, "item_id")) { t = item_name(slot_game(owner), atoll(t), buf, sizeof buf); color = item_color((int)j_num(j_get(d, "flags"), 0)); }
        else if (!strcmp(type, "location_id")) { t = location_name(slot_game(owner), atoll(t), buf, sizeof buf); color = CHAT_LOCATION; }
        else if (!strcmp(type, "player_name")) color = CHAT_PLAYER;
        else if (!strcmp(type, "item_name")) color = item_color((int)j_num(j_get(d, "flags"), 0));
        else if (!strcmp(type, "location_name")) color = CHAT_LOCATION;
        else if (!strcmp(type, "color")) {
            const char *c = j_str(j_get(d, "color"), "");
            color = strstr(c, "green") ? CHAT_LOCATION : strstr(c, "red") ? CHAT_TRAP : strstr(c, "yellow") ? CHAT_PLAYER
                  : strstr(c, "magenta") ? CHAT_ME : strstr(c, "blue") ? CHAT_USEFUL : strstr(c, "cyan") ? CHAT_FILLER : CHAT_TEXT;
        }
        chat_put(&b, t, color);
    }
    if (b.n) ap_chat_add(b.text, b.color);
}

/* A hint for the player (a book was read): a location of this world not found yet, scouted (LocationScouts, no
 * broadcast); the answer (LocationInfo) becomes a hint popup "<location>: <item> (for <player>)". */
void ap_net_hint(void) {
    lock();
    Conn *c = g_conn;
    int64_t pick = 0;
    int open = 0;
    for (int i = 0; i < g_missing_n; i++) if (find_check(g_missing[i]) < 0) open++;
    if (open) {
        int k = (int)(GetTickCount() % (DWORD)open);
        for (int i = 0; i < g_missing_n; i++) if (find_check(g_missing[i]) < 0 && k-- == 0) { pick = g_missing[i]; break; }
    }
    unlock();
    if (!c || !pick) return;
    char buf[160];
    snprintf(buf, sizeof buf, "[{\"cmd\":\"LocationScouts\",\"locations\":[%lld],\"create_as_hint\":0}]", (long long)pick);
    ws_send_text(c, buf);
}

static void on_location_info(J *p) {                 /* (under g_lock) */
    for (J *it = j_get(p, "locations") ? j_get(p, "locations")->kid : NULL; it; it = it->next) {
        int64_t loc = (int64_t)j_num(j_get(it, "location"), 0), id = (int64_t)j_num(j_get(it, "item"), 0);
        int to = (int)j_num(j_get(it, "player"), -1);
        const char *where = "?";
        for (int i = 0; i < g_locs_n; i++) if (g_locs[i].id == loc) where = g_locs[i].name;
        char buf[32];
        const char *name = item_name(to >= 0 && to < MAX_SLOTS ? g_game[to] : NULL, id, buf, sizeof buf);
        ChatBuild b = {{0}, {0}, 0};                      /* "Hint: <location> has <item> (for <player>)" */
        chat_put(&b, "Hint: ", CHAT_TEXT); chat_put(&b, where, CHAT_LOCATION); chat_put(&b, " has ", CHAT_TEXT);
        chat_put(&b, name, item_color((int)j_num(j_get(it, "flags"), 0)));
        if (to != g_slot) { chat_put(&b, " for ", CHAT_TEXT); chat_put(&b, player_name(to), CHAT_PLAYER); }
        ap_chat_add(b.text, b.color);
    }
}

/* ---- Death / Damage / Trap / Ring Link (Bounce with tags) */
#define LINKS 32
static ApLink g_links[LINKS];
static int g_link_head, g_link_n;
static char g_link_tags[96];                         /* the tags this slot joined (ConnectUpdate) */

static void link_tags(Conn *c) {                     /* after Connected: the links slot_data asks for */
    char buf[256];
    JBuf b = {buf, sizeof buf, 0};
    int any = 0;
    g_link_tags[0] = 0;
    static const char *const names[][2] = {{"death_link", "DeathLink"}, {"damage_link", "DamageLink"},
                                           {"trap_link", "TrapLink"}, {"ring_link", "RingLink"}};
    jb_puts(&b, "[{\"cmd\":\"ConnectUpdate\",\"tags\":[");
    for (int i = 0; i < 4; i++) {
        char want[40];
        snprintf(want, sizeof want, ";%s=1;", names[i][0]);
        if (!strstr(g_slot_data, want)) continue;
        if (any) jb_puts(&b, ",");
        jb_str(&b, names[i][1]);
        any = 1;
        strcat(g_link_tags, names[i][1]);
        strcat(g_link_tags, ";");
    }
    jb_puts(&b, "]}]");
    if (any) ws_send_text(c, buf);
}

static void on_bounced(J *p) {                       /* (under g_lock) */
    J *tags = j_get(p, "tags"), *d = j_get(p, "data");
    for (J *t = tags ? tags->kid : NULL; t; t = t->next) {
        const char *tag = j_str(t, "");
        char want[24];
        snprintf(want, sizeof want, "%s;", tag);
        if (!*tag || !strstr(g_link_tags, want)) continue;
        if (!strcmp(j_str(j_get(d, "source"), ""), g_slot_name)) continue;        /* our own */
        if (g_link_n == LINKS) continue;
        ApLink *l = &g_links[(g_link_head + g_link_n++) % LINKS];
        memset(l, 0, sizeof *l);
        l->kind = !strcmp(tag, "DeathLink") ? AP_LINK_DEATH : !strcmp(tag, "DamageLink") ? AP_LINK_DAMAGE
                : !strcmp(tag, "TrapLink") ? AP_LINK_TRAP : !strcmp(tag, "RingLink") ? AP_LINK_RING : -1;
        l->amount = (int)j_num(j_get(d, "amount"), 0);
        snprintf(l->text, sizeof l->text, "%s", j_str(j_get(d, "trap_name"), j_str(j_get(d, "source"), "")));
        if (l->kind < 0) g_link_n--;
        break;
    }
}

/* The next link event from the others (1), or 0. */
int ap_net_link(ApLink *out) {
    lock();
    int got = g_link_n > 0;
    if (got) { *out = g_links[g_link_head]; g_link_head = (g_link_head + 1) % LINKS; g_link_n--; }
    unlock();
    return got;
}

/* Send a link event to the others (when this slot joined that link). */
void ap_net_link_send(int kind, int amount, const char *text) {
    static const char *const tag[] = {"DeathLink", "DamageLink", "TrapLink", "RingLink"};
    if (kind < 0 || kind > 3) return;
    lock();
    Conn *c = g_conn;
    char want[24];
    snprintf(want, sizeof want, "%s;", tag[kind]);
    int on = strstr(g_link_tags, want) != NULL;
    char me[96];
    snprintf(me, sizeof me, "%s", g_slot_name);
    int slot = g_slot;
    unlock();
    if (!c || !on) return;
    char buf[512];
    JBuf b = {buf, sizeof buf, 0};
    char t[64];
    jb_puts(&b, "[{\"cmd\":\"Bounce\",\"tags\":[");
    jb_str(&b, tag[kind]);
    snprintf(t, sizeof t, "],\"data\":{\"time\":%lld,\"source\":", (long long)time(NULL));
    jb_puts(&b, t);
    jb_str(&b, me);
    if (kind == AP_LINK_DEATH) { jb_puts(&b, ",\"cause\":"); jb_str(&b, text && *text ? text : "Bayonetta fell."); }
    if (kind == AP_LINK_TRAP) { jb_puts(&b, ",\"trap_name\":"); jb_str(&b, text ? text : ""); }
    if (kind == AP_LINK_RING || kind == AP_LINK_DAMAGE) {
        snprintf(t, sizeof t, ",\"amount\":%d", amount);
        jb_puts(&b, t);
    }
    snprintf(t, sizeof t, ",\"slot\":%d}}]", slot);
    jb_puts(&b, t);
    ws_send_text(c, buf);
}

/* The next popup to show (1), or 0 when there is none. */
int ap_net_popup(ApPopup *out) {
    lock();
    int got = g_pop_n > 0;
    if (got) { *out = g_popups[g_pop_head]; g_pop_head = (g_pop_head + 1) % POPUPS; g_pop_n--; }
    unlock();
    return got;
}

/* "<seed>_<slot>" of the logged-in session, or "" (keys the client's progress file). */
void ap_net_session(char *out, size_t n) {
    lock();
    snprintf(out, n, "%s", g_conn && g_seed[0] ? g_seed : "");
    if (out[0]) { size_t l = strlen(out); snprintf(out + l, n - l, "_%d", g_slot); }
    unlock();
}

static int session(Attempt *a, int tls) {           /* 0 = ended after a connection, 1 = could not open (try the other scheme) */
    Conn c = {INVALID_SOCKET, NULL, a->gen};
    c.s = tcp_connect(a->host, a->port);
    if (c.s == INVALID_SOCKET) return 1;
    lock(); g_sock = c.s; unlock();
    if (tls && (c.tls = ap_tls_open((uintptr_t)c.s, a->host))) { printf("ap: secure connection (TLS) with %s\n", a->host); fflush(stdout); }
    if ((tls && !c.tls) || ws_handshake(&c, a->host, a->port)) {
        if (c.tls) ap_tls_close(c.tls);
        lock(); if (g_sock == c.s) g_sock = INVALID_SOCKET; unlock();
        closesocket(c.s);
        return 1;
    }
    set_status(a->gen, "Connected to %s, logging in...", a->address);
    int ok = 0;
    char *m;
    while ((m = ws_recv_text(&c))) {
        J *root = j_parse(m);
        free(m);
        for (J *p = root ? root->kid : NULL; p; p = p->next) {
            const char *cmd = j_str(j_get(p, "cmd"), "");
            if (!strcmp(cmd, "RoomInfo")) {
                char id[40], buf[1024];
                JBuf b = {buf, sizeof buf, 0};
                uuid(id);
                jb_puts(&b, "[{\"cmd\":\"Connect\",\"game\":"); jb_str(&b, GAME_NAME);
                jb_puts(&b, ",\"name\":"); jb_str(&b, a->slot);
                jb_puts(&b, ",\"password\":"); jb_str(&b, a->password);
                jb_puts(&b, ",\"uuid\":"); jb_str(&b, id);
                jb_puts(&b, ",\"version\":{\"major\":0,\"minor\":6,\"build\":8,\"class\":\"Version\"},"
                            "\"items_handling\":7,\"tags\":[],\"slot_data\":true}]");
                if (ws_send_text(&c, buf)) break;
                snprintf(g_seed, sizeof g_seed, "%s", j_str(j_get(p, "seed_name"), ""));
            } else if (!strcmp(cmd, "Connected")) {
                ok = 1;
                lock();
                g_slot = (int)j_num(j_get(p, "slot"), 0);
                g_conn = &c;
                snprintf(g_slot_name, sizeof g_slot_name, "%s", a->slot);
                for (J *k = j_get(p, "checked_locations") ? j_get(p, "checked_locations")->kid : NULL; k; k = k->next)
                    mark_sent((int64_t)j_num(k, 0));
                g_missing_n = 0;
                for (J *k = j_get(p, "missing_locations") ? j_get(p, "missing_locations")->kid : NULL; k; k = k->next) {
                    int64_t *n = realloc(g_missing, (g_missing_n + 1) * sizeof *n);
                    if (!n) break;
                    g_missing = n;
                    g_missing[g_missing_n++] = (int64_t)j_num(k, 0);
                }
                g_items_n = 0;                            /* ReceivedItems follows with the whole list */
                unlock();
                set_status(a->gen, "Connected as %s. Choose STORY to play.", a->slot);
                lock(); on_connected(p, &c); unlock();    /* names, then the item names (DataPackage) */
                lock(); link_tags(&c); unlock();          /* Death / Damage / Trap / Ring Link (slot_data) */
                flush_checks(&c);                         /* checks made while offline */
            } else if (!strcmp(cmd, "DataPackage")) {
                lock(); on_data_package(p); unlock();
            } else if (!strcmp(cmd, "PrintJSON")) {
                lock(); on_print(p); unlock();
            } else if (!strcmp(cmd, "Bounced")) {
                lock(); on_bounced(p); unlock();
            } else if (!strcmp(cmd, "LocationInfo")) {
                lock(); on_location_info(p); unlock();
            } else if (!strcmp(cmd, "ReceivedItems")) {
                lock();
                int index = (int)j_num(j_get(p, "index"), 0);
                if (index == 0) g_items_n = 0;
                if (index == g_items_n)                   /* else a gap: the server resends everything on Sync */
                    for (J *k = j_get(p, "items") ? j_get(p, "items")->kid : NULL; k; k = k->next) add_item(k);
                else { unlock(); ws_send_text(&c, "[{\"cmd\":\"Sync\"}]"); lock(); }
                unlock();
            } else if (!strcmp(cmd, "ConnectionRefused")) {
                J *e = j_get(p, "errors");
                set_status(a->gen, "Refused: %s.", refusal(e && e->kid ? j_str(e->kid, "?") : "?"));
            }
        }
        j_free(root);
        if (a->gen != g_generation) break;
    }
    lock(); if (g_conn == &c) g_conn = NULL; unlock();
    if (c.tls) ap_tls_close(c.tls);
    lock(); if (g_sock == c.s) g_sock = INVALID_SOCKET; unlock();
    closesocket(c.s);
    if (a->gen == g_generation)
        set_status(a->gen, ok ? "Disconnected from %s. Press A to connect again." : "%s closed the connection.", a->address);
    return 0;
}

static DWORD WINAPI net_thread(void *p) {
    Attempt *a = p;
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    set_status(a->gen, "Connecting to %s...", a->address);
    int fail = 1;
    if (a->scheme != 1) fail = session(a, 1);        /* wss first, as Archipelago's clients do */
    if (fail && a->scheme != 2) fail = session(a, 0);
    if (fail) set_status(a->gen, "Could not reach %s.", a->address);
    free(a);
    return 0;
}

void ap_net_connect(const char *address, const char *slot, const char *password) {
    LONG gen = InterlockedIncrement(&g_generation);
    if (!*slot) { set_status(gen, "Set a slot name first."); return; }
    Attempt *a = calloc(1, sizeof *a);
    if (!a) return;
    a->gen = gen;
    snprintf(a->address, sizeof a->address, "%s", address);
    snprintf(a->slot, sizeof a->slot, "%s", slot);
    snprintf(a->password, sizeof a->password, "%s", password);
    const char *h = address;
    if (!strncmp(h, "wss://", 6)) { a->scheme = 2; h += 6; }
    else if (!strncmp(h, "ws://", 5)) { a->scheme = 1; h += 5; }
    snprintf(a->host, sizeof a->host, "%s", h);
    char *slash = strchr(a->host, '/'); if (slash) *slash = 0;
    char *colon = strrchr(a->host, ':');
    if (colon && !strchr(colon, ']')) { *colon = 0; snprintf(a->port, sizeof a->port, "%s", colon + 1); }
    else strcpy(a->port, DEFAULT_PORT);
    lock(); if (g_sock != INVALID_SOCKET) shutdown(g_sock, SD_BOTH); unlock();   /* the old thread's recv returns; it sees the new generation */
    if (g_thread) { CloseHandle(g_thread); g_thread = NULL; }
    g_thread = CreateThread(NULL, 0, net_thread, a, 0, NULL);
}
