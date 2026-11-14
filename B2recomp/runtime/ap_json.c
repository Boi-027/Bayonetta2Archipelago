/* [Archipelago] see ap_json.h */
#include <stdlib.h>
#include <string.h>
#include "ap_json.h"

typedef struct { const char *s; int depth; } P;

static void ws(P *p) { while (*p->s == ' ' || *p->s == '\t' || *p->s == '\n' || *p->s == '\r') p->s++; }

static J *node(int type) { J *j = calloc(1, sizeof *j); if (j) j->type = type; return j; }

static int utf8(char *o, unsigned c) {
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xc0 | c >> 6); o[1] = (char)(0x80 | (c & 63)); return 2; }
    if (c < 0x10000) { o[0] = (char)(0xe0 | c >> 12); o[1] = (char)(0x80 | (c >> 6 & 63)); o[2] = (char)(0x80 | (c & 63)); return 3; }
    o[0] = (char)(0xf0 | c >> 18); o[1] = (char)(0x80 | (c >> 12 & 63)); o[2] = (char)(0x80 | (c >> 6 & 63)); o[3] = (char)(0x80 | (c & 63));
    return 4;
}

static unsigned hex4(const char *s) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        v = v << 4 | (unsigned)(c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
    }
    return v;
}

static char *string(P *p) {                          /* at the opening quote */
    const char *s = ++p->s;
    size_t n = 0;
    while (s[n] && s[n] != '"') n += s[n] == '\\' && s[n + 1] ? 2 : 1;
    if (s[n] != '"') return NULL;
    char *o = malloc(n + 1), *w = o;                 /* escapes only shrink */
    if (!o) return NULL;
    while (*p->s && *p->s != '"') {
        char c = *p->s++;
        if (c != '\\') { *w++ = c; continue; }
        c = *p->s++;
        switch (c) {
        case 'n': *w++ = '\n'; break; case 't': *w++ = '\t'; break; case 'r': *w++ = '\r'; break;
        case 'b': *w++ = '\b'; break; case 'f': *w++ = '\f'; break;
        case 'u': {
            unsigned u = hex4(p->s); p->s += 4;
            if (u >= 0xd800 && u < 0xdc00 && p->s[0] == '\\' && p->s[1] == 'u') {     /* a surrogate pair */
                unsigned lo = hex4(p->s + 2); p->s += 6;
                u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
            }
            w += utf8(w, u);
            break;
        }
        default: *w++ = c;
        }
    }
    *w = 0;
    if (*p->s == '"') p->s++;
    return o;
}

static J *value(P *p) {
    ws(p);
    if (++p->depth > 64) return NULL;
    J *j = NULL;
    char c = *p->s;
    if (c == '{' || c == '[') {
        j = node(c == '{' ? J_OBJ : J_ARR);
        p->s++;
        J **tail = &j->kid;
        ws(p);
        if (*p->s == (c == '{' ? '}' : ']')) { p->s++; p->depth--; return j; }
        for (;;) {
            char *key = NULL;
            if (c == '{') {
                ws(p);
                if (*p->s != '"' || !(key = string(p))) break;
                ws(p);
                if (*p->s++ != ':') { free(key); break; }
            }
            J *k = value(p);
            if (!k) { free(key); break; }
            k->key = key;
            *tail = k; tail = &k->next;
            ws(p);
            if (*p->s == ',') { p->s++; continue; }
            if (*p->s == (c == '{' ? '}' : ']')) { p->s++; p->depth--; return j; }
            break;
        }
        j_free(j);
        return NULL;
    }
    if (c == '"') { j = node(J_STR); if (j && !(j->str = string(p))) { free(j); j = NULL; } }
    else if (!strncmp(p->s, "true", 4)) { j = node(J_BOOL); j->num = 1; p->s += 4; }
    else if (!strncmp(p->s, "false", 5)) { j = node(J_BOOL); p->s += 5; }
    else if (!strncmp(p->s, "null", 4)) { j = node(J_NULL); p->s += 4; }
    else if (c == '-' || (c >= '0' && c <= '9')) { char *e; j = node(J_NUM); j->num = strtod(p->s, &e); p->s = e; }
    p->depth--;
    return j;
}

J *j_parse(const char *text) {
    P p = {text, 0};
    return value(&p);
}

void j_free(J *j) {
    while (j) {
        J *n = j->next;
        j_free(j->kid);
        free(j->str); free(j->key); free(j);
        j = n;
    }
}

J *j_get(const J *o, const char *key) {
    for (J *k = o && o->type == J_OBJ ? o->kid : NULL; k; k = k->next) if (k->key && !strcmp(k->key, key)) return k;
    return NULL;
}
const char *j_str(const J *j, const char *def) { return j && j->type == J_STR ? j->str : def; }
double j_num(const J *j, double def) { return j && (j->type == J_NUM || j->type == J_BOOL) ? j->num : def; }

void jb_puts(JBuf *b, const char *s) {
    size_t n = strlen(s);
    if (b->len + n + 1 > b->cap) { b->len = b->cap; return; }
    memcpy(b->p + b->len, s, n + 1);
    b->len += n;
}
void jb_str(JBuf *b, const char *s) {
    char t[8];
    jb_puts(b, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { t[0] = '\\'; t[1] = (char)c; t[2] = 0; }
        else if (c < 0x20) { static const char h[] = "0123456789abcdef"; memcpy(t, "\\u00", 4); t[4] = h[c >> 4]; t[5] = h[c & 15]; t[6] = 0; }
        else { t[0] = (char)c; t[1] = 0; }
        jb_puts(b, t);
    }
    jb_puts(b, "\"");
}
