# Bayonetta 2 Archipelago Randomizer

An Archipelago (https://archipelago.gg) randomizer for **Bayonetta 2 (Switch v1.2)**, running natively on Windows
through **[B2recomp](B2recomp/README.md)**, the static recompilation of the game. Location checks and items are sent
and received in real time while you play, with direct-IP **Story Co-op** for two players.

Everything happens inside the game: you connect from the title menu, and no separate client window is needed.

> **This repository contains no game code and no game assets.** B2recomp translates the game on your PC from your own
> dump, and the Archipelago mod's menu text and logo are built from your own game files. Please don't share anything
> built from your game files.

**NOTE: THIS IS IN TESTING. NO FULL PLAYTHROUGH HAS BEEN DONE YET. EXPECT BUGS, AND BACK UP YOUR SAVES!**

---

## Features

### Native Title Screen Integration
- An **ARCHIPELAGO** entry on the title menu wheel, in the game's own font. It opens ADDRESS / SLOT / PASSWORD /
  CONNECT on the same wheel, with on-screen text editing.
- A **CO-OP** entry for hosting or joining a Story Co-op game.
- A custom title logo with the Archipelago logo.
- Connection settings are saved in `config/archipelago.ini`, so you only type them once.

### Story Co-op (Experimental)
- **Direct IP:** One player hosts, the other connects to their address (UDP port 27870). Lobbies can be public or
  private, with an optional password.
- **Partner in your game:** Each player's Bayonetta appears in the other's game, driven by their real inputs, with
  her position kept in step.
- **Shared enemies:** Enemy waves and the enemies themselves are paired between both games.
- **Verse sync:** A verse starts only when both players have reached it. The banner reads "Waiting for your partner
  (1/2)".
- **Skip voting:** A cinematic is skipped only when both players choose Skip.
- **Falling together:** If either player dies, both go to Continue and restart from the checkpoint.
- **Leaving:** If a player disconnects, their Bayonetta is removed and the other keeps playing.

### Location Checks
- Story chapters (Prologue + Chapters I–XVI)
- All verses, Muspelheims included
- Treasure chests
- All 20 Umbran Crows
- The 25 books lying in the chapters (reading one also shows an Archipelago hint)
- Umbran Tears of Blood (the ones a normal run can get, opt-in)
- Rodin's Treasures: costumes and mirrors (opt-in)
- Tag Climax stages and the Witch Trials (opt-in)
- Optional Verse Rank bonus locations (medal-based, with a **maximum medal** so nobody gets BK'd waiting on a Pure
  Platinum)
- Weapons (Golden LP turn-ins at the Gates of Hell)
- Techniques and Accessories (Gates of Hell purchases)
- Consumables, recipe ingredients and Halos in the item pool

### Goal Options (set in YAML)
- **All Chapters:** Complete the Prologue and Chapters I–XVI
- **Chapter Count:** Complete a set number of chapters (the final chapter can be one of them)
- **Boss Rush:** Complete only the boss chapters, plus the final chapter
- **Chapter Count (No Final Chapter):** Complete X chapters, the final chapter not required
- **Eyes of the World Hunt:** Collect enough Eye of the World Fragments from the item pool

Chapter Blocking (chapters open with their Chapter Unlock item) and the starting chapter are options too.

### Traps (14)
Junk checks can bite back. The full trap pool:

- **Pickpocket:** Steals up to 10,000 Halos.
- **Bloodletting:** Halves your HP.
- **Fragile Witch:** Sets you to 1 HP. Don't get hit.
- **Magic Drain:** Empties your magic gauge.
- **Amnesia:** Only your starting techniques work for 60 seconds.
- **Sticky Fingers:** Swipes up to 3 of one item.
- **Squish:** No jumping or dodging for 20 seconds.
- **Squash:** No punching or kicking for 15 seconds.
- **Alfheim Curse:** Three hits within 30 seconds leave you at 1 HP.
- **Berserk:** Every hit counts double for 30 seconds.
- **Angel Ambush:** Three Affinities spawn beside you.
- **Grace & Glory:** The pair joins your fight.
- **Gracious & Glorious:** The upgraded pair joins the fight.
- **Fairness & Fearless:** Another pair of enemies appears beside you.

A `trap_percentage` YAML option controls how much of the filler pool becomes traps, and `trap_weights` sets how
often each one appears.

### Multiplayer Link Protocols
- **Death Link:** Shared death. When they die, you die. When you die, they die.
- **Damage Link:** Shared pain. Part of the damage you take is sent to the other players. Never lethal on its own.
- **Trap Link:** Shared traps. Your traps fire on others, theirs fire on you.
- **Ring Link:** Shared Halos. Halos you collect become rings for others (1 ring = 50 Halos). Shop purchases excluded.

### Client Features
- **In-Game Chat Box:** Items, checks, hints and chat appear in a chat box in the game, in place of the item popups,
  colored like the Archipelago website.
- **Top Banner:** Co-op status ("Waiting for your partner", "Skip 1/2", "Your partner fell") at the top of the screen.
- **Cheats Pages (Options → Display):** God Mode, Infinite Halos, Infinite Magic, Damage Taken and Halo Gain
  multipliers, Auto QTEs, and Easy Cutscene Skip.
- **Chapter Blocking:** Chapter Select shows only the chapters AP has given you (the Prologue, your starting chapter
  and every Chapter Unlock), so you can jump straight to any of them. If the story carries you into a locked chapter,
  its checks are held and sent once its Chapter Unlock arrives (or sent right away with `send_locked_chapters=1` in
  `config/archipelago.ini`).
- **Book Hints:** Reading a book in a chapter shows an Archipelago hint.

---

## Roadmap

### Done
- AP client core inside the game (connect, slot auth, items, checks, chat, hints)
- ARCHIPELAGO title menu entry, connect menu and custom title logo
- Chapter, verse, chest, crow, book, Tears of Blood, Rodin's Treasure and Tag Climax locations
- Verse Rank (medal) bonus locations with a maximum medal
- Weapons, techniques, accessories, consumables and Halos as items
- Chapter unlocks as items, with chapter blocking
- All five goal options, including the Eyes of the World hunt
- Death Link, Damage Link, Trap Link, Ring Link
- 14 trap items, four of which spawn enemies
- In-game chat box
- Cheats pages in the game's Options menu
- Story Co-op: lobby menu, partner character, enemy sync, verse sync, skip voting, falling together, partner removal

### In Progress
- A full playthrough to find what breaks in long runs
- Confirming Auto QTEs on a real QTE prompt
- Testing co-op on two separate PCs

### To Do
- Apply the partner's chosen outfit in co-op (the CHARACTER setting is in the menu, but outfits aren't applied yet)
- Co-op players at different checkpoints
- Release package (prebuilt tools, the mod and the `.apworld`)

