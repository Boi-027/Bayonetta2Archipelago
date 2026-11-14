/* [Co-op] Story co-op's connection: direct IP over UDP, two players, the host's game in charge.
 *
 * HOST opens UDP port COOP_PORT (or the port of the ADDRESS field) and waits; CONNECT sends HELLO to the address (an
 * empty address: the first public lobby heard on this network). A public lobby says so on the local network (BEACON, a
 * broadcast to COOP_PORT + 1 every 2 s) and lets anyone in; a private one does not broadcast and wants the password.
 * Packets: "B2CO", version, type, then the type's fields. HELLO is sent again until WELCOME or DENY comes; both sides
 * PING every second and drop the other after COOP_TIMEOUT_MS of silence. Events go to the chat box (ap_chat.c).
 * The game-state messages of later milestones ride the same socket (coop_send / coop_take). */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#ifdef _MSC_VER
#pragma comment(lib, "iphlpapi.lib")
#endif
#include "coop.h"
#include "ap_chat.h"

#define COOP_VERSION 1
#define COOP_TIMEOUT_MS 8000
enum { P_HELLO = 1, P_WELCOME, P_DENY, P_PING, P_PONG, P_BYE, P_BEACON, P_GAME };

#pragma pack(push, 1)
typedef struct { char magic[4]; uint8_t version, type; } Head;
typedef struct { Head h; char name[32], password[32]; uint16_t skin; } Hello;
typedef struct { Head h; char name[32]; uint16_t skin; } Welcome;
typedef struct { Head h; char reason[64]; } Deny;
typedef struct { Head h; uint64_t t; } Ping;
typedef struct { Head h; char name[32]; uint16_t port; uint8_t players, priv; } Beacon;
#pragma pack(pop)

static CRITICAL_SECTION g_lock;
static volatile LONG g_init;
static SOCKET g_sock = INVALID_SOCKET, g_beacon_sock = INVALID_SOCKET;
static HANDLE g_thread;
static volatile LONG g_stop;
static int g_role;                                   /* COOP_OFF, COOP_HOSTING, COOP_JOINING, COOP_CONNECTED */
static int g_is_host;
static struct sockaddr_in g_peer;
static int g_have_peer;
static char g_my_name[32], g_password[32], g_peer_name[32], g_status[128];
static int g_public, g_my_skin, g_peer_skin;
static ULONGLONG g_last_heard, g_rtt;
static volatile LONG g_status_changed = 1;
static uint16_t g_port = COOP_PORT;
static char g_join_addr[96];

/* incoming game messages for the game thread (coop_take) */
#define INBOX 64
static struct { int n; uint8_t data[COOP_MSG_MAX]; } g_inbox[INBOX];
static int g_in_head, g_in_n;

static void lock(void) {
    if (InterlockedCompareExchange(&g_init, 1, 0) == 0) { InitializeCriticalSection(&g_lock); InterlockedExchange(&g_init, 2); }
    while (g_init != 2) Sleep(0);
    EnterCriticalSection(&g_lock);
}
static void unlock(void) { LeaveCriticalSection(&g_lock); }

static void say(const char *fmt, ...) {              /* a chat-box line (white "Co-op:" then the text) */
    char t[200]; uint8_t c[200];
    va_list v; va_start(v, fmt);
    int n = snprintf(t, sizeof t, "Co-op: ");
    vsnprintf(t + n, sizeof t - n, fmt, v);
    va_end(v);
    for (int i = 0; t[i]; i++) c[i] = i < 6 ? CHAT_ME : CHAT_TEXT;
    ap_chat_add(t, c);
}
static void set_status(const char *fmt, ...) {
    va_list v; va_start(v, fmt);
    vsnprintf(g_status, sizeof g_status, fmt, v);
    va_end(v);
    InterlockedExchange(&g_status_changed, 1);
}
static void head(Head *h, int type) { memcpy(h->magic, "B2CO", 4); h->version = COOP_VERSION; h->type = (uint8_t)type; }
static void send_to(const void *p, int n, const struct sockaddr_in *to) {
    if (g_sock != INVALID_SOCKET) sendto(g_sock, (const char *)p, n, 0, (const struct sockaddr *)to, sizeof *to);
}

