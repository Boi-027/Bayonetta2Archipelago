"""Bayonetta 2 (Archipelago world), played through B2recomp with the Archipelago client built into the game.

Items, locations and options: Items.py, Locations.py, Options.py. The game's client computes location IDs the same
way (runtime/ap_game.c). IDs: the 220000 block (Bayonetta 1's world uses 50000).
"""
from BaseClasses import Item, ItemClassification as IC, Location, Region, Tutorial
from worlds.AutoWorld import WebWorld, World

from .Items import (CHAPTER_NAMES, CHAPTER_UNLOCKS, CONSUMABLES, HALOS, MACGUFFIN, PIECES, TECHNIQUES, TRAPS,
                    item_name_groups,
                    item_table)
from .Locations import (BOSS_CHAPTERS, MUSPEL_REGION, RANK_BASE, SHOP_BASE, SHOP_REGION, TEARS,
                        TEARS_REGION, TECHNIQUE_LOCATIONS, TRIAL_REGION,
                        location_name_groups, location_table)
from .Options import Bayonetta2Options, Goal

GAME = "Bayonetta 2"


class Bayonetta2Item(Item):
    game = GAME


class Bayonetta2Location(Location):
    game = GAME


class Bayonetta2Web(WebWorld):
    tutorials = [Tutorial("Multiworld Setup Guide", "Playing Bayonetta 2 in a multiworld through B2recomp.", "English",
                          "setup_en.md", "setup/en", ["Boi"])]