### Not Planned / Stretch
- Internet co-op without port forwarding (would need a relay server)

---

## Requirements

- **Your own dump of Bayonetta 2 (Switch), version 1.2**, as plain files (`exefs` and `romfs`). See
  [B2recomp/BUILDING.md](B2recomp/BUILDING.md).
- **Windows 10/11 x64** and a GPU with a **Vulkan 1.3** driver.
- **Python 3.10+** and the build tools listed in [B2recomp/BUILDING.md](B2recomp/BUILDING.md).
- An **Archipelago server** hosting a multiworld session (Archipelago 0.6.x).

---

## Installation

1. **Build B2recomp** from your dump, following [B2recomp/README.md](B2recomp/README.md) and
   [B2recomp/BUILDING.md](B2recomp/BUILDING.md). The Archipelago patch (`B2recomp/scripts/ap_patch.py`) runs as part of
   that build.
2. **Build the Archipelago mod** from your game files (menu text, font and logo):
   ```
   python ap/tools/build_mod.py B2recomp B2recomp/mods/Archipelago
   ```
   The game loads it from `B2recomp/mods/` the next time it starts.
3. **Make sure to back up your saves before playing!**

## APWorld Setup

1. Pack the apworld:
   ```
   python ap/tools/pack_apworld.py .
   ```