static int resolve(const char *text, struct sockaddr_in *out) {   /* "host[:port]" */
    char host[96]; int port = COOP_PORT;
    snprintf(host, sizeof host, "%s", text);
    char *colon = strrchr(host, ':');
    if (colon) { port = atoi(colon + 1); *colon = 0; }
    struct addrinfo hints, *r = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &r) || !r) return 0;
    *out = *(struct sockaddr_in *)r->ai_addr;
    out->sin_port = htons((u_short)(port > 0 && port < 65536 ? port : COOP_PORT));
    freeaddrinfo(r);
    return 1;
}

static void drop(const char *why) {                  /* (under g_lock) back to waiting (host) or off (joiner) */
    if (g_role == COOP_CONNECTED) say("%s %s.", g_peer_name[0] ? g_peer_name : "Your partner", why);
    g_have_peer = 0; g_peer_name[0] = 0; g_in_n = 0;
    if (g_is_host) { g_role = COOP_HOSTING; set_status("Hosting on port %u: waiting for a partner.", g_port); }
    else { g_role = COOP_OFF; set_status("Not connected (%s).", why); }
}

static void on_packet(const uint8_t *buf, int n, const struct sockaddr_in *from) {   /* (under g_lock) */
    if (n < (int)sizeof(Head) || memcmp(buf, "B2CO", 4)) return;
    const Head *h = (const Head *)buf;
    if (h->version != COOP_VERSION) {
        if (h->type == P_HELLO && g_is_host) { Deny d; head(&d.h, P_DENY); snprintf(d.reason, sizeof d.reason, "a different co-op version"); send_to(&d, sizeof d, from); }
        return;
    }
    int same = g_have_peer && from->sin_addr.s_addr == g_peer.sin_addr.s_addr && from->sin_port == g_peer.sin_port;
    switch (h->type) {
    case P_HELLO: {
        if (!g_is_host || n < (int)sizeof(Hello)) return;
        const Hello *p = (const Hello *)buf;
        Deny d; head(&d.h, P_DENY);
        if (g_have_peer && !same) { snprintf(d.reason, sizeof d.reason, "the lobby is full"); send_to(&d, sizeof d, from); return; }
        if ((!g_public || g_password[0]) && strncmp(p->password, g_password, sizeof g_password)) {
            snprintf(d.reason, sizeof d.reason, "wrong password"); send_to(&d, sizeof d, from); return;
        }
        int fresh = !same;
        g_peer = *from; g_have_peer = 1; g_last_heard = GetTickCount64();
        memcpy(g_peer_name, p->name, sizeof g_peer_name); g_peer_name[31] = 0;
        g_peer_skin = p->skin;
        Welcome w; head(&w.h, P_WELCOME);
        memcpy(w.name, g_my_name, sizeof w.name); w.skin = (uint16_t)g_my_skin;
        send_to(&w, sizeof w, from);
        if (fresh) { g_role = COOP_CONNECTED; set_status("%s joined your lobby.", g_peer_name); say("%s joined your lobby.", g_peer_name); }
        break;
    }
    case P_WELCOME:
        if (g_is_host || g_role != COOP_JOINING || n < (int)sizeof(Welcome)) return;
        g_peer = *from; g_have_peer = 1; g_last_heard = GetTickCount64();
        memcpy(g_peer_name, ((const Welcome *)buf)->name, sizeof g_peer_name); g_peer_name[31] = 0;
        g_peer_skin = ((const Welcome *)buf)->skin;
        g_role = COOP_CONNECTED;
        set_status("Connected to %s's lobby.", g_peer_name);
        say("connected to %s's lobby.", g_peer_name);
        break;
    case P_DENY:
        if (g_is_host || g_role != COOP_JOINING || n < (int)sizeof(Deny)) return;
        g_role = COOP_OFF;
        set_status("Refused: %.60s.", ((const Deny *)buf)->reason);
        say("the host refused: %.60s.", ((const Deny *)buf)->reason);
        break;
    case P_PING:
        if (!same || n < (int)sizeof(Ping)) return;
        { Ping r = *(const Ping *)buf; head(&r.h, P_PONG); send_to(&r, sizeof r, from); }
        g_last_heard = GetTickCount64();
        break;
    case P_PONG:
        if (!same || n < (int)sizeof(Ping)) return;
        g_rtt = GetTickCount64() - ((const Ping *)buf)->t;
        g_last_heard = GetTickCount64();
        break;
    case P_BYE:
        if (same) drop("left");
        break;
    case P_GAME:
        if (!same || n <= (int)sizeof(Head) || n - (int)sizeof(Head) > COOP_MSG_MAX) return;
        g_last_heard = GetTickCount64();
        if (g_in_n == INBOX) { g_in_head = (g_in_head + 1) % INBOX; g_in_n--; }        /* full: the oldest goes */
        { int k = (g_in_head + g_in_n++) % INBOX; g_inbox[k].n = n - (int)sizeof(Head); memcpy(g_inbox[k].data, buf + sizeof(Head), (size_t)g_inbox[k].n); }
        break;
    }
}

