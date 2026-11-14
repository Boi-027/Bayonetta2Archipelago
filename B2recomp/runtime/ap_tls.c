/* [Archipelago] TLS for wss:// connections, with Windows' own SChannel (secur32.dll, loaded when first needed, so the
 * runtime links nothing new). The server's certificate is checked by Windows against its trusted roots.
 * TLS 1.2 only: with 1.3, SChannel reports the server's session tickets as renegotiation, which this file does not do. */
#define SECURITY_WIN32
#include <winsock2.h>
#include <windows.h>
#include <security.h>
#include <schannel.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ap.h"

#define IO_SIZE (16384 + 2048)

typedef struct {
    SOCKET s;
    CredHandle cred;
    CtxtHandle ctx;
    int have_ctx;
    SecPkgContext_StreamSizes sz;
    char in[IO_SIZE];                                /* ciphertext received, not yet decrypted */
    int in_n;
    char plain[IO_SIZE];                             /* decrypted, not yet handed out */
    int plain_n, plain_pos;
    CRITICAL_SECTION cs;                             /* SChannel: encrypt and decrypt never at once (sender and reader threads) */
} Tls;

static PSecurityFunctionTableA F;

static int load(void) {
    if (F) return 1;
    HMODULE m = LoadLibraryA("secur32.dll");
    INIT_SECURITY_INTERFACE_A init = m ? (INIT_SECURITY_INTERFACE_A)(void *)GetProcAddress(m, "InitSecurityInterfaceA") : NULL;
    F = init ? init() : NULL;
    return F != NULL;
}

static int send_all(SOCKET s, const char *p, int n) {
    while (n > 0) { int k = send(s, p, n, 0); if (k <= 0) return -1; p += k; n -= k; }
    return 0;
}
static int fill(Tls *t) {                            /* more ciphertext; -1 = closed or full */
    if (t->in_n >= IO_SIZE) return -1;
    int k = recv(t->s, t->in + t->in_n, IO_SIZE - t->in_n, 0);
    if (k <= 0) return -1;
    t->in_n += k;
    return 0;
}

void ap_tls_close(void *p) {
    Tls *t = p;
    if (!t) return;
    if (t->have_ctx) F->DeleteSecurityContext(&t->ctx);
    DeleteCriticalSection(&t->cs);
    F->FreeCredentialsHandle(&t->cred);
    free(t);
}

void *ap_tls_open(uintptr_t sock, const char *host) {
    if (!load()) return NULL;
    Tls *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->s = (SOCKET)sock;
    InitializeCriticalSection(&t->cs);
    SCHANNEL_CRED sc = {0};
    sc.dwVersion = SCHANNEL_CRED_VERSION;
    sc.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
    sc.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
    if (F->AcquireCredentialsHandleA(NULL, (SEC_CHAR *)UNISP_NAME_A, SECPKG_CRED_OUTBOUND, NULL, &sc, NULL, NULL, &t->cred, NULL) != SEC_E_OK) {
        free(t);
        return NULL;
    }
    DWORD flags = ISC_REQ_USE_SUPPLIED_CREDS | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONFIDENTIALITY | ISC_REQ_REPLAY_DETECT |
                  ISC_REQ_SEQUENCE_DETECT | ISC_REQ_STREAM, attrs;
    for (;;) {
        SecBuffer inb[2] = {{(unsigned long)t->in_n, SECBUFFER_TOKEN, t->in}, {0, SECBUFFER_EMPTY, NULL}};
        SecBuffer outb[1] = {{0, SECBUFFER_TOKEN, NULL}};
        SecBufferDesc ind = {SECBUFFER_VERSION, 2, inb}, outd = {SECBUFFER_VERSION, 1, outb};
        SECURITY_STATUS st = F->InitializeSecurityContextA(&t->cred, t->have_ctx ? &t->ctx : NULL, (SEC_CHAR *)host, flags, 0, 0,
                                                           t->have_ctx ? &ind : NULL, 0, t->have_ctx ? NULL : &t->ctx, &outd, &attrs, NULL);
        t->have_ctx = 1;
        if (outb[0].cbBuffer && outb[0].pvBuffer) {
            int bad = send_all(t->s, outb[0].pvBuffer, (int)outb[0].cbBuffer);
            F->FreeContextBuffer(outb[0].pvBuffer);
            if (bad) break;
        }
        if (st == SEC_E_INCOMPLETE_MESSAGE) { if (fill(t)) break; continue; }
        if (inb[1].BufferType == SECBUFFER_EXTRA) { memmove(t->in, t->in + t->in_n - inb[1].cbBuffer, inb[1].cbBuffer); t->in_n = (int)inb[1].cbBuffer; }
        else t->in_n = 0;
        if (st == SEC_E_OK) {
            if (F->QueryContextAttributesA(&t->ctx, SECPKG_ATTR_STREAM_SIZES, &t->sz) != SEC_E_OK) break;
            return t;
        }
        if (st != SEC_I_CONTINUE_NEEDED) break;      /* a certificate Windows does not trust, a server that is no TLS, ... */
        if (!t->in_n && fill(t)) break;
    }
    ap_tls_close(t);
    return NULL;
}

