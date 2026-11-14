/* [Archipelago] A small JSON reader (a tree of J nodes) and writer (into a fixed buffer), for the AP protocol. */
#ifndef AP_JSON_H
#define AP_JSON_H
#include <stddef.h>

enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };
typedef struct J {
    int type;
    double num;                      /* J_NUM, J_BOOL (0 / 1) */
    char *str;                       /* J_STR (UTF-8, unescaped) */
    char *key;                       /* the member's name, inside a J_OBJ */
    struct J *kid, *next;            /* J_ARR / J_OBJ: first child; siblings */
} J;

J *j_parse(const char *text);        /* NULL = not JSON */
void j_free(J *j);
J *j_get(const J *obj, const char *key);
const char *j_str(const J *j, const char *def);
double j_num(const J *j, double def);

typedef struct { char *p; size_t cap, len; } JBuf;   /* writing stops (len stays) when cap is reached */
void jb_puts(JBuf *b, const char *s);
void jb_str(JBuf *b, const char *s);                 /* a JSON string, quoted and escaped */
#endif