/* A public lobby on this network, heard on COOP_PORT + 1 (the joiner with an empty ADDRESS). */
static int listen_beacon(struct sockaddr_in *found, char *name, size_t n, int ms) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return 0;
    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(COOP_PORT + 1); a.sin_addr.s_addr = INADDR_ANY;
    int ok = 0;
    if (!bind(s, (struct sockaddr *)&a, sizeof a)) {
        DWORD t = 200; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof t);
        ULONGLONG end = GetTickCount64() + (ULONGLONG)ms;
        while (!ok && GetTickCount64() < end && !g_stop) {
            Beacon b; struct sockaddr_in from; int fl = sizeof from;
            int r = recvfrom(s, (char *)&b, sizeof b, 0, (struct sockaddr *)&from, &fl);
            if (r == (int)sizeof b && !memcmp(b.h.magic, "B2CO", 4) && b.h.type == P_BEACON && b.h.version == COOP_VERSION && !b.priv && b.players < 2) {
                *found = from; found->sin_port = htons(b.port);
                snprintf(name, n, "%.31s", b.name);
                ok = 1;
            }
        }
    }
    closesocket(s);
    return ok;
}

static DWORD WINAPI net_thread(void *arg) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);   /* the game's busy threads (a stage loading) must not starve the pings */
    (void)arg;
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    lock();
    int host = g_is_host;
    char addr[96]; snprintf(addr, sizeof addr, "%s", g_join_addr);
    unlock();
    struct sockaddr_in target; memset(&target, 0, sizeof target);
    if (!host) {                                       /* where to: the address, or a public lobby on this network */
        int ok;
        char lobby[32] = "";
        if (addr[0]) ok = resolve(addr, &target);
        else { lock(); set_status("Looking for a public lobby on your network..."); unlock(); ok = listen_beacon(&target, lobby, sizeof lobby, 4000); }
        if (!ok) { lock(); g_role = COOP_OFF; set_status(addr[0] ? "Cannot find the address %.60s." : "No public lobby found on your network.", addr); unlock(); WSACleanup(); return 0; }
    }
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in me; memset(&me, 0, sizeof me);
    me.sin_family = AF_INET; me.sin_addr.s_addr = INADDR_ANY; me.sin_port = host ? htons(g_port) : 0;
    if (s == INVALID_SOCKET || bind(s, (struct sockaddr *)&me, sizeof me)) {
        lock(); g_role = COOP_OFF; set_status(host ? "Port %u is in use: is another copy hosting?" : "Cannot open a network socket.", g_port); unlock();
        if (s != INVALID_SOCKET) closesocket(s);
        WSACleanup();
        return 0;
    }
    DWORD tmo = 50; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof tmo);
    if (host) {                                        /* the beacon goes out from a socket of its own */
        g_beacon_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        BOOL yes = TRUE; setsockopt(g_beacon_sock, SOL_SOCKET, SO_BROADCAST, (const char *)&yes, sizeof yes);
    }
    lock();
    g_sock = s;
    if (host) set_status("Hosting on port %u: waiting for a partner.", g_port);
    else set_status("Connecting...");
    unlock();
    ULONGLONG next_ping = 0, next_hello = 0, next_beacon = 0, started = GetTickCount64();
    while (!g_stop) {
        uint8_t buf[1500]; struct sockaddr_in from; int fl = sizeof from;
        int n = recvfrom(s, (char *)buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
        lock();
        if (n > 0) on_packet(buf, n, &from);
        ULONGLONG now = GetTickCount64();                /* after on_packet: g_last_heard must not be later than now */
        if (!host && g_role == COOP_JOINING && now >= next_hello) {
            if (now - started > 10000) { g_role = COOP_OFF; set_status("No answer from the host (is the port open?)."); unlock(); break; }
            Hello h; memset(&h, 0, sizeof h); head(&h.h, P_HELLO);
            memcpy(h.name, g_my_name, sizeof h.name); memcpy(h.password, g_password, sizeof h.password); h.skin = (uint16_t)g_my_skin;
            sendto(s, (const char *)&h, sizeof h, 0, (struct sockaddr *)&target, sizeof target);
            next_hello = now + 500;
        }
        if (g_role == COOP_CONNECTED && now >= next_ping) {
            Ping p; head(&p.h, P_PING); p.t = now; send_to(&p, sizeof p, &g_peer);
            next_ping = now + 1000;
        }
        if (g_role == COOP_CONNECTED && now - g_last_heard > COOP_TIMEOUT_MS) { drop("lost the connection"); if (!host) { unlock(); break; } }
        if (host && g_public && now >= next_beacon && g_beacon_sock != INVALID_SOCKET) {
            Beacon b; memset(&b, 0, sizeof b); head(&b.h, P_BEACON);
            memcpy(b.name, g_my_name, sizeof b.name); b.port = g_port; b.players = (uint8_t)(g_have_peer ? 2 : 1); b.priv = (uint8_t)!g_public;
            struct sockaddr_in bc; memset(&bc, 0, sizeof bc);
            bc.sin_family = AF_INET; bc.sin_port = htons(COOP_PORT + 1); bc.sin_addr.s_addr = INADDR_BROADCAST;
            sendto(g_beacon_sock, (const char *)&b, sizeof b, 0, (struct sockaddr *)&bc, sizeof bc);
            next_beacon = now + 2000;
        }
        unlock();
    }
    lock();
    if (g_have_peer) { Head b; head(&b, P_BYE); send_to(&b, sizeof b, &g_peer); }
    g_sock = INVALID_SOCKET; g_have_peer = 0;
    if (g_beacon_sock != INVALID_SOCKET) { closesocket(g_beacon_sock); g_beacon_sock = INVALID_SOCKET; }
    unlock();
    closesocket(s);
    WSACleanup();
    return 0;
}

