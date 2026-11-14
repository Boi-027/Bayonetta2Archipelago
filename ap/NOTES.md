# Bayonetta 2 Archipelago: research notes

Game: Switch v1.2 (`main` build id f19b2e01e1794f933a9720c1404bb6f6), run through B2recomp. Addresses are guest
addresses; **a guest pointer is a host pointer** in the recomp, so host code can pass C strings to game functions.

## Layout of this project

| Path | What |
|---|---|
| `B2recomp/scripts/ap_patch.py` | lift-step patch (run by `setup.py` after `prompt_fix.py`): main-menu hooks that hand the menu to the runtime |
| `B2recomp/runtime/ap_ui.c` | the connect menu (wheel labels, A / B, text editing, text slots, `config/archipelago.ini`) |
| `B2recomp/runtime/ap_net.c` | AP client: WebSocket (+ permessage-deflate), RoomInfo -> Connect -> Connected / ConnectionRefused |
| `B2recomp/runtime/ap_tls.c`, `ap_json.c`, `ap_inflate.c`, `ap.h` | SChannel TLS 1.2, JSON, raw inflate, shared declarations |
| recomp edits (marked `[Archipelago]`) | `nnhid.c` (`ap_hid_buttons`, keyboard not mapped to buttons while typing), `display.c` (WM_CHAR), `build_runtime.py` (sources), `setup.py` (runs ap_patch.py) |
| `ap/tools/mcd.py` | MCD message file reader / writer (round-trips the game's files byte for byte) |
| `ap/tools/bntx.py` | BNTX textures: decode, encode whole or only some 4x4 blocks (Tegra block-linear swizzle, BCn via Pillow) |
| `ap/tools/build_mod.py` | builds the RomFS mod (`mods/Archipelago/romfs/...`) from the player's own game files: menu text, connect menu labels, text slots, ASCII font (`ascii_font.py`), title logo (`title_logo.py`) |
| `ap/apworld/bayonetta2/` | the apworld (SKELETON: chapter clears, filler); `ap/tools/pack_apworld.py` -> `bayonetta2.apworld` |
| `ap/tests/` | `test_json.c`, `test_inflate.py` (run against zlib) |
| `ap/tools/dev/` | research tools: `aprun.py` (hidden scripted run on a copy of the save), `cpkls.py`, `datls.py`, `elfstrings.py`, `xref.py`, `trace_patch.py` |

Test builds link to `B2recomp/build/aptest` (`build_runtime.py --out=build/aptest`) so a running game never blocks them.
A test seed: `ArchipelagoGenerate.exe --player_files_path <dir> --outputpath <dir>` (the apworld in
`C:/ProgramData/Archipelago/custom_worlds`), then `ArchipelagoServer.exe --host 127.0.0.1 --port 38281 <zip>`.
`RECOMPS_AP_TYPE=<text>|<text>` types into the fields the test opens (addresses, slot) without a window.

## Connect menu

- Main menu: entry 5 ARCHIPELAGO -> the same wheel shows ADDRESS / SLOT / PASSWORD / CONNECT / BACK (`TIT_AP_*`).
- The help line is a text slot `TIT_AP_TXT_<entry>` (96 characters, font 20) rewritten in the loaded mmTIT001:
  `f_7100bbefe0("TIT", 1)` -> message set, `+8` -> the MCD bytes. Font 20 completed with ASCII by the mod
  (the game has no glyph for `# $ % * < = > [ \ ] ^ \` { | } ~`; `_` is made from `-`).
- The wheel is set again with `0x7100d58950(wheel = [[0x710186dce0]], cursor)`, cursor also written to `[0x710186dd08]`
  and `wheel+516`. The title controller is skipped for a frame to keep B (and everything while editing) from the game.
- Pad bits (nnhid.c): 0 A, 1 B, 2 X, 3 Y, 12..15 d-pad left/up/right/down, 16..19 left stick left/up/right/down.

## Title logo

- `ui/ui_title_us.dat` -> `title.wtp` = 6 BNTX packed back to back (offsets in `title.wta`); `title_logoimg` 2048x2048 BC3.
- Front logo "O" ring centre (630, 850) r 50; back logo lens (634, 262); text strip x 676..960, y 948..1046.

## Messages (text)

- Text lives in `mm/mm<NAME>_<lang>.dat` (DAT: `.mcd` + its own font texture `.wtp`). English = `_us`.
- MCD format: see `ap/tools/mcd.py`. Each file only has glyphs for the characters it uses.
- `0x7100dd220c(name)` = CRC32 of the lowercased name (zlib CRC32). MCD events store it `& 0x7fffffff`, sorted.
- `0x7100bbefe0(name, 1)` returns a loaded message file (used for the menu's help line).
- A text part: `+16` = message file, `+200` = message index, `0x7100ba2650(part, ...)` refreshes it.

## Title main menu

- Wheel object (`cUI...`): `+504` kind (0 = main menu), `+508/+512/+516` cursor copies, 5 visible parts set by
  `0x7100d58950(wheel, cursor)`; `0x7100d58cd4(wheel, kind, cursor, x)` sets the next menu.
- Label of entry: `0x7100d5959c(?, kind, index)`; help line: `0x7100d58ae0(?, kind, index)`; both wrap modulo 5.
- Title controller `0x7100b6132c`: cursor int at `[0x710186dd08]` = `0x710195e9c8` (never wrapped; up/down = -1/+1).
  A press at `0x7100b617e4`: cursor modulo 5 -> jump table `0x710152daf8`; exit `L_7100b61474`.
- Entries: 0 STORY `TIT_BASE_01`, 1 TAG CLIMAX `TIT_BASE_18`, 2 UMBRAN TEARS `TIT_BASE_15`,
  3 GALLERY/??? `TIT_BASE_14`/`TIT_BASE_20` (`0x7100d88a38` / `0x7100d907bc` unlock checks), 4 OPTIONS `TIT_BASE_03`;
  5 ARCHIPELAGO `TIT_BASE_AP` (ours).
- Menu messages: `mm/mmTIT001_us.dat`; font 1 + font 2 = the label's two layers; font 20 = help text.

## Useful runtime switches

- `SSBU_SEQ` scripted input (see `B2recomp/tests/route_first_fight.txt`), `SSBU_SHOT_STEPS=1` a picture per step,
  `SSBU_SHOT_EVERY` / `SSBU_SHOT_FROM`, `SSBU_MODS=<folder>|none`.
- `RECOMPS_WATCH=<heap offset>,<len>` / `RECOMPS_WATCH_ABS=<address>` with `RECOMPS_WATCH_RW=3`: hardware read/write
  watch that names the game function. Heap allocation is deterministic for the same save and input.
- Don't print from hooks that run on many threads at once: a run deadlocked on stdout.

## Save object (game progress) — static at 0x71034db3f0 (`[0x71018668d0]`)

The save file's working copy; accessors around 0x7100d88000-0x7100d9c000 (list: `ap/research/save_accessors.txt`).
Watch it live: `RECOMPS_AP_SAVEWATCH=1` (runtime/ap_dev.c). Log every setter call: `ap/tools/dev/save_trace_patch.py`.

| Offset | What | Evidence |
|---|---|---|
| +0x1e4 | play time, float seconds | savewatch |
| +0x190.. | file header while saving (magic 0x20140207, sizes 0xacc0 / 0xa4a0) | savewatch |
| +0xa224 | int16 counters by index (0x7100d90a10 add, 0x7100d90a28 get); NOT the inventory (+0xa228 ticks up in play) | accessor, savewatch |
| +0xa450 | item "obtained" bitset by game item ID, MSB first: the Items page lists only these (Green Herb = bit 3) | Items page test (run59) |
| +0xa564 + 8 * id | item record, +0 = count (Green Herb +0xa57c, Broken Moon Pearl +0xa5f4) | Items page test, your chests |
| +0xa54c + 8 * set + 4 * slot | D-pad quick slots (item ID, -1 empty); 0x7100af3e04 reads it | code, savewatch |
| +0xa82c | Broken Witch Hearts held: bit count (the page shows it mod 4) | Items page test |
| +0xa830 | Broken Moon Pearls held: bit count (mod 2) | Items page test |
| +0xa838 | set with +0xa830 when a Moon Pearl piece is picked up: the piece's world flag? (location) | savewatch |
| +0xadd0 / +0xadd8 | this chapter's chests: bits (0x20, then 0x820) / how many opened | savewatch (2 chests, Ch. I) |
| +0xa260, +0xa310, +0xa320, +0xa46c, +0xa474, +0xa47c, +0xa4d8/dc/e0, +0xaba0 | bitsets (index >> 5 word, bit 31 - index & 31?) | accessors |
| +0xa310 bit 0xb | "???" (gallery) unlock on the title menu | 0x7100d907bc |
| +0xa46c | set from stage-object code 0x71007dxxxx (chests / collectibles?) | callers |
| +0xa4d4 | Witch Hearts (max HP = base + n * 2000) | 0x7100d90aa8, player init |
| +0xa840 | Halos, clamped at 999,999,999 (0x7100d935c8 add, only caller 0x7100b0a678 = result tally) | accessor |
| +0xadc0 | float: time in the current verse/chapter | savewatch |
| +0xadcc | halos picked up in the current chapter (merged into +0xa840 at the result) | savewatch |
| +0xac00 | 0x1c0 bytes cleared at chapter start: this chapter's verse results, 14 x 0x20 | 0x7100d900a4, savewatch |
| +0xac00 + 0x20*i | record: +0 ranks? (04030307), +8 score?, +0xc halos, +0x10, +0x18 = 1 when done | savewatch (Prologue verse) |
| +0xaa88 | a bit set when the Prologue verse ended | savewatch |

## Game data tables (core/core.dat, BXM -> `ap/tools/bxm.py`; XML in `ap/research/core/`)

- ChapterData: chapters 0 (Prologue), 1..16, 17 (Epilogue) with their verses (129 story verses); chapter 20 =
  Muspelheims c60..c7a + Witch Trials c7b..c7f; 21 = Rodin; 22..26 = Angel Slayer.
- VerseHashIndex: 260 verse hashes (sorted); VerseDisplayNo: hash -> softId.
- Phase archives `ph*/p###.dat`: EnemySet.bxm (spawns; Id >> 16: 1 characters, 2 enemies), triggers, positions.
- Game text: `ap/research/messages_us.txt` (all English messages, `ap/tools/dev/dump_messages.py`).

## Chapters, verses and the gameplay client

- save +0x1e0 = the chapter CONTINUE resumes (not the one being played: a Chapter Select replay keeps it) (ChapterData numbering: 0 Prologue (city + jets, phase 110), 1 "Noatun" .. 16, 17 Epilogue);
  titles: `0x7100bee5e0(c + 1)` -> CTT_TITLE_CHAPTER_%02d (c), 18 -> 99 "New Hope".
- save +0xa7c8 = checkpoint phase (0x120 = p120) then the sub-phase name ("INTRO_EV"); +0x284 / +0xa88 / +0x128c /
  +0x1a90 = per-difficulty progress counters (stride 0x804).
- Chapter + verse checks: the chapter result commit 0x7100d94274(save, chapter, results, verses = save + 0xac00, n)
  (callers: the result screen 0x7100c5e1b0 with rd32(result + 616); 0x710088bb10 with 20 = Muspelheims) -> ap_chapter_done:
  record i of +0xac00 with +0x18 == 1 -> 221000 + chapter * 32 + i + 1; the chapter -> 221900 + chapter.
- Chapter start: 0x710088e6c4 clears the verse records, then 0x7100b22a60(phase manager [0x7101869650], save +0xa7c8
  phase, save +0xa7d0 sub-phase name, 1). The phase manager holds the live phase (offset: RECOMPS_AP_SAVEWATCH "phase").
- Per-frame hook: 0x7100d9293c(save, dt) (play time) -> runtime ap_game_frame(save) (ap_patch.py GAME block).
- Player candidates (to confirm): [[0x71018668f8] + 48 / + 72] -> player object, +0xcedc HP, +0xcee0 max HP.
- Scripted runs: on the title, STORY -> second entry is CHAPTER SELECT ("Start from Chapter Select?" YES); the
  opening cinematic of the Prologue cannot be skipped (its pause menu only has "Play").

### Verses (confirmed live 2026-11-12)

- Verse names hash as CRC32(lowercase name) & 0x7fffffff (all 226 match VerseHashIndex). save +0xadc8 = the hash of
  the verse being played.
- Verse end: 0x7100d6f104 reads the hash (L_7100d6f288: x1 = rd32(info + 8)), asks the phase manager for its number
  (0x7100b4f450), then 0x7100d93fb4(save, number - 1, record) stores the record at +0xac00 + slot * 0x1c (+0x18 = 1).
  Found with RECOMPS_WATCH_ABS=71034e6008,4 (save +0xac18). ap_patch.py calls ap_verse_end(hash) there.
- ap_verses.txt (build_mod.py) = hash, chapter, verse number for all 226; story chapters have 127 real verses
  (Chapters I and XVI list an unnamed placeholder); chapter 20 = 53 (Muspelheims c60..c7a, Witch Trials c7b..c7f).
- 0x7100d94274 (chapter result commit) did NOT run when the Prologue was cleared in story mode; story chapter clears
  are taken from save +0x1e0 (CONTINUE chapter) moving c -> c + 1.
- Player HP: [[0x71018668f8] + 48] + 0xcedc; + 0xcee0 is the health bar's trailing value, not max HP.
- Testing: RECOMPS_AP_TEST_GOD=1 keeps Bayonetta's HP up so scripted mashing finishes fights.

## Game item IDs (item table [0x710186ee40], 40-byte records {id, ?, ?, ?, NAME, COM, INFO, HELP, icon, icon_n})

0 Unicorn Horn, 1 Baked Gecko, 2 Mandragora Root, 3 Green Herb, 4 Mega Green Herb, 5 Purple Magic, 6 Mega Purple
Magic, 7 Bloody Rose, 8 Mega Bloody Rose, 9 Yellow Moon, 0xa Mega Yellow Moon, 0xb (NAME_12), 0xc Midas's Testament,
0xd (NAME_17), 0xe Red Hot Shot, 0xf Witch Heart, 0x10 Broken Witch Heart, 0x11 Moon Pearl, 0x12 Broken Moon Pearl,
0x13..0x23 Angelic Hymns Gold LPs (RECORD_NAME_xx), 0x24.. files (FILE_03_NAME_xx).
Test: RECOMPS_AP_TEST_GRANT=<apworld item ids> (ap_game.c) applies items once in a stage; the Items page is reached
in a scripted run from a Chapter I checkpoint save copy: CONTINUE, then `600:- 200:E 200:E` (Minus, R, R).

## Chapters and chests (2026-11-12)

- ChapterData chapter numbers vs the game's titles: 0 (p110) and 1 (p120-p140) are both the Prologue (its card says
  PROLOGUE); k >= 2 is Chapter k - 1 (p212 Noatun = Chapter I); 17 = Chapter XVI; there is no Epilogue entry.
  The apworld names locations by the shown chapter (Items.py DATA_CHAPTERS); IDs stay by ChapterData number.
- Stage placement tables per phase archive: `<phase>_TreasureChest.bxm` (Id = contents code, Num, No, Type),
  `_SetCrow.bxm`, `_SetBook.bxm` (the hint books). Dump: `ap/tools/dev/scan_chests.py` -> `research/chests.tsv`.
- Opening a chest sets bit No (MSB first) of save +0xadd0 and counts +0xadd8 (manager 0x7100b1ba00: object +4512 =
  its No). Your chests: Ch. I No 26 (Id 12, Green Herb), No 20 (Id 17, type 3, Broken Moon Pearl). +0xadd4 bit:
  set earlier for the type-3 chest (appeared?). Chest Nos are unique per chapter; story chests used are all < 32.
- Chest types 4-8 (Id 0, Num 0) come in groups of five: left out (props?). pa11 = the Muspelheim reward chests.
- Runtime: ap_game.c check_chests (phase +0xa7c8 -> chapter via mods/Archipelago/ap_phases.txt) -> 223000 + c*64 + No.
  Verified with RECOMPS_AP_TEST_SETBITS=add0:00000820 (both chests reported).
- Vanilla contents: ap/tools/chests_mod.py (run by build_mod.py) points each real chest's Id/Num at "1" (1 Halo) in
  the mod's copies of the phase archives (74 chests). Type 9 (items lying in the open) stays vanilla: with Halos it is
  taken as the stage loads (bits 9, 10 of Ch. I set at once). Their pieces are overridden by the piece enforcement.

## Crows, files, Bewitchments (2026-11-12)

- Umbran Crows: save +0xa824, bit = the crow's UniqueId (SetCrow tables, 20 crows; 0x7100d9662c counts them).
- Files: game items 0x24..0x41 (ITM_FILE_03_NAME_01..30); picking one sets its "obtained" bit (+0xa450 bitset).
  Not placed in any stage table (events / chests?), so their chapters are unknown.
- Bewitchments (Umbran Tears of Blood): save +0x1b0 bitset, bit k = COM_ACHIEVE_k, set by 0x7100d9045c (which also
  queues the popup; the shop calls it with 11 on a first purchase). (+0x5208 changed near them too: something else.) NOT +0xa46c / +0xa474: those start set and get cleared (0x7100d96a20 /
  0x7100d96b44 clear a bit) - "new" flags. Requirements: SUB_ACHIEVE_INFO_nn in mmSUB040.
- +0xa820: hint books read, bit = the book's Type (your log: Types 5 and 2 in Chapter I).
- Popups: HUD info messages COM_HUD_INFO_01 "Caught an Umbran Crow.", _02 "Received a Bewitchment."; the crow
  popup is made by 0x7100b5e840 via 0x7100babe88([0x7101866000+3632], 0xa); the bewitchment one draws
  "COM_ACHIEVE_%02d" (0x7100c9194c).
- AP item popups (done): the Bewitchment box. Queue [[0x710186dc88]] bit k (k < 30) shows "Received a Bewitchment."
  (COM_HUD_INFO_02, mmCOM001) + COM_ACHIEVE_k (mmACV000) when the player is free; it does not touch the save. The
  client points those two event records (+4 = message index) at the mod's AP_HEAD_RECV / _SENT / _TRAP (mmCOM001) and
  the 64-char slot AP_POPUP_TEXT (mmACV000), sets bit 3, and puts them back 420 frames later. Text: PrintJSON ItemSend
  (ap_net.c), names from Connected players / slot_info and GetDataPackage. mmACV000 font 20 got digits and FGJKNOYZj;
  "qxz'&:" did not fit its atlas (shown as "?"). The crow box: queue [[0x710186dca0]] bit = crow (shows count / 20).
  Message files in memory: f_7100bbefe0("ACV", 0) / ("COM", 1) -> [result + 8].
- Popup icons (done): the medal of Bewitchment 3 is the 64x64 square at (64, 64) of ui_core texture 0 (256x512 BC1).
  Texture memory changes show on screen at once (the renderer re-reads it), so the client writes a pre-encoded copy
  of that texture with the item's icon (ap/tools/popup_icons.py -> mods/Archipelago/ap_icons.bin) while the popup is
  up. UI sprite tables (.uvd): records of name[64] then {hash, texture id (= .wta id table entry), x, y, w, h,
  1/tex w, 1/tex h}. Rewriting .uvd records at run time does nothing (layouts bind sprites at load).

## Gates of Hell (shop), 2026-11-12

- Accessory purchase 0x7100c27710: 0x7100d96d44(save, id 1..14) sets bit id of +0xa478 (owned) and +0xa47c (new
  mark); 0x7100d93c30 tests +0xa478. Pays through 0x7100d935ac / 0x7100d9341c; first purchase: +0xa35c bit 31 and
  Bewitchment 11. Techniques owned: +0xa4d8 bitset (0x7100d90908 tests it).

## 2026-11-12: shop, cheats, books, goal, links, hosted test

- Accessories: ITM_ACCESSORY_NAME_m = bit m + 1 of +0xa478 (verified in the menu). Techniques: bit TEC_NAME_SP_nn
  of +0xa4d8 (new save c2060000). Client: bits AP did not give = a Gates of Hell location (225100 + m, 225200 + nn);
  while connected the bits are exactly what AP gave (ap_game.c shop_frame).
- Weapons: NOT found. +0xa260 is the 53 verse cards (weapon table [0x710186cf98] + 2564, 0x24 bytes, entries 0-52
  are cards, 53.. weapons per hand set); setting all of +0xa260 still listed only Love Is Blue. Weapon items are
  accepted and ignored by the client.
- Books (SetBook): reading one sets bit Type of +0xa820; locations 224100 + Type; each new read asks for a hint
  (LocationScouts of a random unchecked location of this world, shown as a "Hint" popup). No file locations: the 30
  files are not placed in any stage table.
- Bewitchments = +0x1b0 (0x7100d9045c). Chapter complete: checkpoint phase moves into the next ChapterData chapter,
  or Bewitchment 1 (Prologue) / 8 (Chapter XVI). Goal from slot_data, sent as StatusUpdate 30 (goal_frame).
- Chapter blocking: checks of a locked chapter are held in the progress file ([held]) until its unlock arrives.
- Cheats: ARCHIPELAGO > CHEATS (God Mode, Infinite Halos, Infinite Magic), config\archipelago.ini [cheats].
- Links (ap_net.c): ConnectUpdate tags from slot_data, Bounce/Bounced. Verified on a hosted seed with
  ap/tools/dev/ap_test_client.py (ChecksFinder partner): Trap Link out, Ring Link out, Death Link in. Death Link in
  only brings HP to 1 (the game clamps writes of 0 outside its damage code) - not a real death yet.
- Traps working: Pickpocket, Bloodletting, Fragile Witch, Magic Drain, Amnesia, Sticky Fingers. Not: Squish, Squash,
  Angel Ambush, Grace & Glory, Nemesis, Alfheim Curse, Berserk, Gracious & Glorious, Fairness & Fearless.

## 2026-11-12: Death Link (a real death)

- Writing HP 0 is undone: 0x7100019acc copies HP into the lagging field player +0x5f4 (2 frames), and 0x710005d6b8
  (called from the action handlers) puts HP and +0x5f4 back to 1 when +0x5f4 < 1. A real death goes through an action.
- The game's instant kill (enemy grabs): 0x7100088c0c(player) = HP 0, +0x5f4 0, setAction (vtable + 440) 0x11d; its
  callers then call 0x7100b0f884() = the game over ("The Witch Hunts Are Over" -> CONTINUE?). Death Link in: ap_game.c
  sets g_kill, ap_game_frame returns bit 2, the ap_patch.py GAME block calls both on [[0x71018668f8] + 48].
  Verified on the hosted seed both ways (in: killed + Continue screen; out: "Bayonetta fell." bounced to the partner).
- At a death the game writes 2000 into save +0xadcc until the Continue clears it: Ring Link keeps its baseline while
  HP is 0 (it sent 40 rings before).
- Player +0x568 looks like the action id (0x7100019acc tests it against 53 / 117).
- Test: RECOMPS_AP_TEST_KILL=<frames>[,own]. watch.c: watch_arm_at honours RECOMPS_WATCH_RW=3 (reads too).
- ap_test_client.py: a ping no longer restarts the wait (it blocked "wait N" while the server kept pinging).

## 2026-11-12: Weapons (found)

- Weapon i owned = bit i (MSB first) of save +0xa46c; +0xa474 = its "new" mark; +0xa470 = bought at the Gates of Hell.
  The game's grant 0x7100d96c68(save, i) sets them; the subscreen builds its weapon list at open (0x7100b3e7e0:
  i = 0..20, owned (0x7100d93c08) and usable by the character (0x7100bee380 hands / 0x7100bee43c feet masks)).
  NOT +0xa310/+0xa314 (verse-card set, entries of the [0x710186cf98] catalog) and NOT +0x15c (per-character, a HUD fade).
