"""Builds the Archipelago RomFS mod from the player's own game files (nothing of the game is shipped).

    python build_mod.py <B2recomp folder> <output mod folder>

Writes <output>/romfs/mm/mmTIT001_us.dat (the title menu's messages, below) and, through title_logo.py,
<output>/romfs/ui/ui_title_us.dat (the logo). Added to the title menu's message file mmTIT001:
    TIT_BASE_AP  "ARCHIPELAGo" (the menu font; its 'o' is the game's stylised O, as in "SToRY") and its help line
    TIT_AP_*     the connect menu's entries (ADDRESS, SLOT, PASSWORD, CONNECT, BACK)
    TIT_AP_TXT_* text slots the runtime rewrites in memory (runtime/ap_ui.c): 96 characters each, help-text font
and font 20 (the help-text font) gets every printable ASCII character (ascii_font.py).
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mcd import Mcd  # noqa: E402

LABEL, HELP = 'TIT_BASE_03', 'TIT_HELP_03'           # templates: a main-menu label (fonts 1 + 2), a help line (font 20)
ENTRIES = ['ADDRESS', 'SLOT', 'PASSWORD', 'CONNECT', 'BACK']
COOP_ENTRIES = ['HOST', 'NAME', 'LOBBY', 'CHARACTER']   # the co-op menu's own labels (it reuses CONNECT, ADDRESS, ...)   # (the cheats: the Options' Display tab, opt_files)
SLOTS, SLOT_CHARS = 8, 96
MESSAGES = (  # (name, template message, paragraph texts)
    [('TIT_BASE_AP', LABEL, ['ARCHIPELAGo', 'ARCHIPELAGo']),
     ('TIT_HELP_AP', HELP, ['Connect to an Archipelago multiworld.'])]
    + [('TIT_AP_' + e, LABEL, [e.replace('O', 'o')] * 2) for e in ENTRIES]
    + [('TIT_AP_TXT_%d' % i, HELP, [' ' * SLOT_CHARS]) for i in range(SLOTS)]
    + [('TIT_BASE_COOP', LABEL, ['Co-oP', 'Co-oP']),               # story co-op (runtime/coop_net.c, ap_ui.c)
       ('TIT_HELP_COOP', HELP, ['Play the story together with a friend (direct connection).'])]
    + [('TIT_CO_' + e, LABEL, [e.replace('O', 'o')] * 2) for e in COOP_ENTRIES]
)
FONT_REGION = (796, 4, 1020, 1020)                    # the empty right side of mmTIT001's 1024x1024 atlas


class _File:
    def __init__(self, p): self.f = open(p, 'rb')
    def read(self, o, n): self.f.seek(o); return self.f.read(n)


def romfs_files(b2recomp, want):
    """{inner path: bytes} for every RomFS archive member for which want(path) is true"""
    sys.path.insert(0, os.path.join(b2recomp, 'scripts'))
    from cpk import Cpk
    romfs = os.path.join(b2recomp, 'targets', 'romfs')
    out = {}
    for name in sorted(os.listdir(romfs)):
        if name.endswith('.cpk'):
            c = Cpk(_File(os.path.join(romfs, name)))
            for e in c.files:
                if want(e['path']) and e['path'] not in out:
                    out[e['path']] = c.read(e)
    return out


def read_romfs_file(b2recomp, inner):
    got = romfs_files(b2recomp, lambda p: p == inner)
    if inner not in got:
        raise FileNotFoundError(inner)
    return got[inner]


def dat_files(d):
    assert d[:4] == b'DAT\0', 'not a DAT archive'
    n, off_tab, _, name_tab, size_tab = struct.unpack_from('<IIIII', d, 4)
    name_len, = struct.unpack_from('<I', d, name_tab)
    for i in range(n):
        name = d[name_tab + 4 + i * name_len:name_tab + 4 + (i + 1) * name_len].split(b'\0')[0].decode()
        yield i, name, struct.unpack_from('<I', d, off_tab + 4 * i)[0], struct.unpack_from('<I', d, size_tab + 4 * i)[0], size_tab


def replace_last_file(d, name, new):
    """Replace a DAT member that is the last one in the file (only its size changes)."""
    files = list(dat_files(d))
    i, _, off, size, size_tab = next(f for f in files if f[1] == name)
    assert off == max(f[2] for f in files), name + ' is not the last member'
    out = bytearray(d[:off]) + new
    struct.pack_into('<I', out, size_tab + 4 * i, len(new))
    return bytes(out)


def message_atlas(dat, members, base):
    """(offset in dat of the BNTX, Bntx) of a message file's font atlas (one texture in <base>.wtp)"""
    from title_logo import wtp_textures
    wta = dat[members[base + '.wta'][0]:sum(members[base + '.wta'])]
    wtp_off = members[base + '.wtp'][0]
    o, s, b = wtp_textures(wta, dat[wtp_off:sum(members[base + '.wtp'])])[0]
    return wtp_off + o, b


