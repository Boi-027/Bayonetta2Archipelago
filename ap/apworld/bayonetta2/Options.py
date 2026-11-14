"""Bayonetta 2 options: the Bayonetta 1 world's set, with Bayonetta 2's chapters and collectibles."""
from dataclasses import dataclass

from Options import Choice, DefaultOnToggle, OptionDict, PerGameCommonOptions, Range, Toggle


class Goal(Choice):
    """What completes the game.
    all_chapters: the Prologue and Chapters I-XVI.
    chapter_count: a number of chapters (goal_chapter_count; the final chapter, XVI, may be one of them).
    boss_rush: the boss chapters only, plus the final chapter (XVI).
    chapter_count_no_epilogue: a number of chapters, the final chapter (XVI) not needed.
    eyes_of_the_world_hunt: collect Eye of the World Fragments from the item pool."""
    display_name = "Goal"
    default = 0
    option_all_chapters = 0
    option_chapter_count = 1
    option_boss_rush = 2
    option_chapter_count_no_epilogue = 3
    option_eyes_of_the_world_hunt = 4


class GoalChapterCount(Range):
    """Chapters to complete for the chapter_count goals."""
    display_name = "Goal Chapter Count"
    range_start = 1
    range_end = 17
    default = 9


class FragmentsRequired(Range):
    """Eye of the World Fragments needed for the eyes_of_the_world_hunt goal."""
    display_name = "Fragments Required"
    range_start = 1
    range_end = 100
    default = 10


class FragmentsTotal(Range):
    """Eye of the World Fragments in the pool (at least Fragments Required)."""
    display_name = "Fragments Total"
    range_start = 1
    range_end = 100
    default = 15


class FragmentsHuntRequiresEpilogue(DefaultOnToggle):
    """The hunt also needs the final chapter (XVI) cleared."""
    display_name = "Fragment Hunt Requires Final Chapter"


class VerseRankTarget(Choice):
    """Extra locations for the verses' medals (the overall medal on the result), story verses and, with
    include_tag_climax, Tag Climax and the Witch Trials.
    disabled: none. any_medal: one location per verse, for Bronze or better.
    all_medals: one location per verse and medal, Bronze+ up to verse_rank_max_medal."""
    display_name = "Verse Rank Target"
    default = 0
    option_disabled = 0
    option_any_medal = 1
    option_all_medals = 2


class VerseRankMaxMedal(Choice):
    """The best medal a verse location can ask for when verse_rank_target is all_medals (so nobody waits on a Pure
    Platinum run)."""
    display_name = "Verse Rank Max Medal"
    default = 3
    option_bronze = 1
    option_silver = 2
    option_gold = 3
    option_platinum = 4
    option_pure_platinum = 5


class StartingChapter(Range):
    """The chapter open from the start when chapters are locked (Chapter Blocking)."""
    display_name = "Starting Chapter"
    range_start = 1
    range_end = 16
    default = 1


class ChapterBlocking(DefaultOnToggle):
    """Chapters open only with their Chapter Unlock item."""
    display_name = "Chapter Blocking"


class DeathLink(Toggle):
    """When you die, everyone dies; when they die, you die."""
    display_name = "Death Link"


class DamageLink(Toggle):
    """Part of the damage you take goes to the other players (never lethal on its own)."""
    display_name = "Damage Link"


class TrapLink(Toggle):
    """Your traps fire on the other players; theirs on you."""
    display_name = "Trap Link"


class RingLink(Toggle):
    """Halos you collect become rings for the others (1 ring = 50 Halos). Shop purchases do not count."""
    display_name = "Ring Link"


class IncludeWeapons(DefaultOnToggle):
    """Weapons (Golden LP turn-ins at the Gates of Hell) are locations and items."""
    display_name = "Include Weapons"


class IncludeTechniques(DefaultOnToggle):
    """Techniques bought at the Gates of Hell are locations and items."""
    display_name = "Include Techniques"


class IncludeAccessories(DefaultOnToggle):
    """Accessories bought at the Gates of Hell are locations and items."""
    display_name = "Include Accessories"


