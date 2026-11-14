"""A tiny Archipelago test player (standard library only; ws://, no compression) for hosted tests.

    python ap_test_client.py <host:port> <slot> <game> <command> [...]
commands (run in order): wait <s> | check <n> (its next n missing locations) | checkall | bounce <tag> <json data>
| say <text>. Prints the server's PrintJSON lines (item sends) as plain text."""
import base64
import json
import os
import socket
import struct
import sys
import time


class Ws:
    def __init__(self, host, port):
        self.s = socket.create_connection((host, int(port)), timeout=10)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall(('GET / HTTP/1.1\r\nHost: %s:%s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                        'Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n' % (host, port, key)).encode())
        head = b''
        while b'\r\n\r\n' not in head:
            head += self.s.recv(1)
        assert b' 101 ' in head.split(b'\r\n')[0], head
        self.buf = b''

    def send(self, obj):
        data = json.dumps(obj).encode()
        mask = os.urandom(4)
        n = len(data)
        hdr = bytes([0x81]) + (bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack('>H', n)
                                if n < 65536 else bytes([0x80 | 127]) + struct.pack('>Q', n))
        self.s.sendall(hdr + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def _read(self, n):
        while len(self.buf) < n:
            chunk = self.s.recv(65536)
            if not chunk:
                raise EOFError
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def recv(self, timeout):
        self.s.settimeout(timeout)
        try:
            b0, b1 = self._read(2)
        except (socket.timeout, TimeoutError):
            return None
        n = b1 & 0x7f
        if n == 126:
            n, = struct.unpack('>H', self._read(2))
        elif n == 127:
            n, = struct.unpack('>Q', self._read(8))
        data = self._read(n)
        if b0 & 0x0f == 9:                                  # ping: answer with a pong (masked, same payload)
            mask = os.urandom(4)
            self.s.sendall(bytes([0x8a, 0x80 | len(data)]) + mask + bytes(x ^ mask[i % 4] for i, x in enumerate(data)))
            return None                                     # the caller's wait goes on with its own deadline
        return json.loads(data.decode('utf-8', 'replace')) if b0 & 0x0f == 1 else None


def main():
    host, port = sys.argv[1].split(':')
    slot, game, cmds = sys.argv[2], sys.argv[3], sys.argv[4:]
    ws = Ws(host, port)
    missing = []

    t0 = time.time()

    def pump(t):
        end = time.time() + t
        while time.time() < end:
            msg = ws.recv(max(0.05, end - time.time()))
            for p in msg or []:
                c = p.get('cmd')
                if c == 'RoomInfo':
                    ws.send([{'cmd': 'Connect', 'game': game, 'name': slot, 'password': '', 'uuid': 'aptest',
                              'version': {'major': 0, 'minor': 6, 'build': 8, 'class': 'Version'},
                              'items_handling': 7, 'tags': ['DeathLink', 'RingLink', 'TrapLink'], 'slot_data': False}])
                elif c == 'Connected':
                    missing[:] = p['missing_locations']
                    print('connected as %s, %d locations missing' % (slot, len(missing)), flush=True)
                elif c == 'ConnectionRefused':
                    print('refused', p, flush=True)
                elif c == 'PrintJSON':
                    print('%5.1f server:' % (time.time() - t0), ''.join(str(d.get('text', '')) for d in p.get('data', [])), flush=True)
                elif c == 'Bounced':
                    print('%5.1f bounced:' % (time.time() - t0), p.get('tags'), p.get('data'), flush=True)
                elif c == 'ReceivedItems':
                    print('received %d items (index %d)' % (len(p['items']), p['index']), flush=True)

    pump(3)
    i = 0
    while i < len(cmds):
        c = cmds[i]
        if c == 'wait':
            pump(float(cmds[i + 1])); i += 2
        elif c == 'check':
            n = int(cmds[i + 1]); i += 2
            locs, missing[:] = missing[:n], missing[n:]
            ws.send([{'cmd': 'LocationChecks', 'locations': locs}]); pump(1)
        elif c == 'checkall':
            ws.send([{'cmd': 'LocationChecks', 'locations': list(missing)}]); missing[:] = []; pump(1); i += 1
        elif c == 'bounce':
            data = json.loads(cmds[i + 2]); data.setdefault('source', slot); data.setdefault('time', time.time())
            ws.send([{'cmd': 'Bounce', 'tags': [cmds[i + 1]], 'data': data}]); pump(1); i += 3
        elif c == 'say':
            ws.send([{'cmd': 'Say', 'text': cmds[i + 1]}]); pump(1); i += 2
        else:
            raise SystemExit('unknown command ' + c)


if __name__ == '__main__':
    main()
