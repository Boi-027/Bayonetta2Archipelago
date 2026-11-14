/* [Archipelago] Raw DEFLATE (RFC 1951) decoding, for WebSocket permessage-deflate (RFC 7692): the server compresses
 * its messages, each one on its own (server_no_context_takeover). A message ends with an empty stored block that is
 * not final (the sync flush); decoding stops at the end of the input. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ap.h"

typedef struct { uint16_t count[16], sym[288]; } Huff;
typedef struct {
    const uint8_t *in; size_t n, pos;
    uint32_t bits; int nbits;
    uint8_t *out; size_t len, cap;
    int bad;
} St;

static int bit(St *s) {
    if (!s->nbits) {
        if (s->pos >= s->n) { s->bad = 1; return 0; }
        s->bits = s->in[s->pos++]; s->nbits = 8;
    }
    int b = s->bits & 1;
    s->bits >>= 1; s->nbits--;
    return b;
}
static uint32_t bits(St *s, int n) { uint32_t v = 0; for (int i = 0; i < n; i++) v |= (uint32_t)bit(s) << i; return v; }

static void build(Huff *h, const uint8_t *len, int n) {
    uint16_t off[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    off[1] = 0;
    for (int i = 1; i < 15; i++) off[i + 1] = off[i] + h->count[i];
    for (int i = 0; i < n; i++) if (len[i]) h->sym[off[len[i]]++] = (uint16_t)i;
}
static int decode(St *s, const Huff *h) {          /* canonical codes, one bit at a time */
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        code |= bit(s);
        int c = h->count[l];
        if (code - first < c) return h->sym[index + code - first];
        index += c; first += c;
        first <<= 1; code <<= 1;
        if (s->bad) break;
    }
    s->bad = 1;
    return 0;
}

static int put(St *s, uint8_t b) {
    if (s->len == s->cap) {
        size_t c = s->cap ? s->cap * 2 : 4096;
        uint8_t *o = realloc(s->out, c);
        if (!o) return -1;
        s->out = o; s->cap = c;
    }
    s->out[s->len++] = b;
    return 0;
}

static const uint16_t LBASE[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const uint8_t LEXT[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const uint16_t DBASE[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const uint8_t DEXT[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int codes(St *s, const Huff *lit, const Huff *dist) {
    for (;;) {
        int sym = decode(s, lit);
        if (s->bad) return -1;
        if (sym < 256) { if (put(s, (uint8_t)sym)) return -1; continue; }
        if (sym == 256) return 0;
        sym -= 257;
        if (sym >= 29) return -1;
        size_t len = LBASE[sym] + bits(s, LEXT[sym]);
        int d = decode(s, dist);
        if (s->bad || d >= 30) return -1;
        size_t back = DBASE[d] + bits(s, DEXT[d]);
        if (back > s->len) return -1;
        while (len--) if (put(s, s->out[s->len - back])) return -1;
    }
}

static int dynamic(St *s) {
    static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    uint8_t len[320] = {0};
    int nlen = (int)bits(s, 5) + 257, ndist = (int)bits(s, 5) + 1, ncode = (int)bits(s, 4) + 4;
    if (nlen > 286 || ndist > 30) return -1;
    for (int i = 0; i < ncode; i++) len[order[i]] = (uint8_t)bits(s, 3);
    Huff lc, lit, dist;
    build(&lc, len, 19);
    memset(len, 0, sizeof len);
    for (int i = 0; i < nlen + ndist;) {
        int sym = decode(s, &lc);
        if (s->bad) return -1;
        if (sym < 16) { len[i++] = (uint8_t)sym; continue; }
        int rep, val = 0;
        if (sym == 16) { if (!i) return -1; val = len[i - 1]; rep = 3 + (int)bits(s, 2); }
        else if (sym == 17) rep = 3 + (int)bits(s, 3);
        else rep = 11 + (int)bits(s, 7);
        if (i + rep > nlen + ndist) return -1;
        while (rep--) len[i++] = (uint8_t)val;
    }
    build(&lit, len, nlen);
    build(&dist, len + nlen, ndist);
    return codes(s, &lit, &dist);
}

static int fixed(St *s) {
    uint8_t len[320];
    int i = 0;
    for (; i < 144; i++) len[i] = 8;
    for (; i < 256; i++) len[i] = 9;
    for (; i < 280; i++) len[i] = 7;
    for (; i < 288; i++) len[i] = 8;
    for (; i < 288 + 30; i++) len[i] = 5;
    Huff lit, dist;
    build(&lit, len, 288);
    build(&dist, len + 288, 30);
    return codes(s, &lit, &dist);
}

/* raw DEFLATE -> malloc'd bytes plus a 0 terminator (*out_len without it); NULL = broken data */
uint8_t *ap_inflate(const uint8_t *in, size_t n, size_t *out_len) {
    St s = {in, n, 0, 0, 0, NULL, 0, 0, 0};
    int last = 0;
    while (!last && s.pos < s.n) {
        last = bit(&s);
        int type = (int)bits(&s, 2), r;
        if (type == 0) {                             /* stored: byte aligned LEN, NLEN, data */
            s.nbits = 0;
            if (s.pos + 4 > s.n) { r = -1; }
            else {
                unsigned l = s.in[s.pos] | s.in[s.pos + 1] << 8, nl = s.in[s.pos + 2] | s.in[s.pos + 3] << 8;
                s.pos += 4;
                r = (l ^ 0xffff) != nl || s.pos + l > s.n ? -1 : 0;
                for (unsigned i = 0; !r && i < l; i++) r = put(&s, s.in[s.pos + i]);
                s.pos += l;
            }
        } else if (type == 1) r = fixed(&s);
        else if (type == 2) r = dynamic(&s);
        else r = -1;
        if (r || s.bad) { free(s.out); return NULL; }
    }
    if (put(&s, 0)) { free(s.out); return NULL; }
    *out_len = s.len - 1;
    return s.out;
}
