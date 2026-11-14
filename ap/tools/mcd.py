"""PlatinumGames MCD message files (Bayonetta 2, Switch): read, add messages, write.

Layout (all little-endian), in file order:
  header      10 x u32: messages off/count, symbols off/count, glyphs off/count, fonts off/count, events off/count
  char data   per line: (u16 symbol, i16 kerning) pairs; 0x8001 + width = a space; 0x8000 ends the line
  messages    16 bytes: paragraphs off, paragraph count, sequence number, event hash
  paragraphs  20 bytes: lines off, line count, u32, u32, font id
  lines       24 bytes: chars off, u32 0, word count, word count, f32 width, u32 0
  symbols      8 bytes: u16 font id, u16 character (UTF-16), u32 glyph index
  glyphs      40 bytes: u32 texture id, 9 x f32 (u1 v1 u2 v2 width height above below, ?)
Every block after the char data is followed by 4 zero bytes.
  fonts       20 bytes: u32 id, 4 x f32
  events      40 bytes: u32 CRC32(lowercase name) & 0x7fffffff, u32 message index, char name[32]; sorted by hash
  (a message's own event field keeps the full CRC32)

The game finds a message by the CRC32 of its lowercased name (0x7100dd220c) in the events.
"""
import struct
import zlib

END, SPACE = 0x8000, 0x8001


def name_hash(name):
    return zlib.crc32(name.lower().encode()) & 0xffffffff


def event_hash(name):
    return name_hash(name) & 0x7fffffff