- Weapon i -> name for Bayonetta (0x71008ca1a4, decoded by its jump tables): 0 Love Is Blue, 1 Rakshasa,
  2 Takemikazuchi, 3 Alruna, 4 Kafka, 5 Chernobog, 6 Chain Chomp, 7 Undine, 8 Salamandra, 11 Arwing, 12 Handguns,
  13 Scarborough Fair, 14 Shuraba, 15 Rodin, 17/18 Mistelteinn (04_01); Unforgiven (03_01) is another character's.
- Client: ap_game.c weapons_frame keeps the AP weapons exactly what AP gave (Love Is Blue, Chain Chomp, Arwing untouched).
  Verified: RECOMPS_AP_TEST_GRANT=220002,220011 -> Kafka and Rodin with NEW in the weapon ring (screenshots).
- apworld: Love Is Blue, Unforgiven and the Golden LPs are no longer in the pool.
- Scripted input cannot rotate the weapon ring (it shows the new weapons but the cursor stays): check by eye.
- Tools: RECOMPS_AP_TEST_WATCHPTR=<pointer>,<offset>[,<seconds>] arms watch.c on [pointer] + offset (ap_dev.c).

## 2026-11-12: Traps (all 15 fire)

- Player transform: +0x10 world matrix (translation +0x40), +0x50 position, +0x60 rotation, +0x70 scale. The scale only
  reaches the hair/attachments (0x710095a908); the body's matrix (0x710095b06c) is built from position + rotation and
  scaling it after that function moved her instead of resizing. So no size traps; Squish / Squash hold buttons off.
