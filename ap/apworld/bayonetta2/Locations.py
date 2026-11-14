"""Bayonetta 2 locations. Everything comes from the game's own tables (core/core.dat ChapterData.bxm; ap/NOTES.md).

IDs (the game's client computes them the same way, runtime/ap_game.c, from the verse's hash -> chapter, number):
  verse v of ChapterData chapter c (0, 1 Prologue; c >= 2 Chapter c - 1): 221000 + c * 32 + v
  chapter complete (its last ChapterData chapter c):              221900 + c
  verse v of chapter 20 (Tag Climax, Witch Trials):                222000 + v
  chest No n of story chapter c:                                  223000 + c * 64 + n
  crows 224000 + id, books 224100 + type, Tears 224200 + k, Gates of Hell 225100 + accessory, 225200 + technique
"""
from typing import NamedTuple

from .Items import ACCESSORIES, CHAPTER_NAMES, DATA_CHAPTERS, TECHNIQUES

VERSE_BASE, CHAPTER_BASE, TRIAL_BASE = 221000, 221900, 222000
# the verses each story chapter really has (ChapterData; Chapters I and XVI list one unnamed placeholder more)
STORY_VERSES = [1, 3, 13, 9, 1, 9, 11, 1, 5, 14, 8, 13, 1, 2, 12, 7, 8, 9]
BOSS_CHAPTERS = [3, 6, 11, 12, 15]       # shown chapters: the single-verse boss chapters (ChapterData 4, 7, 12, 13, 16)
# chapter 20: (phase, its verse numbers) in ChapterData order; c60..c7a are Tag Climax stages, c7b..c7f the Witch Trials
CHAPTER20 = [("c60", [1]), ("c61", [2]), ("c62", [3]), ("c63", [4]), ("c64", [5]), ("c65", [6, 7, 8]), ("c67", [9]),
             ("c69", [10, 11, 12]), ("c6b", [13]), ("c6c", [14]), ("c6d", [15, 16]), ("c6f", [17]), ("c70", [18]),
             ("c71", [19]), ("c72", [20]), ("c73", [21]), ("c74", [22]), ("c75", [23]), ("c76", [24]), ("c77", [25]),
             ("c78", [26, 27]), ("c79", [28]), ("c7a", [29]), ("c7b", [30, 31, 32, 33, 34]),
             ("c7c", [35, 36, 37, 38, 39, 40, 41]), ("c7d", [42, 43, 44, 45]), ("c7e", [46, 47, 48, 49, 50, 51]),
             ("c7f", [52])]
TRIALS = ["c7b", "c7c", "c7d", "c7e", "c7f"]
MUSPEL_REGION, TRIAL_REGION = -1, -2      # LocationData.chapter for the chapter-20 regions


class LocationData(NamedTuple):
    id: int
    chapter: int
    group: str


# The Muspelheims: the story stages' portals (SetPortal) lead to pa11 arenas, and ChapterData lists each arena as a verse
# of its chapter (PA11_ANGEL_nn_VRS / PA11_DEVIL_nn_VRS), so a Muspelheim is that verse - like B1's Alfheims. Named
# "<chapter> - Muspelheim k" (k: its order in the chapter); the ID is the verse's. (ChapterData chapter, verse number):
MUSPELHEIM_VERSES = [(2, 1), (2, 3), (2, 5), (2, 8), (3, 1), (3, 9), (5, 4), (6, 2), (6, 5), (6, 8), (8, 4), (9, 2), (9, 10),
                     (9, 12), (10, 3), (11, 1), (11, 9), (14, 2), (14, 11), (15, 3), (16, 8), (17, 3)]