class IncludeConsumables(DefaultOnToggle):
    """Lollipops, Red Hot Shots and the recipe ingredients are in the pool."""
    display_name = "Include Consumables"


class IncludeHalos(DefaultOnToggle):
    """Halo bundles are in the pool."""
    display_name = "Include Halos"


class IncludeTagClimax(Toggle):
    """Tag Climax's stages and the Witch Trials (the game's chapter 20) are locations, with their medals when
    verse_rank_target is on. The story's Muspelheims are always locations: each is a verse of its chapter."""
    display_name = "Include Tag Climax"


class IncludeChests(DefaultOnToggle):
    """The story chapters' treasure chests are locations (their own contents are not given)."""
    display_name = "Include Chests"


class IncludeCrows(DefaultOnToggle):
    """The 20 Umbran Crows are locations."""
    display_name = "Include Umbran Crows"


class IncludeFiles(DefaultOnToggle):
    """The books lying in the chapters (25) are locations; reading one also shows an Archipelago hint."""
    display_name = "Include Books"


class IncludeTears(Toggle):
    """Umbran Tears of Blood (the Bewitchments) a normal run can get are locations: the chapter ones, a first Gates of
    Hell purchase, a first Muspelheim, the Torture Attack / Umbran Climax kill counts, and touching Bayonetta."""
    display_name = "Include Umbran Tears of Blood"


class IncludeRodinsTreasures(Toggle):
    """Rodin's Treasures (costumes and mirrors) are locations."""
    display_name = "Include Rodin's Treasures"


class TrapPercentage(Range):
    """How much of the filler becomes traps."""
    display_name = "Trap Percentage"
    range_start = 0
    range_end = 100
    default = 0


class TrapWeights(OptionDict):
    """Relative weight of each trap (0 = never).

    Pickpocket: lose up to 10,000 Halos. Bloodletting: half your HP. Fragile Witch: 1 HP. Magic Drain: empty magic.
    Amnesia: only the starting techniques for 60 s. Sticky Fingers: lose up to 3 of one item.
    Squish: no jumping or dodging for 20 s. Squash: no punching or kicking for 15 s (default controls).
    Alfheim Curse: 3 hits within 30 s leave you at 1 HP. Berserk: every hit counts double for 30 s.
    Angel Ambush: three Affinities. Grace & Glory, Gracious & Glorious, Fairness & Fearless: a pair of enemies
    appears beside you."""
    display_name = "Trap Weights"
    default = {"Pickpocket": 10, "Bloodletting": 10, "Fragile Witch": 5, "Magic Drain": 10, "Amnesia": 10,
               "Sticky Fingers": 10, "Squish": 5, "Squash": 5, "Angel Ambush": 10, "Grace & Glory": 5,
               "Alfheim Curse": 5, "Berserk": 5, "Gracious & Glorious": 3, "Fairness & Fearless": 3}


@dataclass
class Bayonetta2Options(PerGameCommonOptions):
    goal: Goal
    goal_chapter_count: GoalChapterCount
    fragments_required: FragmentsRequired
    fragments_total: FragmentsTotal
    fragments_hunt_requires_epilogue: FragmentsHuntRequiresEpilogue
    verse_rank_target: VerseRankTarget
    verse_rank_max_medal: VerseRankMaxMedal
    starting_chapter: StartingChapter
    chapter_blocking: ChapterBlocking
    death_link: DeathLink
    damage_link: DamageLink
    trap_link: TrapLink
    ring_link: RingLink
    include_weapons: IncludeWeapons
    include_techniques: IncludeTechniques
    include_accessories: IncludeAccessories
    include_consumables: IncludeConsumables
    include_halos: IncludeHalos
    include_tag_climax: IncludeTagClimax
    include_chests: IncludeChests
    include_crows: IncludeCrows
    include_files: IncludeFiles
    include_tears: IncludeTears
    include_rodins_treasures: IncludeRodinsTreasures
    trap_percentage: TrapPercentage
    trap_weights: TrapWeights
