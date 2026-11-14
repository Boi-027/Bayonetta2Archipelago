/* Self-test of runtime/ap_json.c: build and run with
 *   clang -I B2recomp/runtime ap/tests/test_json.c B2recomp/runtime/ap_json.c -o test_json.exe && test_json.exe */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ap_json.h"

int main(void) {
    J *r = j_parse("[{\"cmd\":\"RoomInfo\",\"version\":{\"major\":0,\"minor\":6},\"tags\":[\"AP\"],\"x\":null,\"ok\":true},"
                   " {\"cmd\":\"ConnectionRefused\",\"errors\":[\"InvalidSlot\"]}, {\"cmd\":\"Print\",\"text\":\"a\\\"b\\u00e9\\ud83d\\ude00\\n\"}]");
    assert(r && r->type == J_ARR);
    J *a = r->kid, *b = a->next, *c = b->next;
    assert(!strcmp(j_str(j_get(a, "cmd"), ""), "RoomInfo"));
    assert(j_num(j_get(j_get(a, "version"), "minor"), -1) == 6);
    assert(!strcmp(j_str(j_get(a, "tags")->kid, ""), "AP"));
    assert(j_get(a, "x")->type == J_NULL && j_num(j_get(a, "ok"), 0) == 1);
    assert(!strcmp(j_str(j_get(b, "errors")->kid, ""), "InvalidSlot"));
    assert(!strcmp(j_str(j_get(c, "text"), ""), "a\"b\xc3\xa9\xf0\x9f\x98\x80\n"));    /* escapes, BMP and surrogate pair */
    assert(!j_get(a, "missing") && !strcmp(j_str(NULL, "def"), "def"));
    j_free(r);
    assert(!j_parse("[1,2") && !j_parse("{\"a\" 1}") && !j_parse(""));                     /* broken input */
    J *e = j_parse(" [ ] ");
    assert(e && e->type == J_ARR && !e->kid);
    j_free(e);
    char buf[64];
    JBuf w = {buf, sizeof buf, 0};
    jb_puts(&w, "{\"n\":"); jb_str(&w, "q\"\\\n"); jb_puts(&w, "}");
    assert(!strcmp(buf, "{\"n\":\"q\\\"\\\\\\u000a\"}"));
    char small[8];
    JBuf s = {small, sizeof small, 0};
    jb_puts(&s, "1234567890");                                                             /* too long: nothing written */
    assert(s.len == s.cap);
    printf("ap_json: all checks passed\n");
    return 0;
}
