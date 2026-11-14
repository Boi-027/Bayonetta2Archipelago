"""The launcher's pictures and lettering, made from the player's own game files (nothing of the game is shipped):

    python launcher_assets.py <B2recomp folder> <output mod folder>

Writes <output>/launcher/ (beside the Archipelago mod's romfs/; build_mod.py runs this too):
  logo.bmp      the title logo (BAYONETTA with the Archipelago logo in its "O", as the mod's title screen) and the gold "2"
  art.bmp       the blue key art of the title menu (title_menu_trial)
  art2.bmp      the city key art
  seal.bmp      the Umbran seal (white, alpha)
  moon.bmp      the crescent moon
  font.bmp      the title menu font (mmTIT001 font 1, the letters of STORY / CONTINUE), one row
  font.txt      per glyph: character code, x, width (the row is the font's height)
Pictures are 32-bit BMPs, top-down, premultiplied BGRA (what the launcher's AlphaBlend wants).
"""
import os
import struct
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from build_mod import read_romfs_file  # noqa: E402
from title_logo import compose, dat_member, glyph_font, wtp_textures  # noqa: E402

FRONT_LOGO = (48, 672, 1240, 1056)       # title_logoimg: the front (blue) BAYONETTA
GOLD_TWO = (1808, 8, 2024, 224)          # the gold "2"
SEAL = (1288, 0, 1776, 496)              # the Umbran seal
MOON = (992, 1176, 1416, 1608)           # the crescent moon
ART = (0, 0, 758, 368)                   # title_menu_trial: blue key art
ART2 = (0, 374, 758, 740)                # the city


def save_bmp(im, path):
    im = im.convert('RGBA')
    w, h = im.size
    raw = im.tobytes()
    px = bytearray(len(raw))
    for i in range(0, len(raw), 4):
        r, g, b, a = raw[i:i + 4]
        px[i:i + 4] = bytes((b * a // 255, g * a // 255, r * a // 255, a))
    with open(path, 'wb') as f:
        f.write(b'BM' + struct.pack('<IHHI', 14 + 40 + len(px), 0, 0, 14 + 40))
        f.write(struct.pack('<IiiHHIIiiII', 40, w, -h, 1, 32, 0, len(px), 2835, 2835, 0, 0))
        f.write(px)


def main(b2recomp, out_dir):
    out = os.path.join(out_dir, 'launcher')
    os.makedirs(out, exist_ok=True)
    dat = read_romfs_file(b2recomp, 'ui/ui_title_us.dat')
    a, b = dat_member(dat, 'title.wta')
    c, d = dat_member(dat, 'title.wtp')
    tex = {bn.textures[0]['name']: bn.image(bn.textures[0]) for _, _, bn in wtp_textures(dat[a:a + b], dat[c:c + d])}
    glyphs = glyph_font(b2recomp)

    atlas = compose(tex['title_logoimg'], glyphs)          # the logo as the mod's title screen shows it
    front = atlas.crop(FRONT_LOGO)
    two = tex['title_logoimg'].crop(GOLD_TWO)
    two = two.resize((round(two.size[0] * 0.72), round(two.size[1] * 0.72)), Image.LANCZOS)
    logo = Image.new('RGBA', (front.size[0] + two.size[0] - 70, front.size[1]))
    logo.alpha_composite(front, (0, 0))
    logo.alpha_composite(two, (front.size[0] - 70, 8))
    save_bmp(logo, os.path.join(out, 'logo.bmp'))

    save_bmp(tex['title_menu_trial'].crop(ART), os.path.join(out, 'art.bmp'))
    save_bmp(tex['title_menu_trial'].crop(ART2), os.path.join(out, 'art2.bmp'))
    seal = tex['title_logoimg'].crop(SEAL)
    save_bmp(seal.resize((320, 320), Image.LANCZOS), os.path.join(out, 'seal.bmp'))
    moon = tex['title_logoimg'].crop(MOON)
    save_bmp(moon.resize((216, 220), Image.LANCZOS), os.path.join(out, 'moon.bmp'))

    # each glyph cut to its ink, the row to the ink of all of them (the cells have wide empty margins); white, so
    # the launcher can colour it
    h0 = max(g.size[1] for g in glyphs.values())
    cells = {}
    for ch, g in glyphs.items():
        cell = Image.new('RGBA', (g.size[0], h0))
        cell.alpha_composite(g, (0, h0 - g.size[1]))
        cells[ch] = cell
    top = min(c.getchannel('A').point(lambda v: 255 if v > 24 else 0).getbbox()[1] for c in cells.values())
    bottom = max(c.getchannel('A').point(lambda v: 255 if v > 24 else 0).getbbox()[3] for c in cells.values())
    order = sorted(cells)
    inked = {}
    for ch in order:
        c = cells[ch].crop((0, top, cells[ch].size[0], bottom))
        x0, _, x1, _ = c.getchannel('A').point(lambda v: 255 if v > 24 else 0).getbbox()
        a = c.crop((x0, 0, x1, c.size[1])).getchannel('A')
        inked[ch] = Image.merge('RGBA', (Image.new('L', a.size, 255),) * 3 + (a,))
    h = bottom - top
    row = Image.new('RGBA', (sum(inked[ch].size[0] + 4 for ch in order), h))
    x, lines = 0, []
    for ch in order:
        g = inked[ch]
        row.alpha_composite(g, (x, 0))
        lines.append('%d %d %d' % (ord(ch), x, g.size[0]))
        x += g.size[0] + 4
    save_bmp(row, os.path.join(out, 'font.bmp'))
    with open(os.path.join(out, 'font.txt'), 'w') as f:
        f.write('%d\n' % h + '\n'.join(lines) + '\n')
    print('wrote', out, '(%d glyphs, height %d)' % (len(order), h))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