def verse_names():
    """(shown chapter d, its name, ChapterData chapter c, verse v, the location's base name) for every story verse"""
    out = []
    for d, name in enumerate(CHAPTER_NAMES):              # d: the chapter as shown; c: its ChapterData chapters
        shown = muspel = 0
        for c in DATA_CHAPTERS[d]:
            for v in range(1, STORY_VERSES[c] + 1):
                shown += 1
                if (c, v) in MUSPELHEIM_VERSES:
                    muspel += 1
                    out.append((d, name, c, v, "%s - Muspelheim %d" % (name, muspel)))
                else:
                    out.append((d, name, c, v, "%s - Verse %d" % (name, shown)))
    return out


location_table: dict = {}
for d, name, c, v, base in verse_names():
    location_table[base] = LocationData(VERSE_BASE + c * 32 + v, d, "Muspelheims" if "Muspelheim" in base else "Verses")
for d, name in enumerate(CHAPTER_NAMES):
    location_table["%s - Complete" % name] = LocationData(CHAPTER_BASE + DATA_CHAPTERS[d][-1], d, "Chapter Completions")

# Verse medals (option verse_rank_target): the story verse's overall medal on its result. Tier 0 "Any Medal" (Bronze or
# better), tiers 1..5 "Bronze+" .. "Pure Platinum" (that medal or better); ID 226000 + tier * 1000 + c * 32 + v
# (Tag Climax and Witch Trials: c = 20, below).
RANK_BASE = 226000
RANK_TIERS = ["Any Medal", "Bronze+", "Silver+", "Gold+", "Platinum+", "Pure Platinum"]
for d, name, c, v, base in verse_names():
    for t, tier in enumerate(RANK_TIERS):
        location_table["%s Rank (%s)" % (base, tier)] = LocationData(RANK_BASE + t * 1000 + c * 32 + v, d, "Verse Ranks")

# Story chests: the stages' TreasureChest tables (ap/research/chests.tsv, ap/tools/dev/scan_chests.py), by ChapterData
# chapter -> chest No. The game marks an opened chest by bit No (MSB first) of save +0xadd0; ID 223000 + c * 64 + No.
# Left out: the Muspelheim reward chests (pa11, the Muspelheim checks cover them) and the content-less type 4-8 props.
CHEST_BASE = 223000
STORY_CHESTS = {2: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 26], 3: [0, 2, 20, 21, 22],
                5: [0, 2, 5, 10, 17], 6: [1, 2, 3, 4, 7, 10, 15, 17, 20], 9: [0, 1, 3, 4, 5, 6, 8, 11, 14, 22],
                10: [7, 10], 11: [0, 1, 2, 3, 4, 5, 7, 9], 14: [0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 14],
                15: [0, 1, 2, 4, 5, 6], 16: [0, 6], 17: [0]}
for c, nos in STORY_CHESTS.items():
    for k, no in enumerate(nos, 1):
        location_table["%s - Chest %d" % (CHAPTER_NAMES[c - 1], k)] = LocationData(CHEST_BASE + c * 64 + no, c - 1,
                                                                                  "Chests")

_m = 0
for phase, verses in CHAPTER20:
    if phase in TRIALS:
        title = "Witch Trial %s" % ["I", "II", "III", "IV", "V"][TRIALS.index(phase)]
        region, group = TRIAL_REGION, "Witch Trials"
    else:
        _m += 1
        title, region, group = "Tag Climax %d" % _m, MUSPEL_REGION, "Tag Climax"
    for k, v in enumerate(verses, 1):
        name = title if len(verses) == 1 else "%s - Part %d" % (title, k)
        location_table[name] = LocationData(TRIAL_BASE + v, region, group)
        for t, tier in enumerate(RANK_TIERS):                 # their medals too (chapter 20 in the rank ID)
            location_table["%s Rank (%s)" % (name, tier)] = LocationData(RANK_BASE + t * 1000 + 20 * 32 + v, region,
                                                                         "Verse Ranks")

# Umbran Crows: bit UniqueId (MSB first) of save +0xa824; the stage's SetCrow table gives its ChapterData chapter.
CROW_BASE = 224000
CROWS = {0: 2, 1: 2, 2: 2, 3: 3, 4: 5, 5: 6, 6: 6, 7: 6, 8: 8, 9: 9, 10: 9, 11: 10, 12: 10, 13: 11, 14: 11, 15: 14,
         16: 14, 17: 14, 18: 16, 19: 17}