- Input block: nnhid.c ap_hid_block(mask) clears player 1's buttons (bits BTN_A=0, B=1, X=2, Y=3, ... ZR=9) after
  ap_hid_buttons sees them. Default controls (Type A): X punch, A kick, B jump, ZR evade, Y action.
- Traps: Squish = no B / ZR 20 s; Squash = no X / A 15 s; Alfheim Curse = 3 hits in 30 s -> 1 HP; Berserk = hits count
  double 30 s (traps_frame watches HP drops >= 10; drops made by traps / links set g_hp_set and do not count).
  Spawn traps (Angel Ambush, Grace & Glory, Nemesis, Gracious & Glorious, Fairness & Fearless) are stand-ins mixing the
  others until spawning: EnemySet parsing 0x7100b7dcd8 (field names at 0x71014b1943..), "EmSetManagerWork",
  p%03x_SummonPos.bxm - not followed further.
- Verified: Squish (no jump at the same frame vs a control run), Berserk / Curse / Grace & Glory with
  RECOMPS_AP_TEST_HITS (HP numbers), Fairness & Fearless (magic emptied).
- Battle checkpoint for research: p216 sub-phase P216_BTL01 (Chapter I); RECOMPS_AP_TEST_WARP=2,216,P216_BTL01 did not
  move the CONTINUE there (still p212 plaza) - not solved.