class Bayonetta2World(World):
    """Bayonetta 2: every verse, chapter, chest, Muspelheim, file, tear and shop purchase is a check."""
    game = GAME
    web = Bayonetta2Web()
    options_dataclass = Bayonetta2Options
    options: Bayonetta2Options
    item_name_to_id = {n: d.id for n, d in item_table.items()}
    location_name_to_id = {n: d.id for n, d in location_table.items()}
    item_name_groups = item_name_groups
    location_name_groups = location_name_groups

    def create_item(self, name: str) -> Item:
        return Bayonetta2Item(name, item_table[name].classification, item_table[name].id, self.player)

    # ------------------------------------------------------------------ regions and rules
    def _rank_wanted(self, loc_id: int) -> bool:
        """verse_rank_target: any_medal -> tier 0 only; all_medals -> tiers 1 .. verse_rank_max_medal."""
        tier, mode = (loc_id - RANK_BASE) // 1000, self.options.verse_rank_target.value
        return (mode == 1 and tier == 0) or (mode == 2 and 1 <= tier <= self.options.verse_rank_max_medal.value)

    def create_regions(self) -> None:
        menu = Region("Menu", self.player, self.multiworld)
        self.multiworld.regions.append(menu)
        start = self.options.starting_chapter.value
        for c, name in enumerate(CHAPTER_NAMES):
            region = Region(name, self.player, self.multiworld)
            for loc, data in location_table.items():
                if data.chapter == c and (data.group != "Chests" or self.options.include_chests) \
                        and (data.group != "Crows" or self.options.include_crows) \
                        and (data.group != "Books" or self.options.include_files) \
                        and (data.group != "Verse Ranks" or self._rank_wanted(data.id)):
                    region.locations.append(Bayonetta2Location(self.player, loc, data.id, region))
            self.multiworld.regions.append(region)
            unlock = "Chapter Unlock: %s" % name
            if self.options.chapter_blocking and unlock in item_table and c != start:
                menu.connect(region, None, lambda state, u=unlock: state.has(u, self.player))
            else:
                menu.connect(region)
        if self.options.include_tag_climax:                     # Tag Climax stages and Witch Trials (chapter 20 verses)
            for title, chapter in (("Tag Climax", MUSPEL_REGION), ("Witch Trials", TRIAL_REGION)):
                region = Region(title, self.player, self.multiworld)
                for loc, data in location_table.items():
                    if data.chapter == chapter and (data.group != "Verse Ranks" or self._rank_wanted(data.id)):
                        region.locations.append(Bayonetta2Location(self.player, loc, data.id, region))
                self.multiworld.regions.append(region)
                menu.connect(region)
        o = self.options
        for title, chapter, on in (("Tears of Blood", TEARS_REGION, o.include_tears),
                                   ("Gates of Hell", SHOP_REGION, o.include_accessories or o.include_techniques)):
            if not on:
                continue
            region = Region(title, self.player, self.multiworld)
            for loc, data in location_table.items():
                if data.chapter == chapter and (chapter != SHOP_REGION or (o.include_accessories if data.id < SHOP_BASE + 200
                                                                            else o.include_techniques)):
                    region.locations.append(Bayonetta2Location(self.player, loc, data.id, region))
            self.multiworld.regions.append(region)
            menu.connect(region)
        goal = Bayonetta2Location(self.player, "Goal", None, menu)
        goal.place_locked_item(Bayonetta2Item("Victory", IC.progression, None, self.player))
        menu.locations.append(goal)

    def _goal_rule(self):
        o, p = self.options, self.player
        complete = ["%s - Complete" % n for n in CHAPTER_NAMES]
        if o.goal == Goal.option_boss_rush:
            needed = ["%s - Complete" % CHAPTER_NAMES[c] for c in BOSS_CHAPTERS] + [complete[-1]]
            return lambda s: all(s.can_reach_location(n, p) for n in needed)
        if o.goal in (Goal.option_chapter_count, Goal.option_chapter_count_no_epilogue):
            pool = complete if o.goal == Goal.option_chapter_count else complete[:-1]
            k = min(o.goal_chapter_count.value, len(pool))
            return lambda s: sum(s.can_reach_location(n, p) for n in pool) >= k
        if o.goal == Goal.option_eyes_of_the_world_hunt:
            need = o.fragments_required.value
            epi = bool(o.fragments_hunt_requires_epilogue)
            return lambda s: s.has(MACGUFFIN, p, need) and (not epi or s.can_reach_location(complete[-1], p))
        return lambda s: all(s.can_reach_location(n, p) for n in complete)

    def set_rules(self) -> None:
        if self.options.include_tears:                         # a chapter Bewitchment needs those chapters complete
            for k, (title, shown) in TEARS.items():
                need = ["%s - Complete" % CHAPTER_NAMES[d] for d in shown]
                self.multiworld.get_location("Tear of Blood: %s" % title, self.player).access_rule =                     lambda s, n=need: all(s.can_reach_location(x, self.player) for x in n)
        self.multiworld.get_location("Goal", self.player).access_rule = self._goal_rule()
        self.multiworld.completion_condition[self.player] = lambda s: s.has("Victory", self.player)

    # ------------------------------------------------------------------ items
    def create_items(self) -> None:
        o = self.options
        pool = []
        # Love Is Blue is the starting weapon, Unforgiven is not Bayonetta's; Golden LPs are left out because AP gives
        # the weapons themselves (runtime/ap_game.c weapons_frame)
        skip = {"Love Is Blue", "Unforgiven"}
        groups = {"Weapons": o.include_weapons, "Accessories": o.include_accessories, "Techniques": o.include_techniques}
        for group, on in groups.items():
            if on:
                pool += sorted(n for n in item_name_groups[group]      # techniques: the 15 Bayonetta's shop sells
                               if n not in skip and (group != "Techniques" or n in TECHNIQUE_LOCATIONS))
        if o.chapter_blocking:
            start = "Chapter Unlock: %s" % CHAPTER_NAMES[o.starting_chapter.value]
            pool += [u for u in CHAPTER_UNLOCKS if u != start]
        if o.goal == Goal.option_eyes_of_the_world_hunt:
            pool += [MACGUFFIN] * max(o.fragments_total.value, o.fragments_required.value)
        pool += [PIECES[0]] * 12 + [PIECES[1]] * 12
        places = len([l for l in self.multiworld.get_locations(self.player) if l.address is not None])
        slots = places - len(pool)
        if slots < 0:                                      # more items than places: drop the least useful ones
            pool = pool[:places]
            slots = 0
        traps = slots * o.trap_percentage.value // 100
        weights = {t: max(0, int(o.trap_weights.value.get(t, 0))) for t in TRAPS}
        names = [t for t in TRAPS if weights[t]] or TRAPS
        pool += self.random.choices(names, [weights[t] or 1 for t in names], k=traps)
        filler = ([n for n in CONSUMABLES] if o.include_consumables else []) + (list(HALOS) if o.include_halos else [])
        pool += [self.random.choice(filler or list(HALOS)) for _ in range(slots - traps)]
        self.multiworld.itempool += [self.create_item(n) for n in pool]

    def get_filler_item_name(self) -> str:
        return self.random.choice(list(HALOS))

    def fill_slot_data(self) -> dict:
        o = self.options
        return {"world_version": "0.2.0", "goal": o.goal.value, "goal_chapter_count": o.goal_chapter_count.value,
                "fragments_required": o.fragments_required.value, "chapter_blocking": bool(o.chapter_blocking),
                "starting_chapter": o.starting_chapter.value, "death_link": bool(o.death_link),
                "damage_link": bool(o.damage_link), "trap_link": bool(o.trap_link), "ring_link": bool(o.ring_link),
                "verse_rank_target": o.verse_rank_target.value, "verse_rank_max_medal": o.verse_rank_max_medal.value,
                "fragments_hunt_requires_final": bool(o.fragments_hunt_requires_epilogue)}