2. Put `bayonetta2.apworld` in your Archipelago `custom_worlds` folder.
3. Use **Generate Template Options** in the Archipelago Launcher to get a YAML, edit it, and put it in `Players`.
4. Generate your multiworld as usual. The Bayonetta 2 world will now be available.

**After updating, regenerate your seed.** Location IDs can change between versions.

## Playing

1. Start the game.
2. Select **ARCHIPELAGO** on the title menu, set **ADDRESS**, **SLOT** and **PASSWORD**, then select **CONNECT**.
3. Select **STORY** and play! Checks fire automatically as you clear verses and chapters, open chests, find crows and
   books, and buy at the Gates of Hell.
4. Items and messages appear in the chat box.

### Co-op Setup
1. Both players select **CO-OP** on the title menu.
2. Player 1 selects **HOST**. Set a **PASSWORD** first if you want one.
3. Player 2 enters Player 1's IP address in **ADDRESS** and selects **CONNECT**. On the same network, leaving ADDRESS
   empty joins the first public lobby found.
4. Both players start the same chapter. Once you're both in the stage, you'll see each other in real time!

Playing over the internet needs the host to forward **UDP port 27870** on their router.

**NOTE: CO-OP IS EXPERIMENTAL AND HAS ONLY BEEN TESTED ON ONE PC SO FAR. EXPECT BUGS!**

---

## Troubleshooting

- **The ARCHIPELAGO menu entries are blank:** The Archipelago mod isn't installed. Build it (Installation, step 2) into
  `B2recomp/mods/Archipelago`.
- **Can't connect to a co-op host over the internet:** The host must forward UDP port 27870 (or the port set after
  `:` in ADDRESS) to their PC.
- **Locations don't match after an update:** Your seed was generated with a different `.apworld` version. Regenerate
  it with the latest one.
- **Game problems unrelated to Archipelago:** See [B2recomp/README.md](B2recomp/README.md) and
  [B2recomp/BUILDING.md](B2recomp/BUILDING.md).
- **Something else, or an idea for the project?** Open an Issue and let me know!

---

## Credits

- Built on [B2recomp](B2recomp/README.md) by K0NL, itself built on
  [ssbu-recomp](https://github.com/Denkishi/ssbu-recomp) by Denkishi.
- Option set and features follow my [Bayonetta 1 Archipelago](https://github.com/Boi-027/BayonettaArchipelago) world.
- Project direction and in-game testing by **Boi**.
- [Archipelago](https://github.com/ArchipelagoMW/Archipelago).

---

## License

- **`B2recomp/`** keeps its own licenses: GPL-3.0-or-later, see [B2recomp/LICENSE](B2recomp/LICENSE) and
  [B2recomp/NOTICE.md](B2recomp/NOTICE.md). That includes the Archipelago and co-op code in `B2recomp/runtime/`.
- **The rest of the repository** (`ap/`) is under the MIT license in [LICENSE](LICENSE).

Bayonetta 2 is a trademark of its owners. This is an unofficial fan project, not affiliated with Nintendo or
PlatinumGames.

---

## AI Usage Disclosure

- This project was made with heavy use of AI.
- Most of the code (the Archipelago client, the Story Co-op code, the game patches and the apworld), the reverse
  engineering of the game (`ap/NOTES.md`) and this documentation were written by Claude (Anthropic) through Claude
  Code.
- I directed the work: I chose the features, made the design decisions, and tested the results in the game.
- B2recomp describes its own AI use in [its README](B2recomp/README.md).