def main(b2recomp, out_dir):
    import ascii_font
    mm = romfs_files(b2recomp, lambda p: p.startswith('mm/mm') and p.endswith('_us.dat'))
    dat = bytearray(mm['mm/mmTIT001_us.dat'])
    members = {n: (off, size) for _, n, off, size, _ in dat_files(dat)}
    m = Mcd(bytes(dat[members['mmTIT001.mcd'][0]:sum(members['mmTIT001.mcd'])]))
    # font 20 with all of ASCII, into the empty side of the atlas
    sources = ascii_font.font20_sources(
        [mm[k] for k in sorted(mm)], dat_files, Mcd,
        lambda d, mem, base: (lambda b: b.image(b.textures[0]))(message_atlas(d, mem, base)[1]))
    tex_off, b = message_atlas(dat, members, 'mmTIT001')
    t = b.textures[0]
    atlas, box = ascii_font.extend(m, b.image(t), sources, FONT_REGION)
    if box:
        from bntx import replace_region
        blob = replace_region(b, t, atlas, box)
        dat[tex_off:tex_off + len(blob)] = blob          # same size: nothing else in the archive moves
    for name, template, texts in MESSAGES:
        m.add_message(name, m.find(template), texts)
    new = m.build()
    check = Mcd(new)                                  # what we wrote reads back as what we meant
    for name, _, texts in MESSAGES:
        got = check.text(check.find(name))
        assert got == texts, (name, got)
    lacking = set(ascii_font.WANTED) - {chr(c) for f, c, _ in check.symbols if f == 20}
    assert lacking <= set(r'#$%*<=>[\]^`{|}~'),'font 20 lacks %r' % ''.join(sorted(lacking))   # none of the game's fonts has these
    path = os.path.join(out_dir, 'romfs', 'mm', 'mmTIT001_us.dat')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(replace_last_file(bytes(dat), 'mmTIT001.mcd', new))
    print('wrote', path, '(%d glyphs added to font 20)' % (len(check.glyphs) - len(Mcd(mm['mm/mmTIT001_us.dat'][members['mmTIT001.mcd'][0]:sum(members['mmTIT001.mcd'])]).glyphs)))
    return atlas


# The item popups (runtime/ap_game.c): the "Received a Bewitchment." box with its two messages pointed at these.
POPUP_HEADS = [('AP_HEAD_RECV', 'Item Received'), ('AP_HEAD_SENT', 'Item Sent'), ('AP_HEAD_TRAP', 'Trap Received'),
               ('AP_HEAD_HINT', 'Hint')]
POPUP_CHARS = 64
# mmACV000's font 20 lacks these; its 256x512 atlas has room only below and right of its last glyph row
POPUP_GLYPHS = "0123456789FGJKNOYZjqxz'&:"
POPUP_REGIONS = [(100, 320, 252, 508), (4, 376, 100, 508)]