class Mcd:
    def __init__(self, data):
        (mo, mn, so, sn, go, gn, fo, fn, eo, en) = struct.unpack_from('<10I', data, 0)
        self.symbols = [list(struct.unpack_from('<HHI', data, so + 8 * i)) for i in range(sn)]
        self.glyphs = [data[go + 40 * i:go + 40 * i + 40] for i in range(gn)]
        self.fonts = [data[fo + 20 * i:fo + 20 * i + 20] for i in range(fn)]
        self.events = [(struct.unpack_from('<II', data, eo + 40 * i), data[eo + 40 * i + 8:eo + 40 * i + 40]) for i in range(en)]
        self.messages = []
        for m in range(mn):
            po, pn, seq, ev = struct.unpack_from('<4I', data, mo + 16 * m)
            paras = []
            for p in range(pn):
                lo, ln, a, b, font = struct.unpack_from('<5I', data, po + 20 * p)
                lines = []
                for l in range(ln):
                    co, z0, cn, cn2, width, z1 = struct.unpack_from('<4IfI', data, lo + 24 * l)
                    lines.append({'words': list(struct.unpack_from('<%dH' % cn, data, co)), 'width': width, 'n2': cn2, 'z': (z0, z1)})
                paras.append({'lines': lines, 'a': a, 'b': b, 'font': font})
            self.messages.append({'paras': paras, 'seq': seq, 'event': ev})

    # ---------------------------------------------------------------- writing
    def build(self):
        out = bytearray(40)
        def align(n=4):
            while len(out) % n: out.append(0)
        for m in self.messages:                      # char data first, in message / paragraph / line order
            for p in m['paras']:
                for l in p['lines']:
                    l['_off'] = len(out)
                    out += struct.pack('<%dH' % len(l['words']), *l['words'])
        out += bytes(2); align()                     # at least one zero u16 after the char data, then 4-aligned
        pad = lambda: out.extend(bytes(4))
        mo = len(out); out += bytes(16 * len(self.messages)); pad()
        for m in self.messages:
            m['_off'] = len(out); out += bytes(20 * len(m['paras']))
        pad()
        for m in self.messages:
            for p in m['paras']:
                p['_off'] = len(out); out += bytes(24 * len(p['lines']))
        pad()
        for i, m in enumerate(self.messages):
            struct.pack_into('<4I', out, mo + 16 * i, m['_off'], len(m['paras']), m['seq'], m['event'])
            for j, p in enumerate(m['paras']):
                struct.pack_into('<5I', out, m['_off'] + 20 * j, p['_off'], len(p['lines']), p['a'], p['b'], p['font'])
                for k, l in enumerate(p['lines']):
                    struct.pack_into('<4IfI', out, p['_off'] + 24 * k, l['_off'], l['z'][0], len(l['words']), l['n2'], l['width'], l['z'][1])
        so = len(out)
        for s in self.symbols: out += struct.pack('<HHI', *s)
        pad(); go = len(out)
        for g in self.glyphs: out += g
        pad(); fo = len(out)
        for f in self.fonts: out += f
        pad(); eo = len(out)
        for (h, idx), name in self.events: out += struct.pack('<II', h, idx) + name
        struct.pack_into('<10I', out, 0, mo, len(self.messages), so, len(self.symbols), go, len(self.glyphs),
                         fo, len(self.fonts), eo, len(self.events))
        return bytes(out)

    # ---------------------------------------------------------------- text
    def text(self, m):
        chars = {i: chr(s[1]) for i, s in enumerate(self.symbols)}
        res = []
        for p in self.messages[m]['paras']:
            for l in p['lines']:
                w, s, i = l['words'], '', 0
                while i < len(w) and w[i] != END:
                    s += ' ' if w[i] == SPACE else chars.get(w[i], '?')
                    i += 2
                res.append(s)
        return res

    def find(self, name):
        h = event_hash(name)
        return next((idx for (eh, idx), _ in self.events if eh == h), None)

    def kerning_table(self, font):
        """(symbol, next char or None) -> kerning, from every line in this file written in `font`."""
        pairs, single = {}, {}
        for m in self.messages:
            for p in m['paras']:
                if p['font'] != font: continue
                for l in p['lines']:
                    w = l['words']
                    for i in range(0, len(w) - 1, 2):
                        if w[i] >= END: continue
                        nxt = w[i + 2] if i + 2 < len(w) and w[i + 2] < END else None
                        pairs[(w[i], nxt)] = w[i + 1]
                        single.setdefault(w[i], {}).setdefault(w[i + 1], 0)
                        single[w[i]][w[i + 1]] += 1
        return pairs, {s: max(c, key=c.get) for s, c in single.items()}

    def encode(self, text, font, space_width):
        """Text -> words for one line in `font`, with the kerning this file already uses for the same letters."""
        sym = {chr(s[1]): i for i, s in enumerate(self.symbols) if s[0] == font}
        missing = sorted({ch for ch in text if ch != ' ' and ch not in sym})
        if missing:
            raise ValueError('font %d of this file has no glyph for %r' % (font, ''.join(missing)))
        pairs, common = self.kerning_table(font)
        ids = [None if ch == ' ' else sym[ch] for ch in text]
        words = []
        for i, s in enumerate(ids):
            if s is None:
                words += [SPACE, space_width]
                continue
            nxt = ids[i + 1] if i + 1 < len(ids) else None
            words += [s, pairs.get((s, nxt), common.get(s, 0))]
        return words + [END]

    def add_message(self, name, template, texts):
        """New message `name` shaped like message `template`; texts[i] replaces paragraph i's single line."""
        if self.find(name) is not None:
            raise ValueError(name + ' exists already')
        t = self.messages[template]
        paras = []
        for p, text in zip(t['paras'], texts):
            l0 = p['lines'][0]
            space = next((l0['words'][i + 1] for i in range(0, len(l0['words']) - 1, 2) if l0['words'][i] == SPACE), 20)
            words = self.encode(text, p['font'], space)
            paras.append({'lines': [{'words': words, 'width': l0['width'], 'n2': len(words), 'z': l0['z']}],
                          'a': p['a'], 'b': p['b'], 'font': p['font']})
        self.messages.append({'paras': paras, 'seq': max(m['seq'] for m in self.messages) + 1, 'event': name_hash(name)})
        self.events.append(((event_hash(name), len(self.messages) - 1), name.encode().ljust(32, b'\0')))
        self.events.sort(key=lambda e: e[0][0])
        return len(self.messages) - 1


if __name__ == '__main__':
    import sys
    d = open(sys.argv[1], 'rb').read()
    m = Mcd(d)
    same = m.build() == d
    print('round trip identical:', same)
    for i in range(len(m.messages)):
        print(i, m.text(i))