int ap_tls_send(void *p, const void *data, int n) {
    Tls *t = p;
    const char *d = data;
    char *buf = malloc(t->sz.cbHeader + t->sz.cbMaximumMessage + t->sz.cbTrailer);
    if (!buf) return -1;
    while (n > 0) {
        int k = n < (int)t->sz.cbMaximumMessage ? n : (int)t->sz.cbMaximumMessage;
        memcpy(buf + t->sz.cbHeader, d, k);
        SecBuffer b[4] = {{t->sz.cbHeader, SECBUFFER_STREAM_HEADER, buf}, {(unsigned long)k, SECBUFFER_DATA, buf + t->sz.cbHeader},
                          {t->sz.cbTrailer, SECBUFFER_STREAM_TRAILER, buf + t->sz.cbHeader + k}, {0, SECBUFFER_EMPTY, NULL}};
        SecBufferDesc bd = {SECBUFFER_VERSION, 4, b};
        EnterCriticalSection(&t->cs);
        SECURITY_STATUS es = F->EncryptMessage(&t->ctx, 0, &bd, 0);
        LeaveCriticalSection(&t->cs);
        if (es != SEC_E_OK ||
            send_all(t->s, buf, (int)(b[0].cbBuffer + b[1].cbBuffer + b[2].cbBuffer))) { free(buf); return -1; }
        d += k; n -= k;
    }
    free(buf);
    return 0;
}

int ap_tls_recv(void *p, void *out, int n) {        /* bytes given (>0), or 0 / -1 = closed */
    Tls *t = p;
    while (t->plain_pos >= t->plain_n) {
        t->plain_pos = t->plain_n = 0;
        if (!t->in_n && fill(t)) return -1;
        SecBuffer b[4] = {{(unsigned long)t->in_n, SECBUFFER_DATA, t->in}, {0, SECBUFFER_EMPTY, NULL},
                          {0, SECBUFFER_EMPTY, NULL}, {0, SECBUFFER_EMPTY, NULL}};
        SecBufferDesc bd = {SECBUFFER_VERSION, 4, b};
        EnterCriticalSection(&t->cs);
        SECURITY_STATUS st = F->DecryptMessage(&t->ctx, &bd, 0, NULL);
        LeaveCriticalSection(&t->cs);
        if (st == SEC_E_INCOMPLETE_MESSAGE) { if (fill(t)) return -1; continue; }
        if (st != SEC_E_OK) return -1;               /* closed (SEC_I_CONTEXT_EXPIRED), renegotiation, or broken */
        for (int i = 1; i < 4; i++)
            if (b[i].BufferType == SECBUFFER_DATA && b[i].cbBuffer <= sizeof t->plain) { memcpy(t->plain, b[i].pvBuffer, b[i].cbBuffer); t->plain_n = (int)b[i].cbBuffer; }
        int extra = 0;
        for (int i = 1; i < 4; i++) if (b[i].BufferType == SECBUFFER_EXTRA) { memmove(t->in, b[i].pvBuffer, b[i].cbBuffer); extra = (int)b[i].cbBuffer; }
        t->in_n = extra;
    }
    int k = t->plain_n - t->plain_pos < n ? t->plain_n - t->plain_pos : n;
    memcpy(out, t->plain + t->plain_pos, k);
    t->plain_pos += k;
    return k;
}
