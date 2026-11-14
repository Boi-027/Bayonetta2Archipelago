"""The item popups' icons: <mod>/ap_icons.bin, read by runtime/ap_game.c.

    python popup_icons.py <B2recomp folder> <mod folder>

The popup (the game's Bewitchment box) draws the medal of Bewitchment 3, a 64x64 square at (64, 64) of ui_core's
texture 0 (256x512, BC1). The runtime finds that texture's data in memory and, while an item popup is up, writes one of
the versions below over it (the renderer picks up the change), then the original back. One version per icon: the
game's own sprite of the item (Items page, accessories, weapons, techniques) on a dark disc, or the Archipelago logo.

ap_icons.bin: "APIC", u32 count, u32 texture bytes, u32 signature offset, 64 signature bytes (a unique run of the
original texture data at that offset), then count x (u32 key, texture bytes). Key: an apworld item ID, 0 = AP logo.
"""
import os
import struct
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bntx import Bntx, replace_region  # noqa: E402
from build_mod import dat_files, read_romfs_file  # noqa: E402
from title_logo import wtp_textures  # noqa: E402

MEDAL = (64, 64, 128, 128)                 # Bewitchment 3's medal in ui_core texture 0
AP_LOGO = os.path.join(os.path.dirname(HERE), 'assets', 'ap_logo.png')

CONSUMABLE_NO = [1, 2, 3, 4, 5, 6, 7, 8, 11, 13, 14, 15]          # Items.py CONSUMABLES -> ITM_ITEM_NAME_nn
WEAPON_NO = [1, 2, 3, 5, 6, 7, 9, 11, 15, 16, 17]                  # Items.py WEAPONS -> ITM_WEAPON_NAME_01_nn
TECHNIQUE_NO = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 16, 17, 12]   # Items.py TECHNIQUES -> TEC_NAME_SP_nn


def icon_sprites():
    """apworld item ID -> (ui archive, base, sprite name)"""
    out = {}
    for k, no in enumerate(CONSUMABLE_NO):
        out[220300 + k] = ('ui/ui_hud1_us.dat', 'hud1', 'hud1_icon_item_%02d' % no)
    out[220400] = ('ui/ui_hud1_us.dat', 'hud1', 'hud1_icon_item_19')
    out[220401] = ('ui/ui_hud1_us.dat', 'hud1', 'hud1_icon_item_21')
    for k in range(14):
        out[220100 + k] = ('ui/ui_hud1_us.dat', 'hud1', 'sub_icon_acsry_%02d' % (k + 1))
    for k, no in enumerate(WEAPON_NO):
        out[220001 + k] = ('ui/ui_hud1_single_us.dat', 'hud1_single', 'hud1_icon_weapon_01_%02d' % no)
    out[220001 + 11] = ('ui/ui_hud1_single_us.dat', 'hud1_single', 'hud1_icon_weapon_03_01')   # Unforgiven
    for k, no in enumerate(TECHNIQUE_NO):            # shortcut: shop_icon_tec_nn assumed in TEC_NAME_SP order; check
        out[220150 + k] = ('ui/ui_shop_us.dat', 'shop', 'shop_icon_tec_%02d' % no)
    return out


class UiFile:
    def __init__(self, b2recomp, path, base):
        d = read_romfs_file(b2recomp, path)
        m = {n: (o, s) for _, n, o, s, _ in dat_files(d)}
        wta = d[m[base + '.wta'][0]:sum(m[base + '.wta'])]
        self.wtp = d[m[base + '.wtp'][0]:sum(m[base + '.wtp'])]
        self.textures = wtp_textures(wta, self.wtp)
        hdr = struct.unpack_from('<7I', wta, 0)
        self.ids = [struct.unpack_from('<I', wta, hdr[6] + 4 * i)[0] for i in range(hdr[2])]
        self.uvd = d[m[base + '.uvd'][0]:sum(m[base + '.uvd'])]
        self.images = {}

    def sprite(self, name):
        at = self.uvd.find(name.encode() + b'\0')
        if at < 0:
            return None
        r = at + 0x40                                   # a record: name (64 bytes), then hash, texture id, x y w h
        tex_id, = struct.unpack_from('<I', self.uvd, r + 4)
        x, y, w, h = struct.unpack_from('<4f', self.uvd, r + 8)
        i = self.ids.index(tex_id)
        if i not in self.images:
            _, _, b = self.textures[i]
            self.images[i] = b.image(b.textures[0]).convert('RGBA')
        return self.images[i].crop((int(x), int(y), int(x + w), int(y + h)))


def tile(icon):
    """64x64: the icon on a dark disc with a thin gold ring (BC1: no alpha, so the square is opaque)"""
    t = Image.new('RGBA', (64, 64), (8, 8, 14, 255))
    d = ImageDraw.Draw(t)
    d.ellipse((1, 1, 62, 62), fill=(24, 20, 36, 255), outline=(196, 160, 86, 255), width=2)
    icon = icon.copy()
    icon.thumbnail((54, 54), Image.LANCZOS)
    t.alpha_composite(icon, ((64 - icon.size[0]) // 2, (64 - icon.size[1]) // 2))
    return t


def main(b2recomp, mod):
    core = UiFile(b2recomp, 'ui/ui_core_us.dat', 'core')
    off, size, b = core.textures[0]
    t = b.textures[0]
    blob = core.wtp[off:off + size]
    data = blob[t['data']:t['data'] + t['size']]
    sig_at = next(k for k in range(0, len(data), 64) if core.wtp.count(data[k:k + 64]) == 1)
    files = {}
    icons = {0: Image.open(AP_LOGO).convert('RGBA')}
    for key, (path, base, name) in icon_sprites().items():
        if path not in files:
            files[path] = UiFile(b2recomp, path, base)
        im = files[path].sprite(name)
        if im is None:
            print('popup icon: no sprite %s in %s (item %d keeps the AP logo)' % (name, path, key))
            continue
        icons[key] = im
    atlas = b.image(t).convert('RGBA')
    out = [b'APIC', struct.pack('<III', len(icons), len(data), sig_at), data[sig_at:sig_at + 64]]
    for key, im in sorted(icons.items()):
        canvas = atlas.copy()
        canvas.paste(tile(im), MEDAL[:2])
        nb = Bntx(bytes(blob))
        new = replace_region(nb, nb.textures[0], canvas, MEDAL)
        out.append(struct.pack('<I', key) + new[t['data']:t['data'] + t['size']])
    path = os.path.join(mod, 'ap_icons.bin')
    with open(path, 'wb') as f:
        f.write(b''.join(out))
    print('wrote %s (%d icons)' % (path, len(icons)))
    return icons


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