- Tests: RECOMPS_AP_TEST_TRAP=<frames>,<k>, RECOMPS_AP_TEST_HITS=<frame>,<hp>, RECOMPS_AP_TEST_WATCHPL=<off>,
  RECOMPS_AP_TEST_DUMP pointer 0 = the player object.

## 2026-11-12: In-game Options menu (for a Cheats category) - research so far

- cUIOptMenu (help-line function 0x7100c46c38): +0xa8/+0xb0/+0xb8/+0xc0 = the 4 category controllers (Controller,
  Camera, Sound, Display = cUIOptDisplayCtrl); +0x118/+0x128 = the tab list (24-byte entries {category 0-3, enabled 1}),
  +0x130 = count 4 / capacity 4; +0xd8 = tab cursor; +0x1f0 = 7, +0x1f8.. = 7 row widgets (shared by the categories?).
  Messages: mmOPT000 (OPT_TITLE_BASE_0n, OPT_<CAT>_BASE_nn / _HELP_nn, OPT_SEL_01/02 ON/OFF).
- Layouts: ui/ui_option*.dat, 31 .uid files (an undocumented node-tree format); a 5th tab button needs either a .uid
  edit or the tab list growing to 5 with the game making the button. Not done yet.
- The tab cursor count is passed as a constant 4 (0x7100c46e14(layout, cursor, 0, 4) plays the 4 tab parts' states);
  pointing the tab list at a 5-entry array changed nothing (navigation still wraps Display -> Controller).
- The layout object ([menu + 160], constructor 0x7100d03944) embeds exactly 4 tab parts (+1664 .. +2336, 224 bytes
  apart) and 7 row parts (+2560 .. +3904); option0030.uid holds the 4 tab labels (OPT_TITLE_BASE_0n hashes, nodes 0x428
  apart). A 5th tab = a new uid node + a part object outside the layout + drawing it: not attempted.
- Category controllers own typed row widgets: Display (0x7100c4b5d0 builds its rows; row list +1672 / count +1704;
  jump table at 0x7101533dc8): +208 slider, +464 / +728 selectors, +992/+1000 and +1256/+1264 ON/OFF pairs. Camera has
  4 ON/OFF + 2 sliders. Row labels are set from code by message name (OPT_<CAT>_BASE_nn), so they can be renamed.

## 2026-11-12: Cheats pages on the Options' Display tab (runtime/ap_opt.c)

- A 5th tab is not possible cheaply (the 4 tab parts and 7 row parts are fixed in the layout; the cursor count is a
  constant 4). Instead L / R on the Display tab turn pages: page 0 = Display, pages 1.. = Cheats (the tab label and
  help line say so). Hook: ap_patch.py OPT block wraps the menu update 0x7100c439e8 (ap_opt_frame before and after).
- Display controller (menu +192): row list [+1752] (24-byte {type, enabled, label hash}); row cursor +1672; slider
  widget [+208] (value +732, display value +744, step table [+656] 7 floats 0.7..1.3, count +652 as float); ON/OFF
  rows 3 / 4: value +1024 / +1288 (0 = ON), buttons [+992]/[+1000] and [+1256]/[+1264] (+520/+524 = chosen; looks the
  same at rest). Menu: +124 focus (1 tabs, 2 rows, 3 editing), +120 2 = "Apply changes?", +216 tab.
- Rows show what the list says only when the tab gets the focus again: the client presses Up then Down for the player
  (nnhid.c ap_hid_press) and 15 frames later puts the cheats' values in; the dial is moved with 0x7100d0638c (it shows
  an exact step value one position right: position k is written as step k - 1, position 0 as 0). The slider sets the
  brightness live (save +0x13c): held at the player's value after each update while a cheats page is up. Leaving
  (B on the tabs, another tab, the question) puts the real Display rows and values back first.
- Messages: mmOPT000 gets every ASCII glyph in font 20, AP_CHT_* labels, AP_OPT_SLIDER (the slider label with its
  value), and the Display help lines moved to font 20 with room (build_mod.py opt_files); OPT_LANG_* / _FIXED_* are
  blanked on cheats pages and restored after.
- Cheats now: Damage Taken (x0 .. x5), God Mode, Infinite Halos, Halo Gain (x1 .. x50), Infinite Magic. Values in
  config\archipelago.ini [cheats] (ap_opt.c owns them; ap_cheat / ap_cheat_value). The title's ARCHIPELAGO > CHEATS is gone.

## 2026-11-12: Easy Cutscene Skip and Auto QTEs (Cheats page 3)
- Pause menu (ap_patch.py PAUSE block wraps its update 0x7100c63c4c -> ap_pause_frame): item ids at +204.. (a
  cinematic's menu: 0 Play, 10 Skip, 13 none), cursor +248, state +188 (0 list, 1 the Skip YES/NO question with YES
  first, then 2 / 3 while it closes). Easy Skip: on a menu with item 10 the cursor is set to it and A pressed (10
  frames after opening), then A again 10 frames into the question. Verified: Plus on Chapter I's map intro jumps to
  the next scene; the gameplay pause menu (no item 10) is untouched.
