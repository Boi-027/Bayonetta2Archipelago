"""The Archipelago title logo: the AP logo in the "O" of BAYONETTA and "ARCHIPELAGO / MADE BY BoI" under it, in the
game's own menu font. Edits the logo atlas (title_logoimg, 2048x2048 BC3) of ui/ui_title_us.dat.

    python title_logo.py <B2recomp folder> <output mod folder> [--preview <png>]

Atlas places (pixels of title_logoimg, measured on the v1.2 texture):
  front logo (blue outline)  "O": inner ring centre (630, 850), inside radius ~50, a lens in it
  back logo (dark)           "O": lens around (634, 262)
  free strip under "NE" of the front logo: x 676..960, y 948..1046 (left of 676 is the O's cross arm)
"""
import io
import os
import struct
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bntx import Bntx            # noqa: E402
from build_mod import read_romfs_file, dat_files  # noqa: E402
from mcd import Mcd              # noqa: E402

AP_LOGO = os.path.join(os.path.dirname(HERE), 'assets', 'ap_logo.png')
LOGO_TEX = 'title_logoimg'
FRONT_O, FRONT_R = (630, 850), 50
BACK_LENS = (634, 262)
TEXT_BOX = (676, 948, 960, 1046)
LINES = [('ARCHIPELAGo', 38), ('MADE BY BoI', 26)]       # text, cap height in atlas pixels


def dat_member(dat, name):
    for _, n, off, size, _ in dat_files(dat):
        if n == name:
            return off, size
    raise KeyError(name)


def wtp_textures(wta, wtp):
    """[(offset, size, Bntx)] for each BNTX packed in a .wtp, from its .wta"""
    n, = struct.unpack_from('<I', wta, 8)
    offs, sizes = struct.unpack_from('<2I', wta, 12)
    out = []
    for i in range(n):
        o, = struct.unpack_from('<I', wta, offs + 4 * i)
        s, = struct.unpack_from('<I', wta, sizes + 4 * i)
        out.append((o, s, Bntx(wtp[o:o + s])))
    return out


def glyph_font(b2recomp):
    """letter -> RGBA glyph image of the title menu font (font 1 of mmTIT001), and that font's glyph height"""
    dat = read_romfs_file(b2recomp, 'mm/mmTIT001_us.dat')
    mcd = Mcd(dat[slice(*(lambda o, s: (o, o + s))(*dat_member(dat, 'mmTIT001.mcd')))])
    wta = dat[slice(*(lambda o, s: (o, o + s))(*dat_member(dat, 'mmTIT001.wta')))]
    wtp = dat[slice(*(lambda o, s: (o, o + s))(*dat_member(dat, 'mmTIT001.wtp')))]
    (_, _, b), = wtp_textures(wta, wtp)[:1]
    atlas = b.image(b.textures[0])
    W, H = atlas.size
    glyphs = {}
    for font, ch, g in mcd.symbols:
        if font != 1:
            continue
        tex, u1, v1, u2, v2, w, h, *_ = struct.unpack_from('<I8f', mcd.glyphs[g])
        glyphs[chr(ch)] = atlas.crop((round(u1 * W), round(v1 * H), round(u2 * W), round(v2 * H)))
    return glyphs


def set_text(glyphs, text, cap):
    """one line of text from glyph images, scaled so the glyph box is `cap` pixels high"""
    h0 = max(g.size[1] for g in glyphs.values())
    k = cap / h0
    parts, x = [], 0
    for ch in text:
        if ch == ' ':
            x += int(cap * 0.35)
            continue
        g = glyphs[ch]
        g = g.resize((max(1, round(g.size[0] * k)), cap), Image.LANCZOS)
        parts.append((x, g))
        x += g.size[0] - int(cap * 0.06)
    line = Image.new('RGBA', (x + int(cap * 0.06), cap))
    for px, g in parts:
        line.alpha_composite(g, (px, 0))
    return line


def colour(mask_img):
    """the front logo's look: pale cyan core, blue towards the bottom, a dark navy edge for contrast"""
    a = mask_img.getchannel('A').filter(ImageFilter.MaxFilter(3))       # a little heavier than the menu font
    w, h = a.size
    grad = Image.new('RGB', (1, h))
    for y in range(h):
        t = y / max(1, h - 1)
        grad.putpixel((0, y), (int(225 - 120 * t), int(250 - 80 * t), 255))
    fill = grad.resize((w, h))
    edge = a.filter(ImageFilter.MaxFilter(7))
    out = Image.new('RGBA', (w, h), (6, 14, 40, 0))
    out.putalpha(edge)
    body = fill.convert('RGBA'); body.putalpha(a)
    out.alpha_composite(body)
    return out


def compose(atlas, glyphs):
    im = atlas.copy()
    # clear the lenses: everything inside the front "O"'s inner ring, and the back lens
    clear = Image.new('L', im.size, 0)
    d = ImageDraw.Draw(clear)
    cx, cy = FRONT_O
    d.ellipse((cx - FRONT_R + 4, cy - FRONT_R + 4, cx + FRONT_R - 4, cy + FRONT_R - 4), fill=255)
    bx, by = BACK_LENS
    d.ellipse((bx - 22, by - 40, bx + 22, by + 40), fill=255)
    im.putalpha(ImageChops.subtract(im.getchannel('A'), clear))
    # the AP logo in the front "O"
    logo = Image.open(AP_LOGO).convert('RGBA')
    side = 2 * FRONT_R - 14
    k = side / max(logo.size)
    logo = logo.resize((round(logo.size[0] * k), round(logo.size[1] * k)), Image.LANCZOS)
    im.alpha_composite(logo, (cx - logo.size[0] // 2, cy - logo.size[1] // 2))
    # the two lines under "NE", centred in the free strip
    x0, y0, x1, y1 = TEXT_BOX
    y = y0
    for text, cap in LINES:
        line = colour(set_text(glyphs, text, cap))
        if line.size[0] > x1 - x0:
            line = line.resize((x1 - x0, round(line.size[1] * (x1 - x0) / line.size[0])), Image.LANCZOS)
        im.alpha_composite(line, (x0 + (x1 - x0 - line.size[0]) // 2, y))
        y += line.size[1] + 6
    assert y <= y1 + 6, 'text does not fit the strip under the logo'
    return im


def main(b2recomp, out_dir, preview=None):
    dat = bytearray(read_romfs_file(b2recomp, 'ui/ui_title_us.dat'))
    wta_off, wta_size = dat_member(dat, 'title.wta')
    wtp_off, wtp_size = dat_member(dat, 'title.wtp')
    packed = wtp_textures(bytes(dat[wta_off:wta_off + wta_size]), bytes(dat[wtp_off:wtp_off + wtp_size]))
    o, s, b = next(p for p in packed if p[2].textures[0]['name'] == LOGO_TEX)
    t = b.textures[0]
    new = compose(b.image(t), glyph_font(b2recomp))
    if preview:
        bg = Image.new('RGBA', new.size, (10, 20, 50, 255)); bg.alpha_composite(new)
        bg.crop((0, 600, 1300, 1100)).convert('RGB').save(preview)
    blob = b.replace(t, new)
    assert len(blob) == s
    dat[wtp_off + o:wtp_off + o + s] = blob                 # same size: nothing else in the archive moves
    path = os.path.join(out_dir, 'romfs', 'ui', 'ui_title_us.dat')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(dat)
    print('wrote', path)


if __name__ == '__main__':
    args = sys.argv[1:]
    pv = None
    if '--preview' in args:
        i = args.index('--preview'); pv = args[i + 1]; del args[i:i + 2]
    main(args[0], args[1], pv)
