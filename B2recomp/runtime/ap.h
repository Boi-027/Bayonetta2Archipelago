/* [Archipelago] what the AP parts of the runtime share (ap_ui.c, ap_net.c, the hooks in nnhid.c and display.c). */
#ifndef AP_H
#define AP_H
#include <stdint.h>
#include <stddef.h>

/* ap_ui.c */
int ap_kb_captured(void);                 /* a text field is being edited: the keyboard types instead of pressing buttons */
int ap_kb_char(unsigned ch);              /* WM_CHAR; 1 = taken */

/* nnhid.c: player 1's buttons as the game last saw them (pad, keyboard, test scripts; Pro Controller bits) */
uint64_t ap_hid_buttons(void);

/* ap_net.c */
typedef struct { int64_t item, location; int player, flags; } ApItem;
void ap_net_check(int64_t location);      /* a location found (any thread) */
void ap_net_goal(void);                   /* the goal is done */
int ap_net_items(int from, ApItem *out, int max, int *total);   /* received items from index `from` */
enum { AP_POP_RECEIVED, AP_POP_SENT, AP_POP_TRAP, AP_POP_HINT };
typedef struct { int kind; int64_t item; int ours; char text[64]; } ApPopup;   /* ours: a Bayonetta 2 item */
int ap_net_popup(ApPopup *out);
void ap_net_hint(void);
enum { AP_LINK_DEATH, AP_LINK_DAMAGE, AP_LINK_TRAP, AP_LINK_RING };
typedef struct { int kind, amount; char text[64]; } ApLink;
int ap_net_link(ApLink *out);                       /* the next Death / Damage / Trap / Ring Link event (1), 0 = none */
void ap_net_link_send(int kind, int amount, const char *text);                             /* scout a location of this world for a hint popup */                     /* the next item popup (1), 0 = none */
enum { AP_CHEAT_GOD, AP_CHEAT_HALOS, AP_CHEAT_MAGIC, AP_CHEAT_DMG_TAKEN, AP_CHEAT_HALO_GAIN, AP_CHEAT_EASY_SKIP, AP_CHEAT_AUTO_QTE, AP_CHEAT_COUNT };
int ap_cheat(int which);                            /* runtime/ap_opt.c: a cheat of the Options' Cheats pages is on */
float ap_cheat_value(int which);                    /* runtime/ap_opt.c: a slider cheat's value */
int ap_net_checked(int64_t location);                 /* found here or already checked on the server */
int ap_net_slot_int(const char *key, int def);       /* a number of the seed's slot_data (0 / 1 for booleans) */
void ap_net_session(char *out, size_t n); /* "<seed>_<slot>" when logged in, else "" */
void ap_net_connect(const char *address, const char *slot, const char *password);
void ap_net_status(char *out, size_t n);  /* one line for the connect menu */
int ap_net_status_changed(void);          /* since the last call */

/* ap_inflate.c: raw DEFLATE -> malloc'd bytes plus a 0 (NULL = broken data) */
uint8_t *ap_inflate(const uint8_t *in, size_t n, size_t *out_len);

/* ap_tls.c (SChannel, TLS 1.2) */
void *ap_tls_open(uintptr_t sock, const char *host);   /* NULL = no TLS session (not TLS, untrusted certificate, ...) */
int ap_tls_send(void *t, const void *data, int n);     /* 0 = sent */
int ap_tls_recv(void *t, void *out, int n);            /* bytes (> 0); <= 0 = closed */
void ap_tls_close(void *t);
#endif