- Event script conditions: 0x71007b5d80(x0, record); record +0 type, type 3 = a name at +4 compared with the
  strings FADE_END, RECORD, PUSH_PAUSE_END, RESULT_PAUSE_END, ACT_SUCCES, ACT_FAILED, RAPID_SUCCES, RAPID_FAILED, LOOP,
  LOOP_FAILED. ACT = [[0x7101866000+2544]+164] != 0; RAPID_SUCCES = [[0x7101867d88]] bit 26; RAPID_FAILED compares
  the mash count +12 with 8 / 16 / 24 by the difficulty byte +120. ap_patch.py COND block -> ap_event_cond(record):
  with Auto QTEs on, ACT_/RAPID_SUCCES answer 1 and _FAILED 0. Called only by the event code at 0x7100e2d9xx; not
  reached in Chapter I's opening (RECOMPS_AP_TEST_QTE=2 logs every condition), so not yet seen on a real prompt.
- Re-check of the five older cheats (Chapter I, RECOMPS_AP_TEST_HITS=300,500 = a 500 HP drop every 60 frames):
  God Mode on keeps HP at 2000 (off: 1500, 1000, 500, 1); Infinite Magic sets save +0xa53c to 500.0 (off: stays
  111.25); Infinite Halos sets +0xa840 to 999999999 (off: 0). Damage Taken and Halo Gain were checked earlier. All kept.

## 2026-11-12: Verse ranks (options verse_rank_target / verse_rank_max_medal, like B1)
- Verse result record [0x71018700f0], filled by 0x7100d7010c just before the verse-end hook (ap_verse_end): +0..+2
  the category medals (bytes, start 7), +3 the overall medal, +4 time, +8 score; 0x7100d93fb4 copies it to save
  +0xac00 + slot * 0x1c. Overall medal = entry of the table [0x7101870180] (6 x 12 bytes {points >=, medal, Halo
  bonus}): 120 -> 8 (1000), 105 -> 7, 75 -> 6, 45 -> 5, 15 -> 4, 0 -> 3 (0) = Pure Platinum, Platinum, Gold, Silver,
  Bronze, Stone. A verse the game marks unrated (save +0xaafc bit) gets 03030303 = Stone.
- runtime verse_medal(): the medal's place in that table -> 5 Pure Platinum .. 1 Bronze, 0 Stone. On a story verse's
  end it sends 226000 + t * 1000 + c * 32 + v for t = 0 (Any Medal) and 1 .. medal (Bronze+ .. Pure Platinum); the
  server ignores the tiers the seed lacks. apworld: Locations.py RANK_TIERS (762 locations, group "Verse Ranks"),
  __init__.py _rank_wanted (any_medal -> tier 0; all_medals -> tiers 1 .. max medal).
- Tested: RECOMPS_AP_TEST_RANK=1 prints the table; RECOMPS_AP_TEST_VERSE=<hash>,<medal byte> ends a verse with that
  medal. Hosted seed (all_medals, max Gold): Gold on Chapter I Verse 1 and Pure Platinum on Verse 2 each sent the
  verse + Bronze+ / Silver+ / Gold+. A scripted Prologue run never finished Verse 1 by mashing (20 min on the jets).

## 2026-11-12: Enemy spawns (the five spawn traps)
- Object factory table [0x710186c248] = 0x71017d7da0: 32-byte entries {ID, create fn, flags, class name}; the type
  prefixes {0x10000 pl, 0x20000 em, 0x30000 wp, ...} at 0x71017c0530. Factory 0x71009cee34(pool, set info, ID, ...)
  creates, stores set info at obj +1400 / +1304, ID at +1408.
- Request API (173 callers): 0x71009a7740(manager [0x71018668d8], name, ID, set info) -> a request (its files are
  loaded first, then the object is made; the request keeps the set-info pointer, so it must stay valid). Set info
  0x78 bytes as the player spawn (0x710000cb20) builds it: +0x10/+0x20/+0x30 rotation rows, +0x40 position, +0x58
  scale, +0x64 -1. Player position: player +0x40.
- ap_patch.py GAME block: ap_game_frame bit 4 -> ap_spawn_take (one request per 0.4 s, 16 persistent set-info
  slots, a few steps beside Bayonetta) -> the request -> ap_spawned. Test: RECOMPS_AP_TEST_SPAWN=<frame>,<gap>,<ID>...
- IDs seen in game: 0x20000 Affinity (B1 ID kept), 0x20050 / 0x20051 Fearless / Fairness (geared beasts, B1 IDs),
  0x21000 the Masked Lumen, 0x20030 / 0x20031 a purple demon pair, 0x20060 / 0x20061 a golden winged angel pair.
  0x20040 is made and removed at once (a part: its sets have ParentId); 0x20041..0x20043 are not in the factory.
  Enemy lists per phase: ap/tools/dev/scan_enemies.py (_EnemySet.bxm).
- Traps: Angel Ambush 3 x 0x20000; Grace & Glory 0x20030 + 0x20031; Nemesis 0x21000; Gracious & Glorious 0x20060 +
  0x20061; Fairness & Fearless 0x20051 + 0x20050. Not checked: whether a spawned Masked Lumen can be beaten outside
  its own fight (in a test it knocked Bayonetta into the water).
- 2026-11-12, later: Nemesis removed (user). Its ID 220610 stays unused so the other traps keep their numbers; Trap
  Link ignores an incoming "Nemesis".

## 2026-11-12: Chat box (replaces the item popups; user's choice over the game's notification)
- runtime/ap_chat.c keeps the last 32 lines (text + a colour per character); ap_net.c on_print turns every PrintJSON
  (item sends of everyone, hints, chat, joins/leaves/goals) into one line, coloured by part (player_id: own magenta /
  others yellow; item_id by flags: progression purple, useful blue, filler cyan, trap salmon; location_id green; "color"
  parts mapped); the book hints (LocationInfo) too. The popups are no longer queued.
