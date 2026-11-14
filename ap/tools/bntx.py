"""Nintendo Switch BNTX textures: list, export to PNG, replace one texture's image (same size and format).

    python bntx.py <file.wtp|.bntx> list
    python bntx.py <file> export <out dir>            every texture, mip 0, as PNG
    python bntx.py <file> replace <index> <png> <out file>

Textures are Tegra block-linear ("GOB") swizzled; BCn blocks are decoded / encoded with Pillow's bcn codec.
"""
import os
import struct
import sys

from PIL import Image

# format id (high byte = kind, low byte = type) -> (name, bytes per block, block w, block h, Pillow bcn n or None)
FORMATS = {
    0x0b: ('RGBA8', 4, 1, 1, None),
    0x1a: ('BC1', 8, 4, 4, 1),
    0x1b: ('BC2', 16, 4, 4, 2),
    0x1c: ('BC3', 16, 4, 4, 3),
    0x1d: ('BC4', 8, 4, 4, 4),
    0x1e: ('BC5', 16, 4, 4, 5),
    0x20: ('BC7', 16, 4, 4, 7),
}
GOB_W, GOB_H = 64, 8                                   # bytes x rows


def div_up(a, b):
    return (a + b - 1) // b


def gob_offset(x, y):
    """byte offset inside a 512-byte GOB for byte column x (0..63), row y (0..7)"""
    return ((x % 64) // 32) * 256 + ((y % 8) // 2) * 64 + ((x % 32) // 16) * 32 + (y % 2) * 16 + (x % 16)


def deswizzle(data, width_bytes, rows, block_height):
    gobs_x = div_up(width_bytes, GOB_W)
    out = bytearray(width_bytes * rows)
    for y in range(rows):
        for x in range(0, width_bytes, 16):           # 16-byte runs stay together
            gx, gy = x // GOB_W, y // GOB_H
            block = (gy // block_height) * gobs_x * block_height + gx * block_height + gy % block_height
            src = block * 512 + gob_offset(x, y)
            n = min(16, width_bytes - x)
            out[y * width_bytes + x:y * width_bytes + x + n] = data[src:src + n]
    return bytes(out)


def swizzle(linear, width_bytes, rows, block_height, size):
    gobs_x = div_up(width_bytes, GOB_W)
    out = bytearray(size)
    for y in range(rows):
        for x in range(0, width_bytes, 16):
            gx, gy = x // GOB_W, y // GOB_H
            block = (gy // block_height) * gobs_x * block_height + gx * block_height + gy % block_height
            dst = block * 512 + gob_offset(x, y)
            n = min(16, width_bytes - x)
            out[dst:dst + n] = linear[y * width_bytes + x:y * width_bytes + x + n]
    return bytes(out)


class Bntx:
    def __init__(self, data):
        self.data = bytearray(data)
        d = self.data
        assert d[:4] == b'BNTX', d[:4]
        assert d[0x20:0x24] == b'NX  '
        count, = struct.unpack_from('<I', d, 0x24)
        info_ptrs, = struct.unpack_from('<Q', d, 0x28)
        self.textures = []
        for i in range(count):
            brti, = struct.unpack_from('<Q', d, info_ptrs + 8 * i)
            assert d[brti:brti + 4] == b'BRTI', hex(brti)
            (flags, dim, tile, swz, mips, _, fmt, _, w, h, depth, arr, layout) = struct.unpack_from('<BBHHHIIIiiiii', d, brti + 0x10)
            img_size, align = struct.unpack_from('<II', d, brti + 0x50)
            name_ptr, _, mip_ptrs = struct.unpack_from('<QQQ', d, brti + 0x60)
            name_len, = struct.unpack_from('<H', d, name_ptr)
            name = d[name_ptr + 2:name_ptr + 2 + name_len].decode()
            mip0, = struct.unpack_from('<Q', d, mip_ptrs)
            self.textures.append(dict(index=i, name=name, fmt=fmt, w=w, h=h, mips=mips, arr=arr, block_height_log2=layout & 7,
                                      size=img_size, data=mip0, tile=tile))

    @staticmethod
    def fmt_info(t):
        return FORMATS.get(t['fmt'] >> 8)

    def mip0_geometry(self, t):
        name, bpb, bw, bh, _ = self.fmt_info(t)
        wb, rows = div_up(t['w'], bw) * bpb, div_up(t['h'], bh)
        # block height: from the texture layout; the hardware shrinks it for small images
        bhgt = 1 << t['block_height_log2']
        while bhgt > 1 and div_up(rows, GOB_H) <= bhgt // 2:
            bhgt //= 2
        size = div_up(wb, GOB_W) * GOB_W * div_up(rows, GOB_H * bhgt) * GOB_H * bhgt
        return wb, rows, bhgt, size

    def image(self, t):
        name, bpb, bw, bh, n = self.fmt_info(t)
        wb, rows, bhgt, size = self.mip0_geometry(t)
        lin = deswizzle(self.data[t['data']:t['data'] + size], wb, rows, bhgt)
        pw, ph = div_up(t['w'], bw) * bw, div_up(t['h'], bh) * bh
        if n is None:
            im = Image.frombuffer('RGBA', (pw, ph), lin, 'raw', 'RGBA', 0, 1)
        else:
            im = Image.frombuffer('RGBA' if n not in (4, 5) else ('L' if n == 4 else 'RGB'), (pw, ph), lin, 'bcn', n)
        return im.crop((0, 0, t['w'], t['h'])).convert('RGBA')

    def replace(self, t, im):
        """Write `im` (RGBA, the texture's size) as mip 0; the smaller mips are rebuilt by downscaling."""
        name, bpb, bw, bh, n = self.fmt_info(t)
        assert im.size == (t['w'], t['h']), (im.size, t['w'], t['h'])
        off = t['data']
        w, h = t['w'], t['h']
        for level in range(t['mips']):
            lt = dict(t, w=max(1, w), h=max(1, h))
            wb, rows, bhgt, size = self.mip0_geometry(lt)
            lvl = im.resize((lt['w'], lt['h']), Image.LANCZOS) if level else im
            pw, ph = div_up(lt['w'], bw) * bw, div_up(lt['h'], bh) * bh
            canvas = Image.new('RGBA', (pw, ph)); canvas.paste(lvl, (0, 0))
            if n is None:
                lin = canvas.tobytes()
            else:
                lin = encode_bcn(canvas, n)
            assert len(lin) == wb * rows, (len(lin), wb * rows)
            self.data[off:off + size] = swizzle(lin, wb, rows, bhgt, size)
            off += size
            w, h = w // 2, h // 2
            if level == 0 and t['mips'] > 1:
                # mip offsets are listed in the BRTI; follow them instead of assuming packing
                pass
        return bytes(self.data)


def replace_region(b, t, im, box):
    """Write the box (x0, y0, x1, y1; multiples of 4) of `im` into mip 0 of texture t, re-encoding only the 4x4 blocks
    inside it: every other block keeps its original bytes. Single-mip block-compressed textures only."""
    name, bpb, bw, bh, n = Bntx.fmt_info(t)
    assert n and t['mips'] == 1 and all(v % 4 == 0 for v in box), (name, t['mips'], box)
    wb, rows, bhgt, size = b.mip0_geometry(t)
    lin = bytearray(deswizzle(b.data[t['data']:t['data'] + size], wb, rows, bhgt))
    x0, y0, x1, y1 = box
    enc = encode_bcn(im.crop(box).convert('RGBA'), n)
    row_bytes = (x1 - x0) // 4 * bpb
    for r in range((y1 - y0) // 4):
        dst = (y0 // 4 + r) * wb + x0 // 4 * bpb
        lin[dst:dst + row_bytes] = enc[r * row_bytes:(r + 1) * row_bytes]
    b.data[t['data']:t['data'] + size] = swizzle(bytes(lin), wb, rows, bhgt, size)
    return bytes(b.data)


def encode_bcn(im, n):
    import io
    mode = {1: 'DXT1', 3: 'DXT5', 7: 'BC7'}.get(n)
    if n == 7:
        im.save(buf := io.BytesIO(), 'DDS', pixel_format='BC7')
    elif n in (1, 3):
        im.save(buf := io.BytesIO(), 'DDS', pixel_format=mode)
    else:
        raise ValueError('BC%d encoding is not supported' % n)
    dds = buf.getvalue()
    hdr = 128 + (20 if dds[84:88] == b'DX10' else 0)
    return dds[hdr:]


if __name__ == '__main__':
    b = Bntx(open(sys.argv[1], 'rb').read())
    cmd = sys.argv[2]
    if cmd == 'list':
        for t in b.textures:
            f = b.fmt_info(t)
            print('%3d %-24s %4dx%-4d %-6s fmt %04x mips %d arr %d bh %d data %x size %x tile %d'
                  % (t['index'], t['name'], t['w'], t['h'], f[0] if f else '?', t['fmt'], t['mips'], t['arr'],
                     1 << t['block_height_log2'], t['data'], t['size'], t['tile']))
    elif cmd == 'export':
        os.makedirs(sys.argv[3], exist_ok=True)
        for t in b.textures:
            if b.fmt_info(t):
                b.image(t).save(os.path.join(sys.argv[3], '%03d_%s.png' % (t['index'], t['name'])))
    elif cmd == 'replace':
        t = b.textures[int(sys.argv[3])]
        open(sys.argv[5], 'wb').write(b.replace(t, Image.open(sys.argv[4]).convert('RGBA')))
