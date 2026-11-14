# Bayonetta 2 Archipelago

An [Archipelago](https://archipelago.gg) multiworld randomizer for **Bayonetta 2** (Nintendo Switch, v1.2), running on
[B2recomp](B2recomp/README.md), the static recompilation of the game for Windows.

Chapters, verses, chests, crows and other pickups become checks. Weapons, techniques, accessories and items come from
the multiworld instead. Everything happens inside the game: you connect from the main menu, and items arrive while you
play. You don't need a separate client window.

> **This repository contains no game code and no game assets.** B2recomp translates the game on your own PC, from a copy
> you dumped yourself from your own console. The Archipelago mod's menu text and logo are also built from your own game
> files. Please don't share anything built from your game files, and don't ask for it.

## Status

**In testing.** Each feature below works in short tests, but no full playthrough has been done yet. Expect bugs, and
play on a **copy** of your save.

Not done yet:
- A full playthrough from start to finish
- Auto QTEs have not been confirmed to work
- Story co-op has only been tested with two copies of the game on one PC, never on two separate PCs
- No release package yet (you build everything yourself, below)

## Features

**Locations**, each one an option:
- Story chapters, and their verses (Muspelheims count as verses). With `verse_rank_target`, the verse medals too
- Treasure chests, the 20 Umbran Crows and the 25 books (reading a book also shows an Archipelago hint)
- Umbran Tears of Blood, Rodin's Treasures, and Tag Climax stages with the Witch Trials

**Items:** weapons, techniques, accessories, consumables (lollipops, Red Hot Shots, ingredients) and halos.

**Goals:**
- All chapters
- A number of chapters, with or without the epilogue
- Boss rush
- An Eyes of the World fragment hunt

You can also set the starting chapter and chapter blocking.

**Links:** Death Link, Damage Link, Trap Link and Ring Link.

**15 traps**, five of which spawn enemies. Their weights and the share of filler that becomes traps are both options.

**In-game client:**
- An **ARCHIPELAGO** entry on the main menu for the address, slot and password
- A chat box for items and messages
- Cheats pages in Options: god mode, infinite halos, infinite magic, auto QTEs and Easy Cutscene Skip

**Story co-op (direct IP).** Two players play the story together, each in their own game, and each player's Bayonetta
appears in the other's game:
- Enemies are shared
- A verse starts only once both players are there
- A cinematic is skipped only when both choose Skip
- If one player falls, both go back to the checkpoint
- If a player leaves, their Bayonetta is removed

Co-op uses UDP port 27870.

## Setup

1. **Build B2recomp** from your own dump of Bayonetta 2 v1.2, following [B2recomp/README.md](B2recomp/README.md) and
   [B2recomp/BUILDING.md](B2recomp/BUILDING.md). The Archipelago patch (`B2recomp/scripts/ap_patch.py`) runs as part
   of that build.
2. **Build the Archipelago mod** from your game files (menu text, font and logo):
   ```
   python ap/tools/build_mod.py B2recomp B2recomp/mods/Archipelago
   ```
   The game loads it from `B2recomp/mods/` the next time it starts. Without it, the ARCHIPELAGO menu entries are blank.
3. **Install the apworld.** Pack it, then put `bayonetta2.apworld` in your Archipelago install's `custom_worlds` folder:
   ```
   python ap/tools/pack_apworld.py .
   ```
   To get a YAML to edit, use **Generate Template Options** in the Archipelago Launcher. Then generate and host as
   usual.
4. **Connect.** Start the game and choose **ARCHIPELAGO** on the main menu. Set **ADDRESS**, **SLOT** and
   **PASSWORD**, then choose **CONNECT**. Then choose **STORY** and play.

## Repository layout

| Path | What it is |
|---|---|
| `ap/apworld/bayonetta2/` | The apworld: items, locations, options and docs |
| `ap/tools/` | Builds the RomFS mod (`build_mod.py`) and packs the apworld (`pack_apworld.py`). `dev/` holds the test tools |
| `ap/NOTES.md` | Research notes: game addresses and how each feature works |
| `B2recomp/` | The recompilation |
| `B2recomp/runtime/` | The Archipelago client (`ap_*.c`) and story co-op (`coop_*.c`) |
| `B2recomp/scripts/ap_patch.py` | Hooks the game's code to the runtime after it is translated |

## AI usage

This project was made with heavy use of AI. Most of the Archipelago and co-op code, the apworld, the research into the
game's internals (`ap/NOTES.md`) and this documentation were written by Claude, Anthropic's AI model, working through
Claude Code. Boi directed the work: they chose the features, made the design calls, and tested the results in the
game.

B2recomp describes its own AI use in [its README](B2recomp/README.md).

## License

- **`B2recomp/`** keeps its own licenses: GPL-3.0-or-later, see [B2recomp/LICENSE](B2recomp/LICENSE) and
  [B2recomp/NOTICE.md](B2recomp/NOTICE.md). That includes the Archipelago and co-op code in `B2recomp/runtime/`.
- **The rest of the repository** (`ap/`) is under the MIT license in [LICENSE](LICENSE).

Bayonetta 2 is a trademark of its owners. This is an unofficial fan project, not affiliated with Nintendo or PlatinumGames.

## Credits

- [B2recomp](B2recomp/README.md) by K0NL, built on [ssbu-recomp](https://github.com/Denkishi/ssbu-recomp) by Denkishi
- [Archipelago](https://github.com/ArchipelagoMW/Archipelago)
- The option set follows the Bayonetta 1 Archipelago world