- Drawn by nvn_vk_draw.c chat_draw in the overlay pass (after the game's picture, so SSBU_SHOT pictures lack it): a box in
  the bottom left with the newest lines while one is under 12 s old, wrapped at 40 % of the width, line height H / 30.
  ap_chat_font.bin (build_mod.py chat_font): the game's font-20 glyphs (ink only) pre-drawn on the box colour in the 8
  colours, so each character is one scaled blit. The font lacks [ ] { } < > = * # ...: ap_net.c swaps in ( ) - + etc.
- Test: RECOMPS_AP_TEST_CHAT=<frames> posts sample lines; screenshot from the screen (SSBU_SHOW=1), ap/screenshots/chat_box.png.
- Vulkan renderer only (the GL renderer has no chat box).

## 2026-11-12: Muspelheims are verses (correction)
- The story's Muspelheims are the pa11 arenas the stages' SetPortal tables lead to (Subphase PA11_ANGEL_nn /
  PA11_DEVIL_nn); ChapterData lists each as a verse of its chapter (Chapter I verse 1 = PA11_ANGEL_02_VRS), so they
  already were verse checks with rank tiers (seen live: Pure Platinum and Gold on Chapter I's). 22 of them; the apworld
  now names them "<chapter> - Muspelheim k" (Locations.py MUSPELHEIM_VERSES), IDs unchanged.
- ChapterData chapter 20 (c60..c7a, c80; Witch Trials c7b..c7f) is NOT Muspelheims: Tag Climax stages. The option is now
  include_tag_climax (default off; was include_muspelheims, default on) and those locations "Tag Climax k".
- 2026-11-12, later: the chat box is now see-through. ap_chat.c composes the panel on the CPU when a message arrives
  (premultiplied RGBA: rounded corners, a dark gradient at ~50-68 % opacity, a light edge, a purple accent on the left,
  text with a drop shadow, colours by part) from ap_chat_font.bin, now plain glyph coverage ('APCA': alpha only).
  nvn_vk_draw.c chat_draw uploads it (NSLOT staging buffers) into a texture half the window's size and blends it with one
  draw (premultiplied over x the fade; own pipeline, dynamic rendering into the swapchain image, or in HDR into the 8-bit
  picture, which got COLOR_ATTACHMENT usage). It fades out over the last 1.5 s of the 12 s.

## 2026-11-12: Story co-op, milestone 1 (menu, lobby, connection)
- User: direct IP only; B1's co-op (Steam lobbies + P2P, unfinished) is only a loose reference.
- Main menu wheel: 7 entries (game 0-4, ARCHIPELAGO 5, CO-OP 6; ap_ui.c MAIN_*). CO-OP opens the lobby menu on the same
  wheel: HOST, CONNECT, ADDRESS, NAME, PASSWORD, LOBBY (public / private), CHARACTER (21 Bayonetta / Jeanne outfits,
  names only so far), BACK. Labels TIT_BASE_COOP "Co-oP", TIT_CO_HOST / NAME / LOBBY / CHARACTER (build_mod.py; the
  menu font has no J, Q, V, Z, so JOIN is CONNECT); help lines reuse TIT_AP_TXT_0..7. Fields in archipelago.ini [coop].
- runtime/coop_net.c: UDP port 27870 (ADDRESS ":port" changes it), packets "B2CO" v1: HELLO (name, password, skin) ->
  WELCOME / DENY (full, wrong password, version), PING / PONG every second, 8 s silence = gone, BYE, BEACON (public
  lobbies broadcast to port 27871 every 2 s; CONNECT with an empty address joins the first one heard), GAME (later
  milestones: coop_send / coop_take). Events go to the chat box. HOST again closes, CONNECT again leaves.
- Tested: two copies on one PC, host through the real menu, joiner with RECOMPS_COOP_TEST=join:127.0.0.1: both report
  the connection. Screenshots ap/screenshots/coop_main_menu.png, coop_lobby_hosting.png.
- Not done: the partner in the game (M2: a second player character driven by the partner's state; the type table has
  pl IDs 0x10000.., players [0x71018668f8] +48 Bayonetta / +72 the partner slot), skins applied, enemy and verse sync,
  chapter / checkpoint sync, internet (the host must forward UDP 27870; no NAT punching without a server).

## 2026-11-13: Story co-op, milestone 2 research (the partner character)
- Object creation trace: ap/tools/dev/objtrace.py <gen_eh> wraps 0x71009a7740 (request), 0x71009cee34 (create),
  0x71009cfd6c (pool) with RECOMPS_TRACE_OBJ=1 logging (TEMPORARY: copy the original c_00cd.c / c_00d1.c back after).
- Bayonetta = ID 0x10000 (class pl0000; many characters / outfits share the class: 0x10000..0x100e9), requested by
  0x7100b10ac4 from a character table ([0x710186c000+3544], 20-byte entries: +4 ID, +12 name) picked by the player
  selection [0x710186c000+3528] (+12); her init 0x7100008360 requests her parts (0x1001f "pl0000", 0x40062 ...).
- The player list [0x71018668f8]: +32/+48 player 1 (set by 0x71009942a4), +56/+72 player 2 (0x71009942b0), +80/+96
  (0x71009942bc, when obj +3984). 0x710000ca10 picks by obj +3968 (copied from +1248): 0 -> player 1, 1 -> player 2.
- A request (0x71009a7740) for 0x10000 "pl0000" with set info word +0 = 1 makes a second Bayonetta as PLAYER 2 (slot
  +72), player 1 unchanged; with +0 = 0 she replaces player 1 (camera follows her). Test: RECOMPS_AP_TEST_SPAWN=300,30,
  10000 RECOMPS_AP_TEST_SPAWN_SI=1,0,0,0 RECOMPS_AP_TEST_PLAYERS=1 (logs the slots and the words differing p1/p2).
- Player 2 is NOT updated or drawn in a story stage: her matrices stay identity, she is not visible. Poking +0x1d4=1,
  +0x4e0=0, +0x254=0x201 (player 1's values) does not wake her. Next: find what updates player 1 each frame (the
  player manager's update list / a "players in play" count, as Tag Climax has two) and include player 2, or use player
  1-style updating for a puppet.
- 2026-11-13, later (M2 research continued):
  - Player 2 (set info +0 = 1) IS updated: the player methods (vtable 0x710176c328: +176 f_7100019acc, +184
    f_7100021ddc, +192 .. +216) are called on her each frame (ap/tools/dev/mtrace.py). Inside, player 2 takes other
    branches (f_7100019acc: obj +3968 != 0 skips the local-input part), so she stays in her bind pose, invisible.
  - The game's own player-2 spawn (Tag Climax) is in 0x7100b10ac4 after player 1: second record ([+3528] +16 / +20 /
    +28 = the character code into the table [0x710186c000+3544]), only when [[0x7101866000+2336] +16] <= 2; its set
    info: +0 = 1, +12 = 1 (when the mode is 1), +72 = 1.0, +100 = 0x102 (not -1); after: obj +10548 = 1 in mode 1.
    Using those words (RECOMPS_AP_TEST_SPAWN_SI=0=1,c=1,64=102) changes nothing visible.
  - Player 2's init requests her parts (pl0009, pl000a, pl000b, pl0002, pl0001, 0x1001f, 0x10031, 0x1003a, 0x1003f,
    weapons 0x30000, effects 0x4006x/0x4007x): the request processor 0x71009a69e8 returns 1 for each but the pl parts
    are never created (effects and weapons are). Player 1's identical requests at stage load are created.
  - Working theory: player 2 is Tag Climax's REMOTE player, posed from network data (pia session) that story mode never
    has. Next: find where her update reads the remote state (the f_7100019acc player-2 branch, [0x7101866000+2272]
    +19 bit 6) and feed it from coop_net; or, if that path is unusable, a puppet of another kind.
- 2026-11-13: M2 partner WORKS (first version), runtime/coop_game.c + ap_patch.py COOP block:
  - Player 2 proper (set info +0 = 1) is Tag Climax's remote player (posed from pia network data): not usable.
    Instead the partner is a second PLAYER-1-type Bayonetta (0x10000, set info +0 = 0): fully built, animated, drawn.
    She takes the player slots (+32/+40/+48, +80/+88/+96) and the camera target words; coop_game.c puts player 1 back
    and her into +56/+64/+72 (the camera then follows player 1: [[0x71018668f0] +2712] = player 1).
  - Controllers: raw records [0x7101870000+1424] (0xc0 each, 0x7100d9ea30) -> controller objects 0x71018aab70 +
    0x1f0 * n (0x7100d9eef4, from 0x7100969c90); player 1 reads object 0. The COOP block wraps the player class's
    update methods (vtable +176..+216: 0x7100019acc, 0x7100021ddc, 0x7100033194, 0x7100033d90, 0x7100034968,
    0x7100041720) with coop_obj_pre / coop_obj_post: while the partner's run, object 0 (+16..+0x1f0) holds the
    partner's own controller object (sent every frame with the sender's position, 0x1f0 + 20 bytes, UDP).
  - Her place: snapped to the sender's player position when more than 3 units off.
  - Tested: two copies on one PC (RECOMPS_COOP_TEST=host / join:127.0.0.1, both CONTINUE into Chapter I): each spawns
    the other as a partner who walks and fights with the other's controller. ap/screenshots/coop_partner_bayonetta.png.
  - Known: the stick is camera-relative, so the same input walks another way under another camera (the snap corrects
    it, jerkily); no outfit / character yet (always Bayonetta); the partner stays when the connection drops; enemies,
    verses, checkpoints and cutscenes are not synced; ap_game.c RECOMPS_AP_TEST_P2KEEP / SPAWN_SI are research switches.
