"""Bayonetta 2 items. Names are the game's own (its English message files, ap/research/messages_us.txt).

IDs: Bayonetta 2 uses the 220000 block (Bayonetta 1's world uses 50000). Within it:
  220001.. weapons        220100.. accessories    220150.. techniques    220200.. Golden LPs
  220300.. consumables    220400.. Witch Heart / Moon Pearl pieces       220450.. halos
  220500.. chapter unlocks (option)                220600.. traps          220700.. MacGuffin
"""
from typing import NamedTuple

from BaseClasses import ItemClassification as IC


class ItemData(NamedTuple):
    id: int
    classification: IC
    group: str


# Weapons: one item per weapon (both the hands and feet versions, as the game gives them together).
# The Nintendo weapons (Chain Chomp, Arwing, Super Mario 64 LP) and Angel Slayer rewards stay out of the pool.
WEAPONS = ["Love Is Blue", "Kafka", "Rakshasa", "Takemikazuchi", "Chernobog", "Alruna", "Salamandra", "Undine",
           "Scarborough Fair", "Shuraba", "Rodin", "Unforgiven", "Mistelteinn"]
ACCESSORIES = ["Infernal Communicator", "Pulley's Butterfly", "Selene's Light", "Star of Dinéta", "Evil Harvest Rosary",
               "Gaze of Despair", "Moon of Mahaa-Kalaa", "Eternal Testimony", "Bracelet of Time", "Climax Brace",
               "Immortal Marionette", "Earrings of Ruin", "Climax Brace 2", "Mallet of Rewards"]
TECHNIQUES = ["Stiletto", "Heel Slide", "Tetsuzanko", "Heel Stomp", "Breakdance", "Witch Strike", "After Burner Kick",
              "Witch Twist", "Heel Tornado", "Charge Bullet", "Bat Within", "Crow Within", "Moth Within", "Owl Within",
              "Hornet Within", "Falcon Within", "Umbran Spear", "Dodge Offset", "Air Dodge"]
GOLDEN_LPS = ["Golden LP: Entrance of the Gladiators", "Golden LP: William Tell Overture", "Golden LP: Der Hölle Rache",
              "Golden LP: Matthäus-Passion", "Golden LP: The Harmonious Blacksmith", "Golden LP: Erlkönig"]
CONSUMABLES = ["Green Herb Lollipop", "Purple Magic Lollipop", "Bloody Rose Lollipop", "Yellow Moon Lollipop",
               "Mega Green Herb Lollipop", "Mega Purple Magic Lollipop", "Mega Bloody Rose Lollipop",
               "Mega Yellow Moon Lollipop", "Red Hot Shot", "Mandragora Root", "Unicorn Horn", "Baked Gecko"]
PIECES = ["Broken Witch Heart", "Broken Moon Pearl"]
HALOS = {"137 Halos": 137, "500 Halos": 500, "1000 Halos": 1000, "2500 Halos": 2500, "5000 Halos": 5000,
         "10000 Halos": 10000, "20000 Halos": 20000, "50000 Halos": 50000, "100000 Halos": 100000}
# a trap's ID is 220600 + its place here (the game's client numbers them the same); None = dropped (Nemesis), ID unused
TRAP_SLOTS = ["Pickpocket", "Bloodletting", "Fragile Witch", "Magic Drain", "Amnesia", "Sticky Fingers", "Squish",
              "Squash", "Angel Ambush", "Grace & Glory", None, "Alfheim Curse", "Berserk", "Gracious & Glorious",
              "Fairness & Fearless"]
TRAPS = [t for t in TRAP_SLOTS if t]
# The chapters as the game shows them. The game's own table (ChapterData) splits the Prologue in two (0: p110,
# 1: p120-p140) and has no Epilogue entry, so ChapterData chapter k >= 2 is Chapter k - 1 (p212 Noatun = Chapter I).
CHAPTER_NAMES = ["Prologue"] + ["Chapter %s" % r for r in
                                ("I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII", "XIII", "XIV",
                                 "XV", "XVI")]
DATA_CHAPTERS = [[0, 1]] + [[d + 1] for d in range(1, len(CHAPTER_NAMES))]   # shown chapter -> ChapterData chapters
CHAPTER_UNLOCKS = ["Chapter Unlock: %s" % c for c in CHAPTER_NAMES[1:]]
MACGUFFIN = "Eye of the World Fragment"

item_table: dict = {}


def _add(names, base, cls, group):
    for i, n in enumerate(names):
        item_table[n] = ItemData(base + i, cls, group)


_add(WEAPONS, 220001, IC.progression, "Weapons")
_add(ACCESSORIES, 220100, IC.useful, "Accessories")
_add(TECHNIQUES, 220150, IC.useful, "Techniques")
item_table["Crow Within"] = item_table["Crow Within"]._replace(classification=IC.progression)   # flight: __init__ set_rules
_add(GOLDEN_LPS, 220200, IC.useful, "Golden LPs")
_add(CONSUMABLES, 220300, IC.filler, "Consumables")
_add(PIECES, 220400, IC.useful, "Pieces")
_add(list(HALOS), 220450, IC.filler, "Halos")
_add(CHAPTER_UNLOCKS, 220500, IC.progression, "Chapter Unlocks")
for _k, _t in enumerate(TRAP_SLOTS):
    if _t:
        item_table[_t] = ItemData(220600 + _k, IC.trap, "Traps")
item_table[MACGUFFIN] = ItemData(220700, IC.progression_skip_balancing, "MacGuffin")

item_name_groups = {}
for name, data in item_table.items():
    item_name_groups.setdefault(data.group, set()).add(name)
