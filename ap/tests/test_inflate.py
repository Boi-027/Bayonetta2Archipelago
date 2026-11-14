"""Self-test of runtime/ap_inflate.c against Python's zlib, framed like WebSocket permessage-deflate.

    python test_inflate.py <B2recomp folder> <scratch folder>
Builds a tiny C driver with the runtime's compiler, feeds it each compressed case and compares the output."""
import os
import random
import subprocess
import sys
import zlib

b2, tmp = sys.argv[1], sys.argv[2]
os.makedirs(tmp, exist_ok=True)
driver = os.path.join(tmp, 'inflate_driver.c')
open(driver, 'w').write(r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
uint8_t *ap_inflate(const uint8_t *in, size_t n, size_t *out_len);
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); static uint8_t in[1 << 24]; size_t n = fread(in, 1, sizeof in, f); fclose(f);
    size_t len; uint8_t *o = ap_inflate(in, n, &len);
    if (!o) { printf("NULL\n"); return 1; }
    f = fopen(argv[2], "wb"); fwrite(o, 1, len, f); fclose(f); free(o); return 0;
}
''')
exe = os.path.join(tmp, 'inflate_driver.exe')
clang = os.path.join(b2, 'deps', 'llvm-mingw', 'bin', 'x86_64-w64-mingw32-clang.exe')
subprocess.check_call([clang, '-O1', '-I', os.path.join(b2, 'runtime'), driver, os.path.join(b2, 'runtime', 'ap_inflate.c'), '-o', exe])

rnd = random.Random(7)
cases = {
    'empty': b'',
    'json': b'[{"cmd":"RoomInfo","version":{"major":0,"minor":6,"build":8,"class":"Version"},"tags":["AP"]}]',
    'repeat': b'[{"cmd":"PrintJSON","data":[{"text":"Boi found their Witch Heart"}]}]' * 400,
    'random': bytes(rnd.getrandbits(8) for _ in range(70000)),           # incompressible: stored blocks
    'text': ' '.join(rnd.choice(['Bayonetta', 'Jeanne', 'Loki', 'halo', 'verse', 'Umbran', 'Tear']) for _ in range(50000)).encode(),
}
fails = 0
for name, data in cases.items():
    for level in (0, 1, 6, 9):
        c = zlib.compressobj(level, zlib.DEFLATED, -15)
        z = c.compress(data) + c.flush(zlib.Z_SYNC_FLUSH)
        assert z.endswith(b'\x00\x00\xff\xff')
        z = z[:-4] + b'\x00\x00\xff\xff'                # what a client does: the server drops it, the client adds it
        zin, zout = os.path.join(tmp, 'z.bin'), os.path.join(tmp, 'z.out')
        open(zin, 'wb').write(z)
        r = subprocess.run([exe, zin, zout], capture_output=True, text=True)
        ok = r.returncode == 0 and open(zout, 'rb').read() == data
        fails += not ok
        print('%-7s level %d: %s' % (name, level, 'ok' if ok else 'FAILED ' + r.stdout.strip()))
broken = os.path.join(tmp, 'broken.bin')
open(broken, 'wb').write(b'\x07\xff\x00')               # reserved block type 3
r = subprocess.run([exe, broken, os.path.join(tmp, 'z.out')], capture_output=True, text=True)
print('broken input refused:', r.stdout.strip() == 'NULL')
fails += r.stdout.strip() != 'NULL'
sys.exit(1 if fails else 0)