- 2026-11-13, M2 "smooth her movement" (runtime/coop_game.c, coop_net.c, ap_patch.py COOPFNS):
  - Stick: controller object +0x10 / +0x14 = left stick x / y floats (-1000..1000, up = -1000). The camera object
    [0x71018668f0] (0x71018a48b0): +0x170 eye, +0x180 look-at point; heading = atan2(dx, dz) is the way "up" runs.
    The state message carries the sender's heading; her stick is turned by (heading here - heading there).
  - Position: her place lives at +0x40 (transform) AND +0xee0 / +0xef0 (body now / before); writing only +0x40 is
    undone each frame. The pull shifts all three by 15% of the gap per frame; a snap only past 8 units. Tested: the
    partner tracks the other player's real position within ~0.6 units at full run (one frame of lag), no snaps.
  - Input leak: the game runs player 1's update methods NESTED inside the partner's (hundreds of calls a second), so
    coop_obj_pre/post keep a stack and give each call its own record. Players also read the controller COPY array
    ([0x7101866998] -> 0x71018ab520 = controller objects + 5 * 0x1f0, filled by 0x710004e3d4); both are swapped.
    0x710004ae88 (vtable +688, reads the copy) is wrapped too. A small leak remains (~0.8 unit tug on the host's
    partner while the host runs; the pull holds her). Wrapping ALL the player vtable methods in player code (22 more)
    broke the connection; reverted.
  - coop_net.c bug fixed: `now` was read before lock() / on_packet, so a packet stamped g_last_heard later than now
    and the unsigned `now - g_last_heard` wrapped -> random "lost the connection" within a minute or two. Now read
    after on_packet. The network thread also runs at THREAD_PRIORITY_HIGHEST.
  - OPEN, serious: the CAMERA in co-op. With the partner in the stage the camera jumps between following player 1,
    views from inside a Bayonetta, and one fixed far view of the pier; on the joiner it can stay on a fixed scenic
    shot. Not caused by the input swap (RECOMPS_COOP_NOPAD test), the pull (snap only), the player-2 slot (leaving
    +56..+72 empty is worse), her player number (+3968 = 1 outside her updates: no change) or her character (0x10010
    Jeanne: same). Camera +0x590 (the focus feeding +0x180) flips between player 1 and her; +0x5a0 stays on player 1.
    Each player's own update writes camera +0x450..+0x4ac (restoring it after hers did not help). Likely the partner,
    being a full player-1 object, makes / drives a camera of her own. Next: find who writes camera +0x590 / which
    camera the renderer uses (camera manager list), or create her without her camera.
  - Characters: the factory table has pl0000-class IDs 0x10000 (Bayonetta), 0x10010 (Jeanne, shows white), 0x1001e,
    0x10020, 0x10050..: spawning 0x10010 works (for the skin picker).
- 2026-11-13, M2 camera FIXED + input rebuilt (runtime/coop_game.c, ap_patch.py COOPSKIP / COOPINPUT):
  - Found with hardware watchpoints (debug registers on every thread, a vectored handler, stacks mapped through
    game_fn_table_p): the camera's eye / look-at are set once a frame by the camera module (0x710091ccdc ->
    setters 0x7100db51a4 / 0x7100db5204); its follow block (camera +0x450..+0x4ac) is written by 0x710003e8f4, which
    the player update 0x7100034968 runs for EACH player-1-type object. With the partner it ran twice a frame with
    different players -> the jumping camera. Fix: 0x710003e8f4 is skipped for her (COOPSKIP, coop_obj_skip). Tested:
    eye ~3 units behind / above player 1 like single-player, on both copies (ap/screenshots/coop_camera_fixed*.png).
    (+0x590 is only an idle orbit: player 1 +0x5a0 plus a turning offset. Tag Climax's real player 2 stays a network
    puppet, unusable in story mode, so the fix keeps the player-1-type partner.)
  - The old controller swap (COOPFNS wrappers around the update methods) was wrong at its root: the players' updates
    run on several threads at once, the global swap's call stack grew without bound (depth 2000+) and stopped
    swapping, and the partner simply read the local controller (with the pull off she copied the host step for step).
    Removed. Instead: each player copies controller 0 into its OWN input copies (object +2112 and +2608, 0x1f0 each)
    in 0x7100025c68; right after (COOPINPUT, coop_obj_input) hers are overwritten with the partner's record (the whole
    0x1f0 object: +0 buttons / stick direction bits 0xf0000, +4 pressed, +8 released, +0xc repeat, +0x10 stick),
    stick turned by the camera headings and its direction bits redone. Tested: with the pull off she no longer moves
    with the host; with it on both partners stay within ~0.15 units of the other player's real position.
  - Research switches and traces from this work were removed (objtrace / camtrace gen patches reverted).