static void start(int host, const char *addr, const char *name, const char *password, int public_lobby, int skin) {
    coop_leave();
    lock();
    g_is_host = host;
    g_role = host ? COOP_HOSTING : COOP_JOINING;
    snprintf(g_my_name, sizeof g_my_name, "%s", name && *name ? name : "Bayonetta");
    memset(g_password, 0, sizeof g_password);
    snprintf(g_password, sizeof g_password, "%s", password ? password : "");
    g_public = public_lobby; g_my_skin = skin;
    snprintf(g_join_addr, sizeof g_join_addr, "%s", addr ? addr : "");
    g_port = COOP_PORT;
    if (host && addr && strrchr(addr, ':')) { int p = atoi(strrchr(addr, ':') + 1); if (p > 0 && p < 65536) g_port = (uint16_t)p; }
    g_peer_name[0] = 0; g_have_peer = 0; g_in_n = 0;
    set_status(host ? "Opening the lobby..." : "Connecting...");
    unlock();
    g_stop = 0;
    g_thread = CreateThread(NULL, 0, net_thread, NULL, 0, NULL);
}

void coop_host(const char *port_addr, const char *name, const char *password, int public_lobby, int skin) { start(1, port_addr, name, password, public_lobby, skin); }
void coop_join(const char *addr, const char *name, const char *password, int skin) { start(0, addr, name, password, 1, skin); }
void coop_leave(void) {
    if (!g_thread) return;
    InterlockedExchange(&g_stop, 1);
    WaitForSingleObject(g_thread, 3000);
    CloseHandle(g_thread);
    g_thread = NULL;
    lock(); g_role = COOP_OFF; set_status("Not connected."); unlock();
}
int coop_role(void) { lock(); int r = g_role; unlock(); return r; }
int coop_is_host(void) { return g_is_host; }
void coop_status(char *buf, size_t n) { lock(); snprintf(buf, n, "%s", g_status[0] ? g_status : "Not connected."); unlock(); }
int coop_status_changed(void) { return InterlockedExchange(&g_status_changed, 0) != 0; }
void coop_partner(char *name, size_t n, int *skin, unsigned *rtt_ms) {
    lock(); snprintf(name, n, "%s", g_peer_name); if (skin) *skin = g_peer_skin; if (rtt_ms) *rtt_ms = (unsigned)g_rtt; unlock();
}