def popup_files(b2recomp, out_dir):
    import ascii_font
    mm = romfs_files(b2recomp, lambda p: p.startswith('mm/mm') and p.endswith('_us.dat'))
    sources = ascii_font.font20_sources(
        [mm[k] for k in sorted(mm)], dat_files, Mcd,
        lambda d, mem, base: (lambda b: b.image(b.textures[0]))(message_atlas(d, mem, base)[1]))
    for base, regions, add in (('mmACV000', POPUP_REGIONS, [('AP_POPUP_TEXT', 'COM_ACHIEVE_03', [' ' * POPUP_CHARS])]),
                               ('mmCOM001', [], [(n, 'COM_HUD_INFO_02', [t]) for n, t in POPUP_HEADS])):
        dat = bytearray(mm['mm/%s_us.dat' % base])
        members = {n: (off, size) for _, n, off, size, _ in dat_files(dat)}
        mo, ms = members[base + '.mcd']
        m = Mcd(bytes(dat[mo:mo + ms]))
        for region in regions:
            tex_off, b = message_atlas(dat, members, base)
            t = b.textures[0]
            atlas, box = ascii_font.extend(m, b.image(t), sources, region, POPUP_GLYPHS, partial=True)
            if box:
                from bntx import replace_region
                blob = replace_region(b, t, atlas, box)
                dat[tex_off:tex_off + len(blob)] = blob
        for name, template, texts in add:
            m.add_message(name, m.find(template), texts)
        new = m.build()
        check = Mcd(new)
        for name, _, texts in add:
            assert check.text(check.find(name)) == texts, name
        have = {chr(c) for f, c, _ in check.symbols if f == 20}
        if regions:
            print('%s: font 20 still lacks %r' % (base, ''.join(c for c in POPUP_GLYPHS if c not in have)))
        path = os.path.join(out_dir, 'romfs', 'mm', '%s_us.dat' % base)
        with open(path, 'wb') as f:
            f.write(replace_last_file(bytes(dat), base + '.mcd', new))
        print('wrote', path)


# The Options screen's Cheats pages (runtime/ap_opt.c): row labels, a slider label slot, and the help lines the runtime
# rewrites (moved to font 20, which gets all of ASCII, with room for 64 characters).
OPT_LABELS = [('AP_CHT_GOD', 'God Mode'), ('AP_CHT_HALOS', 'Infinite Halos'), ('AP_CHT_MAGIC', 'Infinite Magic'), ('AP_CHT_SKIP', 'Easy Cutscene Skip'), ('AP_CHT_QTE', 'Auto QTEs'),
              ('AP_CHT_NONE', ' '), ('AP_OPT_SLIDER', ' ' * 40)]
OPT_HELPS = ['OPT_DISPLAY_HELP_00', 'OPT_DISPLAY_HELP_01', 'OPT_DISPLAY_HELP_02', 'OPT_DISPLAY_HELP_05',
             'OPT_COMMON_HELP_00', 'OPT_TITLE_HELP_04']
OPT_HELP_CHARS = 64
OPT_FONT_REGION = (4, 592, 508, 1020)                  # the empty lower part of mmOPT000's 512x1024 atlas


# The chat box (runtime/ap_chat.c composes it, nvn_vk_draw.c blends it over the picture): the game's font-20 glyphs
# as coverage (alpha only; the runtime colours and shadows them). ap_chat_font.bin: 'APCA', u32 width, height (= the
# cell height), then for chars 32..126 u16 x, u16 width, then width * height alpha bytes.


def chat_font(b2recomp, out_dir):
    import ascii_font
    from PIL import Image
    mm = romfs_files(b2recomp, lambda p: p.startswith('mm/mm') and p.endswith('_us.dat'))
    sources = ascii_font.font20_sources(
        [mm[k] for k in sorted(mm)], dat_files, Mcd,
        lambda d, mem, base: (lambda b: b.image(b.textures[0]))(message_atlas(d, mem, base)[1]))
    cell = ascii_font.CELL_H
    glyphs, x = [], 0
    for ch in range(32, 127):
        img = sources[chr(ch)][1] if chr(ch) in sources else None
        box = img.getchannel('A').getbbox() if img else None
        if box:                                        # only the ink (the game's cells are padded wide), 3 px after
            img = img.crop((box[0], 0, box[2] + 3, img.size[1]))
        w = img.size[0] if box else 12                 # the space (and any glyph the game lacks): an empty cell
        glyphs.append((x, w, img if box else None))
        x += w + 2
    atlas = Image.new('L', (x, cell), 0)
    for gx, w, img in glyphs:
        if img:
            atlas.paste(img.getchannel('A'), (gx, cell - img.size[1]))
    head = struct.pack('<4s2I', b'APCA', x, cell) + b''.join(struct.pack('<2H', gx, w) for gx, w, _ in glyphs)
    path = os.path.join(out_dir, 'ap_chat_font.bin')
    with open(path, 'wb') as f:
        f.write(head + atlas.tobytes())
    print('wrote', path, '%dx%d' % (x, cell))