- 2026-11-13, enemy sync (runtime/coop_enemy.c; ap_patch.py COOPNEW wraps the object factory 0x71009cee34 ->
  coop_obj_new(obj, ID)):
  - Enemy object: HP = int at +0x5f4 (2000 for an Affinity); writing 0 is a real death (the game's death, halos,
    combo points follow); place +0x40 (writing it alone sticks; +0xec0 the body copy is moved too); set info pointer
    +1400 (-> +0x40 the set position); gone = the destructor sets the base vtable 0x71017d51c8.
  - Pairing key: ID + set position rounded to 1 unit + how many of that ID were made there before (not the order of
    making: the two games' triggers fire at different times).
  - HP: each game sends each enemy's HP (20 times a second, up to 36 per message, in turns) and takes the LOWER of
    the two. Sending damage totals counted every hit twice (the partner on each screen repeats the other player's
    hits); the lower HP does not, and a kill on either side kills on both. An enemy that left without dying (gone,
    HP > 0) is not copied.
  - Place: the host's enemies are the real ones; the joiner's are pulled toward them (20% of the gap per frame,
    put there past 15 units).
  - Tested (two copies, RECOMPS_AP_TEST_SPAWN_AT=x,y,z puts a test spawn at the same place in both): the host punches
    an Affinity, the joiner's copy follows its place within ~0.3 units, loses HP with it and dies at the same moment
    (explosion, halos, combo on the joiner's screen); the kill takes as long as in single-player.
    ap/screenshots/coop_enemy_sync_*.png. Test switch RECOMPS_COOP_EMDBG=1 lists the enemies once a second.
  - Not yet: an enemy only one game has made (a wave triggered on one side) is not made on the other; enemy attacks
    and which player they chase are each game's own.
- 2026-11-13, wave sync (runtime/coop_enemy.c; ap_patch.py COOPREQ wraps the object request 0x71009a7740):
  - The factory 0x71009cee34 is handed the REQUEST (x1), not the set info; the request call is where the set info
    is (x3). COOPREQ tells coop_obj_req(0, ID, set info) before the call (the enemy is made INSIDE it when its files
    are loaded already) and coop_obj_req(request, ID, set info) after; coop_obj_new(obj, ID, request) looks it up.
    Set info +0x40 = the set position (the key), +0x64 = -1 for a top-level enemy (parts have their parent there).
  - Waves: an enemy the other game lists that this game never made (not gone, top-level) is made here at its set
    position (ap_game.c spawn_object_at: the spawn queue with a position, 0.15 s apart) as a MIRROR with the other
    game's key. This game's own k-th enemy of an ID at a place pairs with the other game's k-th there: if that is a
    live mirror, the new one takes its HP and the mirror is put away (1000 units down, HP 0); if the mirror was
    beaten already, the new one is put away the same way (this game's own fight counts the kill and can end).
  - Tested with two copies: (1) the host's enemy only: the joiner makes the mirror, it follows the host's HP / place
    and dies with it; (2) the joiner's own trigger while the mirror lives: its enemy takes over (HP 250 -> 100 -> 0
    with the host's); (3) the joiner's own trigger after the kill: put away, no enemy comes back on either side.
    ap/screenshots/coop_wave_sync_*.png.
  - Not tested: a real stage wave (the plaza p212 has only townspeople; the first fight, p216 around (-158, -28, 330),
    is not reachable by a scripted run: setting the player's place is undone by her collision body, and the way there
    is not known). Mirrors of a real wave are made by the object request like the traps', outside the stage's enemy
    set manager, so the joiner's own fight (arena walls, verse) only starts when the joiner walks in.
  - Research left in (test switches): RECOMPS_AP_TEST_SPAWN_AT=x,y,z, RECOMPS_COOP_EMDBG=1.
- 2026-11-13, waiting for each other (runtime/coop_sync.c; ap_patch.py COOPVERSE / COOPFREEZE; ap_chat.c banner):
  - Banner: ap_banner_set(text) -> a one-line panel at the top middle in the chat box's style (ap_chat.c compose into
    its own buffers); nvn_vk_draw.c chat_draw draws both panels (two textures / descriptor sets, one pipeline).
  - Cinematic skip vote: the pause hook (ap_opt.c ap_pause_frame -> coop_pause_frame) treats choosing Skip as a vote
    when the partner's game is talking: at the YES/NO question A is held off (nnhid.c coop_hid_block, its own mask
    beside the traps'), the vote goes out (M_SYNC, every 6 frames) and the menu backs out (B: cursor to No, B: back
    to the list, Play + A: closed), so the cinematic goes on with "Skip 1/2 - waiting for your partner"; the partner
    sees "Your partner wants to skip (1/2)". Both voted -> each game opens its pause menu (Plus) and skips (Skip, A,
    YES); partner votes are ignored for 3 s after a skip (their last "skipping" words). Easy Cutscene Skip votes at
    once. Tested with two copies on Chapter I's opening cinematic: one vote each, both skipped together.
  - Verse gate: the verse start is 0x7100d6eb14(hash, ..) (found with RECOMPS_WATCH_ABS on save +0xadc8, called from
    the game flow 0x7100992cdc via 0x7100b85038). COOPVERSE tells coop_verse_start(hash); with a partner heard in the
    last 15 s who has not reached that verse, this game is held: COOPFREEZE skips the object manager's six update
    passes (0x71009cfa6c, ..cfb1c, ..cfb54, ..cfc28, ..cfc60, ..cfc98 from 0x71009923b4), so every object stands still
    (2D UI, scripts, requests and the runtime's own frame hook go on), banner "Waiting for your partner (1/2)"; the
    partner's game shows "Your partner is waiting for you at a verse (1/2)". When the partner's game reaches the same
    verse (M_SYNC carries the verse each game is at, for 15 s after it starts), it goes straight on and this game
    lets go ("Both players here (2/2)"). The hold also ends if the partner is gone.
  - Tested: RECOMPS_COOP_FREEZE=<frame>,<frames> (single player) holds an Affinity and Bayonetta in place, then the fight
    goes on; two copies into the Prologue (Chapter Select: STORY, CHAPTER SELECT, YES, l l = "The End", A, A, A): the
    Prologue's verse 1 (3e76bc75) starts as the chapter loads (before the opening cinematic); the first game held ~12 s
    (black screen and the intro text) until the second arrived, then both went on.
  - Test switches: RECOMPS_AP_TEST_BANNER=<text>, RECOMPS_COOP_FREEZE. A test build finds the chat font only with
    SSBU_MODS=<B2recomp>\mods (aprun sets none).
  - Not tested: a verse that starts mid-stage (trigger areas), Muspelheim portals (each player's own; the other player
    would be waited for there too), a long loading screen on one side (15 s window).
- 2026-11-13, partner leaving and falling together (runtime/coop_game.c):
  - Object removal (found by watching an enemy's vtable as it died: 0x7100992cdc -> 0x71009a75f8 -> 0x71009a6d1c ->
    destructor): the object manager [0x71018668d8] keeps a list at +112 (node +0 entry, +16 next; +120 the end). Its
    removal pass looks at entries with flags (+12) bit 1 set: object byte +0x4fc bit 1 = "remove me" -> the pass sets
    bit 0 there, then the entry's bit 0, then destroys it. The players' entries have flags 0 (never removed), so the
    partner's entry gets bit 1 too. When the connection is gone (coop_role != CONNECTED) she is removed that way, her
    partner slots (+56/+64/+72) get back what was there before her, and her input is zeroed until she is gone.
    Tested (two copies): removed within half a second, the host plays on; a new joiner gets a new partner.
  - Until her player's state arrives (the partner is still in menus) her input copies are zeroed (before, she copied
    the host's controller).
  - Her HP here = the partner's own HP from the state message (never below 1): she falls only in her own game.
  - Falling together: the state message carries a count of this player's own deaths (HP to 0 on the same player
    object). When the partner's count goes up, this game's Bayonetta is killed (ap_game.c ap_coop_kill = the Death
    Link kill, not sent to AP; not counted back), banner "Your partner fell - back to the checkpoint" (360 game
    frames, set at once: the game over screen runs no game frames). Both take the Continue and restart from the
    checkpoint; the enemy pairing starts over (coop_enemy.c coop_enemy_reset). Own deaths, AP Death Links and traps
    that kill count and go to the partner.
    Tested (two copies, RECOMPS_AP_TEST_KILL=900,own on the joiner): both at CONTINUE?, both back at the Chapter I
    checkpoint with a new partner each. ap/screenshots/coop_fall_and_leave_sequence.png, coop_partner_removed.png.
  - Step screenshots (SSBU_SHOT_STEPS) are the game's frame without the chat box / banner (drawn at present).
  - Not handled: two players at different checkpoints (each restarts from its own), a death while the other player
    has no player object (menus, cinematics: ignored).

## 2026-11-13, first real seed: fixes
- Disconnects: a book's hint scouted a random location of the whole game (the DataPackage); one not in the seed
  (e.g. a tier-0 "Any Medal" rank in an all_medals seed) makes the server raise KeyError in LocationScouts and drop
  the client. Hints now pick from Connected's missing_locations (ap_net.c g_missing).
- Techniques: +0xa4d8 bit = the game's technique ID, not TEC_NAME_SP_nn. 0x71008cf93c(id) names it (jump table
  0x710151d0a0, ids 3..28): 3 Crow / 4 Bat / 26 Umbran Spear (Bayonetta; Jeanne Owl / Moth, Rosa Falcon / Hornet by
  save +0xa4b4), 8 Tetsuzanko, 9 Heel Stomp, 10 Stiletto, 12 Heel Slide, 15 Witch Twist, 16 Heel Tornado,
  17 Breakdance, 18 After Burner Kick, 19 Witch Strike, 21 Air Dodge, 27 Charge Bullet, 28 Dodge Offset. The Gates of
  Hell lists 28 21 10 12 8 9 17 19 18 15 16 27 4 3 26 (0x7100b39b74). Other bits of the word are story flags (bit 2
  turns on by itself: it was taken for Tetsuzanko). Location numbers kept from the first seeds (ap_game.c tec_location).
- ChapterData 0 = "Records of Time - The End" (COM_BASE_CHAPTER_95), 1 = Prologue "World of Chaos", 2 = Chapter I.
  Its verse is now named "Prologue - Records of Time" (same ID).
- Chapter Select: per difficulty k (0..4) a status per ChapterData chapter c at save +0x238 + 0x804 * k + 0x4c * c
  (0 not reached, 1 reached, 2 open, 3 cleared on that difficulty; k = 4 is all 0 until unlocked). The cards offer c
  when status(c) >= 1 and status(c - 1) >= 2 (tested by editing save copies: [3,2,2,2,2,2,2,0] offers up to Chapter V).
  ap_game.c chapter_select_frame raises every chapter up to the highest Chapter Unlock to 2 in each block with
  status(0) != 0, from the title menu (ap_ui.c ap_menu_frame -> ap_title_frame: the cards are built before any stage
  runs) and in play. Tested on a local seed (start_inventory Chapter Unlock: Chapter V, Prologue save): Chapter Select
  offers Prologue .. Chapter V, and Chapter V starts. config [cheats] send_locked_chapters=1 still sends a locked
  chapter's checks anyway (the user's first seed); a held check says so in the chat once per chapter.
- Infinite Halos (cheat) pins +0xa840, so Halo items show nothing while it is on.
- Chapter Select shows only given chapters (B1's behaviour): the card menu 0x7100b2aa80 counts the entries 0..22 of
  the chapter table [0x710186d298] (0..17, 22..26, then 0 ending it) that 0x7100d96244(save, c) calls open, and card i
  is table[i] (menu +11152 the card, +11156 the count; 0x7100b2aca0 finds a chapter's card). The client puts its own
  table there (given story chapters first, the bonus chapters 22..26, then the rest), 0x7100d96244 answers "given"
  for c < 18 (ap_chapter_given) and 0x7100b2aca0's card of a hidden chapter becomes the last given card
  (ap_card_index); both wrapped by scripts/ap_patch.py patch_chapsel (its own marker, so an already patched gen gets
  it). Found with read watches on the statuses (RECOMPS_AP_TEST_WATCHPTR=71018668d0,a88,14 RECOMPS_WATCH_RW=3).
  Tested on a local seed: Records of Time, Prologue, Chapter I, Chapter V shown, II-IV hidden; Chapter V starts.