_per = {}
for uid, c in CROWS.items():
    _per[c] = _per.get(c, 0) + 1
    location_table["%s - Umbran Crow %d" % (CHAPTER_NAMES[c - 1], _per[c])] = LocationData(CROW_BASE + uid, c - 1, "Crows")

# Books (the floating books in the stages, the game's SetBook tables): reading one sets bit Type (MSB first) of save
# +0xa820; ID 224100 + Type, by its stage's chapter (ChapterData). Reading one also shows an Archipelago hint.
BOOK_BASE = 224100
BOOKS = {0: 2, 2: 2, 5: 2, 7: 2, 8: 2, 9: 2, 10: 2, 11: 3, 12: 3, 13: 5, 14: 6, 15: 6, 16: 6, 17: 6, 18: 8, 19: 9,
         20: 9, 21: 9, 22: 9, 23: 11, 24: 11, 25: 14, 26: 14, 27: 15, 28: 17}
_per = {}
for t, c in BOOKS.items():
    _per[c] = _per.get(c, 0) + 1
    location_table["%s - Book %d" % (CHAPTER_NAMES[c - 1], _per[c])] = LocationData(BOOK_BASE + t, c - 1, "Books")

# Umbran Tears of Blood (the Bewitchments): bit k (MSB first) of save +0x1b0 (set by 0x7100d9045c). Only the ones a normal run can get;
# value = the shown chapters that must be complete first.
TEAR_BASE, TEARS_REGION = 224200, -4
TEARS = {0: ("After the End Comes...", []), 1: ("Prologue Complete", [0]), 2: ("Chapters I-IV Complete", [1, 2, 3, 4]),
         3: ("Chapters V-VI Complete", [5, 6]), 4: ("Chapter VII Complete", [7]),
         5: ("Chapters VIII-IX Complete", [8, 9]), 6: ("Chapters X-XII Complete", [10, 11, 12]),
         7: ("Chapters XIII-XV Complete", [13, 14, 15]), 8: ("Chapter XVI Complete", [16]),
         11: ("Prepared for Anything", []), 12: ("War on All Planes", [1]), 19: ("Cruel Witchcraft", []),
         20: ("Relentless Witchcraft", []), 21: ("Destroyer of the Light", []), 22: ("Survivor of Darkness", []),
         28: ("Bad Boy!", [])}
for k, (title, _) in TEARS.items():
    location_table["Tear of Blood: %s" % title] = LocationData(TEAR_BASE + k, TEARS_REGION, "Tears of Blood")

# Gates of Hell purchases. Accessory k (ITM_ACCESSORY_NAME_k, 1..14) is bit k + 1 of save +0xa478 (0x7100d96d44 sets it when bought); a
# technique is bit TEC_NAME_SP_nn of +0xa4d8. The five techniques a new save already has are no locations.
SHOP_BASE, SHOP_REGION = 225000, -5
TECHNIQUE_NO = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 16, 17, 12, 19, 20]   # Items.py TECHNIQUES -> TEC_NAME_SP_nn
DEFAULT_TECHNIQUES = {0, 1, 6, 13, 14}                                             # bits of a new save (0xc2060000)
for k, name in enumerate(ACCESSORIES, 1):
    location_table["Gates of Hell: %s" % name] = LocationData(SHOP_BASE + 100 + k, SHOP_REGION, "Gates of Hell")
for name, no in zip(TECHNIQUES, TECHNIQUE_NO):
    if no not in DEFAULT_TECHNIQUES:
        location_table["Gates of Hell: %s" % name] = LocationData(SHOP_BASE + 200 + no, SHOP_REGION, "Gates of Hell")

location_name_groups = {}
for n, d in location_table.items():
    location_name_groups.setdefault(d.group, set()).add(n)
