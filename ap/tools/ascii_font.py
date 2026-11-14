"""Completes font 20 (the game's help-text font) of one message file with every printable ASCII character, so the
runtime can write any text (server addresses, slot names, item names) into a message in the game's own lettering.

The glyph images come from the game's other English message files (each carries only the glyphs it uses; together
they have 77 of the 94); '_' is made from '-'. New glyphs go into empty space of the file's atlas: the region must be
fully transparent, and only the 4x4 blocks inside it are re-encoded (the rest of the texture keeps its bytes).
"""
import struct

from PIL import Image

FONT = 20
CELL_H = 53                                            # every font-20 glyph is a full-height cell (baseline 10 up)
WANTED = [chr(c) for c in range(33, 127)]


def font20_sources(dats, dat_files, Mcd, wtp_atlas):
    """char -> (glyph record bytes, glyph image) from the first message file that has it in font 20"""
    found = {}
    for dat in dats:
        members = {n: (off, size) for _, n, off, size, _ in dat_files(dat)}
        mcd_name = next((n for n in members if n.endswith('.mcd')), None)
        if not mcd_name:
            continue
        base = mcd_name[:-4]
        m = Mcd(dat[members[mcd_name][0]:sum(members[mcd_name])])
        atlas = None
        for font, ch, g in m.symbols:
            c = chr(ch)
            if font != FONT or c in found or c not in WANTED:
                continue
            if atlas is None:
                atlas = wtp_atlas(dat, members, base)
            rec = m.glyphs[g]
            _, u1, v1, u2, v2 = struct.unpack_from('<I4f', rec)
            W, H = atlas.size
            found[c] = (rec, atlas.crop((round(u1 * W), round(v1 * H), round(u2 * W), round(v2 * H))))
    if '_' not in found and '-' in found:                  # '_': the dash moved down onto the baseline area
        rec, dash = found['-']
        a = dash.getchannel('A')
        bbox = a.getbbox()
        under = Image.new('RGBA', dash.size)
        if bbox:
            bar = dash.crop(bbox)
            bar = bar.resize((dash.size[0] - 4, bar.size[1]))
            under.alpha_composite(bar, (2, CELL_H - 10 - bar.size[1] // 2))
        found['_'] = (rec, under)
    return found


def extend(m, atlas, sources, region, wanted=WANTED, partial=False):
    """Add the missing font-20 symbols (of `wanted`, in that order) to Mcd m, drawing their glyphs into `atlas` inside
    `region` (x0, y0, x1, y1). partial: place what fits and stop (call again with another region for the rest).
    Returns (atlas, box that changed) or (atlas, None) if nothing was placed."""
    have = {chr(ch) for font, ch, _ in m.symbols if font == FONT}
    missing = [c for c in wanted if c not in have and c in sources]
    if not missing:
        return atlas, None
    tex_id = struct.unpack_from('<I', m.glyphs[next(g for f, _, g in m.symbols if f == FONT)])[0]
    W, H = atlas.size
    x0, y0, x1, y1 = region
    x, y, used_y1 = x0, y0, y0
    placed = []
    for c in missing:
        rec, img = sources[c]
        w, h = img.size
        if x + w + 2 > x1:
            x, y = x0, y + CELL_H + 4
        if y + h > y1:
            if partial:
                break
            raise ValueError('the atlas region is too small for %d glyphs' % len(missing))
        placed.append((c, rec, img, x, y))
        x += w + 4
        used_y1 = max(used_y1, y + h)
    if not placed:
        return atlas, None
    box = (x0, y0, x1, (used_y1 + 3) // 4 * 4)
    if atlas.crop(box).getchannel('A').getbbox():
        raise ValueError('atlas region %r is not empty' % (box,))
    atlas = atlas.copy()
    for c, rec, img, px, py in placed:
        atlas.alpha_composite(img, (px, py))
        w, h = img.size
        fields = list(struct.unpack_from('<I9f', rec))
        fields[0] = tex_id
        fields[1:5] = [px / W, py / H, (px + w) / W, (py + h) / H]
        m.glyphs.append(struct.pack('<I9f', *fields))
        m.symbols.append([FONT, ord(c), len(m.glyphs) - 1])
    return atlas, box
