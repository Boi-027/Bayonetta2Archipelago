/* [Co-op] Story co-op (runtime/coop_net.c: the connection; ap_ui.c: the CO-OP menu). */
#pragma once
#include <stddef.h>

#define COOP_PORT 27870                                /* UDP; COOP_PORT + 1: the local-network lobby beacon */
#define COOP_MSG_MAX 1024
enum { COOP_OFF, COOP_HOSTING, COOP_JOINING, COOP_CONNECTED };

void coop_host(const char *port_addr, const char *name, const char *password, int public_lobby, int skin);
void coop_join(const char *addr, const char *name, const char *password, int skin);
void coop_leave(void);
int coop_role(void);
int coop_is_host(void);
void coop_status(char *buf, size_t n);
int coop_status_changed(void);
void coop_partner(char *name, size_t n, int *skin, unsigned *rtt_ms);
void coop_local_ips(char *buf, size_t n);
int coop_send(const void *data, int n);
int coop_take(void *data, int cap);

/* the characters / outfits offered by the CHARACTER entry (ap_ui.c) */
extern const char *const k_coop_skins[];
extern const int k_coop_skin_count;
