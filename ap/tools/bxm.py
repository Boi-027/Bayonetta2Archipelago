"""PlatinumGames BXM (binary XML) -> XML text. python bxm.py <file.bxm> [out.xml]

Layout (big-endian): "BXM\\0", u32 0, u16 node count, u16 data count, u32 string size;
nodes: (u16 child count, u16 first child, u16 attribute count, u16 data index);
data: (u16 name offset, u16 value offset) into the strings, 0xffff = none; then the strings (UTF-8, 0-terminated).
A node's name / text is data[data index]; its attributes are the next attribute-count data entries."""
import struct
import sys
from xml.sax.saxutils import escape, quoteattr


def parse(d):
    assert d[:4] in (b'BXM\0', b'XML\0'), d[:4]
    nn, nd, ssize = struct.unpack_from('>HHI', d, 8)
    nodes = [struct.unpack_from('>4H', d, 16 + 8 * i) for i in range(nn)]
    do = 16 + 8 * nn
    data = [struct.unpack_from('>2H', d, do + 4 * i) for i in range(nd)]
    so = do + 4 * nd

    def s(o):
        if o == 0xffff:
            return None
        e = d.index(b'\0', so + o)
        return d[so + o:e].decode('utf-8', 'replace')

    def node(i):
        cc, first, ac, di = nodes[i]
        name, text = s(data[di][0]), s(data[di][1])
        attrs = [(s(data[di + 1 + k][0]), s(data[di + 1 + k][1])) for k in range(ac)]
        return {'name': name, 'text': text, 'attrs': attrs, 'kids': [node(first + k) for k in range(cc)]}
    return node(0)


def to_xml(n, depth=0):
    pad = '  ' * depth
    a = ''.join(' %s=%s' % (k, quoteattr(v or '')) for k, v in n['attrs'])
    if not n['kids']:
        return '%s<%s%s>%s</%s>\n' % (pad, n['name'], a, escape(n['text'] or ''), n['name'])
    return '%s<%s%s>%s\n%s%s</%s>\n' % (pad, n['name'], a, escape(n['text'] or ''),
                                         ''.join(to_xml(k, depth + 1) for k in n['kids']), pad, n['name'])


if __name__ == '__main__':
    x = to_xml(parse(open(sys.argv[1], 'rb').read()))
    if len(sys.argv) > 2:
        open(sys.argv[2], 'w', encoding='utf-8').write(x)
    else:
        sys.stdout.write(x)