/* this PC's IPv4 addresses on its networks, "a.b.c.d, e.f.g.h" (for the HOST line: what to give a friend on the LAN) */
void coop_local_ips(char *buf, size_t n) {
    buf[0] = 0;
    ULONG size = 15000;
    IP_ADAPTER_ADDRESSES *a = malloc(size);
    if (a && GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, a, &size) == NO_ERROR)
        for (IP_ADAPTER_ADDRESSES *p = a; p; p = p->Next) {
            if (p->OperStatus != IfOperStatusUp || p->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (IP_ADAPTER_UNICAST_ADDRESS *u = p->FirstUnicastAddress; u; u = u->Next) {
                char t[32];
                inet_ntop(AF_INET, &((struct sockaddr_in *)u->Address.lpSockaddr)->sin_addr, t, sizeof t);
                if (!strncmp(t, "169.254.", 8)) continue;
                size_t l = strlen(buf);
                snprintf(buf + l, n - l, "%s%s", l ? ", " : "", t);
            }
        }
    free(a);
}

/* game messages (later milestones): to the partner / from the partner (oldest first; 0 = none) */
int coop_send(const void *data, int n) {
    if (n <= 0 || n > COOP_MSG_MAX) return 0;
    uint8_t buf[sizeof(Head) + COOP_MSG_MAX];
    head((Head *)buf, P_GAME);
    memcpy(buf + sizeof(Head), data, (size_t)n);
    lock();
    int ok = g_role == COOP_CONNECTED && g_have_peer;
    if (ok) send_to(buf, (int)sizeof(Head) + n, &g_peer);
    unlock();
    return ok;
}
int coop_take(void *data, int cap) {
    lock();
    int n = 0;
    if (g_in_n) { n = g_inbox[g_in_head].n <= cap ? g_inbox[g_in_head].n : cap; memcpy(data, g_inbox[g_in_head].data, (size_t)n); g_in_head = (g_in_head + 1) % INBOX; g_in_n--; }
    unlock();
    return n;
}