def opt_files(b2recomp, out_dir):
    import ascii_font
    mm = romfs_files(b2recomp, lambda p: p.startswith('mm/mm') and p.endswith('_us.dat'))
    sources = ascii_font.font20_sources(
        [mm[k] for k in sorted(mm)], dat_files, Mcd,
        lambda d, mem, base: (lambda b: b.image(b.textures[0]))(message_atlas(d, mem, base)[1]))
    for key in ('mm/mmOPT000_us.dat',):             # (mmopt000_us.dat: the same name on Windows; the game loads this one)
        if key not in mm:
            continue
        dat = bytearray(mm[key])
        members = {n: (off, size) for _, n, off, size, _ in dat_files(dat)}
        base = next(n for n in members if n.endswith('.mcd'))[:-4]
        mo, ms = members[base + '.mcd']
        m = Mcd(bytes(dat[mo:mo + ms]))
        tex_off, b = message_atlas(dat, members, base)
        t = b.textures[0]
        atlas, box = ascii_font.extend(m, b.image(t), sources, OPT_FONT_REGION)
        if box:
            from bntx import replace_region
            blob = replace_region(b, t, atlas, box)
            dat[tex_off:tex_off + len(blob)] = blob
        label = m.find('OPT_DISPLAY_BASE_01')
        for name, text in OPT_LABELS:
            m.add_message(name, label, [text])
        tmpl = m.messages[label]['paras'][0]          # font 20 and its space width
        l0 = tmpl['lines'][0]
        space = next((l0['words'][i + 1] for i in range(0, len(l0['words']) - 1, 2) if l0['words'][i] == 0x8001), 20)
        for name in OPT_HELPS:
            i = m.find(name)
            text = ' '.join(s.strip() for s in m.text(i))      # one line now; the runtime writes the real text
            p = m.messages[i]['paras'][0]
            words = m.encode(text.ljust(OPT_HELP_CHARS), 20, space)
            line = dict(p['lines'][0], words=words, n2=len(words))
            m.messages[i]['paras'] = [dict(p, lines=[line], font=20)]
        new = m.build()
        check = Mcd(new)
        for name, text in OPT_LABELS:
            assert check.text(check.find(name)) == [text], name
        path = os.path.join(out_dir, 'romfs', key)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(replace_last_file(bytes(dat), base + '.mcd', new))
        print('wrote', path)


def write_verse_table(b2recomp, out_dir):
    """ap_verses.txt: '<hash> <chapter> <verse number>' for every verse of ChapterData (core/core.dat). The game names
    a verse by CRC32(lowercase name) & 0x7fffffff; the runtime (ap_game.c) turns the hash into a location."""
    import zlib
    from bxm import parse
    core = read_romfs_file(b2recomp, 'core/core.dat')
    off, size = next((o, s) for _, n, o, s, _ in dat_files(core) if n == 'ChapterData.bxm')
    lines, phases = [], []
    for ch in parse(core[off:off + size])['kids']:
        c = int(dict(ch['attrs'])['chapter'])
        for phase in ch['kids']:
            phases.append('%s %d' % (dict(phase['attrs'])['phase'].lower(), c))
            for v in phase['kids']:
                a = dict(v['attrs'])
                if a.get('verse', '__None__') != '__None__' and a.get('normal'):
                    lines.append('%08x %d %s' % (zlib.crc32(a['verse'].lower().encode()) & 0x7fffffff, c, a['normal']))
    path = os.path.join(out_dir, 'ap_verses.txt')
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print('wrote', path, '(%d verses)' % len(lines))
    path = os.path.join(out_dir, 'ap_phases.txt')            # '<phase hex> <chapter>': chests name their chapter by it
    with open(path, 'w') as f:
        f.write('\n'.join(phases) + '\n')
    print('wrote', path, '(%d phases)' % len(phases))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
    write_verse_table(sys.argv[1], sys.argv[2])
    popup_files(sys.argv[1], sys.argv[2])            # the item popups' messages (mmACV000, mmCOM001)
    opt_files(sys.argv[1], sys.argv[2])              # the Options' Cheats pages (mmOPT000)
    chat_font(sys.argv[1], sys.argv[2])              # the chat box's lettering (ap_chat_font.bin)
    import popup_icons                                 # the popups' item icons (ap_icons.bin)
    popup_icons.main(sys.argv[1], sys.argv[2])
    import chests_mod                                  # story chests give 1 Halo (AP gives their item)
    chests_mod.main(sys.argv[1], sys.argv[2])
    import title_logo                                  # the title logo (ui/ui_title_us.dat)
    title_logo.main(sys.argv[1], sys.argv[2])
    import launcher_assets                             # the launcher's pictures and lettering (launcher/)
    launcher_assets.main(sys.argv[1], sys.argv[2])
