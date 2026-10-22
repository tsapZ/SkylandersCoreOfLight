# The launcher (phase 1, first version 2026-10-22)

`coreoflight.exe`: one screen with a card per game, in release order. It starts each game's own
exe and gets out of the way while the game runs; no game needs it.

## What it does

- **Finds the games** (#6), for each in this order:
  1. a path in `coreoflight.ini` next to the launcher (`ssa = D:\Games\Spyro\ssarecompiled.exe`,
     keys `ssa`, `giants`, `swapforce`, `trapteam`, `superchargers`, `imaginators`; a folder works
     too);
  2. the installer's folder, `%LOCALAPPDATA%\Programs\<Game> Recompiled\`;
  3. next to the launcher (the exe itself, or a folder named like the repository or the app);
  4. a developer's layout above the launcher: `<Repo>\<Repo>\out\build\win-amd64-release\<exe>`.
- **Checks the disc:** the game looks for `rom\` in the folder it runs in (an installed game next
  to its exe, a developer's build three folders up), so the launcher looks there for
  `rom\default.xex` and compares its SHA-256 with the hash the game's repository pins
  (`docs/game/default.xex.sha256`): "Bereit", "Andere Disc-Version" or "Disc fehlt".
- **Shows a few of the game's own settings** from its settings file next to its exe
  (`<game>recomp.toml`): resolution scale, frame rate, native 3D, language. It only reads it.
- **Sets up a disc** (#6): X (D) picks an ISO of the owner's own disc; dropping one on the window
  or on coreoflight.exe does the same. The launcher reads the image's file system itself (XDVDFS,
  src/xiso.cpp: a full Xbox 360 dump with the game partition 0xFD90000 into it, or a trimmed image),
  knows the game by its default.xex's SHA-256 (the hash each game pins) and unpacks the disc into
  the rom\ folder that game reads, with a progress bar; default.xex comes last, so a folder left
  half done never counts as a disc, and B doesn't quit meanwhile. A disc that is set up already, a
  game that isn't installed and an unknown disc each get their own message. A folder with an
  unpacked disc (default.xex in it) works the same way: O picks one, or drop it on the window;
  its files are copied into the rom folder.
- **Each game's own settings** (#7): Y (Tab, a right click) on a card opens the game's settings
  with exactly the pages of its F4 menu (Grafik, Bildrate, Spiel, Portal, Steuerung), their texts,
  options and explanations. The launcher doesn't know them itself: it starts the game's exe with
  `--settings_schema=<file>` (skylanders-portal `settings_schema.h`), and the game writes its pages
  with the values it would start with and quits after about 2.5 s, before it opens a window or
  checks its disc. The chosen card's settings are read ahead. Left / right (A, a click) changes a
  setting, LB / RB (Q / E) switches the page, Y saves the changes into the game's settings file
  line by line as the game's menu does (comments and the rest stay), B goes back (with unsaved
  changes, a second B throws them away). A preset (Leistung / Qualität / Beste) sets its
  settings together. A game built before the flag existed isn't asked (the launcher looks for the
  flag's name in the exe first, so it never starts a whole game by mistake).
- **Starts the game** (#5) in that folder with A / Enter / a click on the chosen card, hides
  itself while it runs and comes back when the game ends.
- Counts the figure files in the shared folder `Documents\Skylanders\Figures`.
- **The figure library** (#8, Start / F): every figure in that folder with its level, XP to the
  next level, gold, play time, upgrades, nickname and the day it was last played, by game (LB /
  RB), read with skylanders-portal's figure code (its `skylanders_portal_figures` target, built
  without the games' SDK, AES and MD5 from Windows). X switches to every Skylander of a game, the
  owner's marked ("11 von 54 dabei"), each with the progress of the owner's figure. Y on a
  Skylander without a file marks it **"Hab ich"** (I own it: on the shelf, not as a file); it then
  counts as owned and shows in "Meine Skylander" as "hab ich (ohne Datei)", and Y takes the mark
  back. The marks are the profile's, `Skylanders\owned.txt` next to its figure folder (one
  `<id>\t<variant>` a line); the games never read it. The pictures
  are the games' own: each game, while it runs, leaves the figure pictures it reads from its disc
  in `Documents\Skylanders\Pictures\<game>` as `<id>-<variant>.png` (skylanders-portal
  `StartFigurePictureCache`), and the library takes the newest game's picture of each figure; a
  figure no played game has a picture of gets its element's colour and first letter. The figure's
  hat shows by its game name, with its picture and bonuses: the games leave `hats.txt` and
  `hat-<n>.png` there too. A (Enter) on one of the owner's figures opens its **upgrade tree** as
  the games' details view has it: the attacks, basic upgrades, soul gem and Wow Pow, the two paths
  side by side, the bought ones bright, the chosen one's picture, price and text in the panel; the
  games leave the trees of the owner's figures (`upgrades-<id>-<variant>.txt` and their pictures).
  Copies the
  games keep in a `real` folder before they write to a real figure are left out.
  `figures = <folder>` in coreoflight.ini uses another figure folder (as the games'
  `portal_figures_dir`); the pictures are then in the `Pictures` folder next to it.
- **Profiles** (#4, Back / P / a click on the profile at the top right), like an Xbox account:
  a name, a Skylander's picture (Y: the profile's own figures and each game's hero, those a game
  has left a picture of), their own saves, achievements and figures. The main profile is the data
  as the games keep it without the launcher (`Documents\<game>recomp`,
  `Documents\Skylanders\Figures`), so nothing changes for it. Every other profile is a folder
  `Documents\Skylanders\Profiles\<id>` (its `profile.ini` has the name and picture) that stands
  in for Documents: the launcher starts a game with `--user_data_root=<folder>\<game>recomp`, the
  game then keeps the profile's figures in `<folder>\Skylanders\Figures` and its pictures in
  `<folder>\Skylanders\Pictures` by itself (the games put both next to their user data's
  folder), and `--cache_root=Documents\<game>recomp\cache` keeps the main profile's shader
  cache, so a new profile doesn't build it again. A new profile can start with the current
  profile's figures made new (the same Skylanders back at the start, new serial numbers), with
  copies of them as they are, or with none. In the figure library, Y (C) copies the chosen figure's
  file, as it is, into another profile's figure folder (the same sub-folder, "Name (2).dump" if the
  name is taken). X renames it (keyboard), Back deletes it into the
  recycle bin. `Profiles\profiles.ini` remembers the active profile and the main profile's name
  and picture. The games themselves still call their player "User".
- **Achievements** (RB / E, for the chosen game; LB / RB switch the game): the game's own list
  with its pictures, the active profile's unlocked ones bright with their date, how many and the
  gamerscore; each card shows "Erfolge 2 / 46". The list comes from the game: while it runs it
  leaves `achievements.txt` and the title's pictures (`ach-<n>.png`, the PNGs of its XDBF) in its
  picture folder (skylanders-portal `StartAchievementCache`); what is unlocked is the profile's
  `<game>recomp\achievements\<title id>.toml`, which the SDK keeps.
- Each card also says when the active profile last saved the game (the newest file in its save
  folder, `<game>recomp\B13EBABEBABEBABE`).
- **Play time** per game and profile, on the card ("Gespielt 3 Std. 12 Min."): the time from
  start to end of each game the launcher started, for the profile it was started with, kept in
  `Skylanders\playtime.txt` next to the profile's figure folder (`<game key>\t<seconds>`). A game
  started on its own isn't counted.
- `--documents=<folder>` runs the launcher on a copy: that folder in place of Documents for the
  figures, pictures and profiles, and the games it starts get their user data there too.
- Controller (d-pad or stick, A, B), keyboard (arrows, Enter, Escape) and mouse; German or English
  by Windows' display language.

## What it doesn't do yet

- Settings that take a free value (no list of options) are shown but not changed here; the
  game's own menu (F10) has them.
- The profile's name in the games (the SDK's XAM always answers "User"); online co-op (#1-#3).
- Pictures: the cards are plain colours and text; pictures would come from the owner's discs at
  run time, as in the games' menus (no artwork is shipped).

## Build

From a Visual Studio 2022 x64 developer prompt:

    git submodule update --init
    cmake --preset release
    cmake --build --preset release

The exe is `out\build\release\coreoflight.exe`. It is a plain Win32 / Direct3D 11 program with
Dear ImGui (thirdparty/imgui, 1.92.5, the version the games' SDK uses), so it builds in seconds
and needs neither the SDK nor the games' sources.

## Checked (2026-10-22)

- Achievements: Trap Team, run once on a copy, left 46 achievements with 46 pictures and their
  German names; with two of them unlocked in the copy's `trapteamrecomp\achievements\41560911.toml`
  the card said "Erfolge 2 / 46" and the screen "2 von 46 Erfolgen · 20 / 1000 G", the two bright
  with their dates.

- Play time, on a copy with `coreoflight.ini` pointing Giants at a stand-in exe that runs 61 s:
  after it ended, `playtime.txt` had `giants	61` and the card "Gespielt 1 Min.".

- "Hab ich", on a copy without figure files: Y on Camo in "Alle Skylander" marked it ("1 von 597
  dabei", the tile "hab ich (ohne Datei)", the hint then "Hab ich nicht"), "Meine Skylander" showed
  it, and Y there took the mark back (`owned.txt` empty again).

- Profiles, on a copy (`--documents`): a new profile with its figures made new (29 figure files,
  only their headers left), renamed by keyboard, given a picture; the launcher shows it at the top
  right and counts its figures. Started from the launcher (a stand-in exe that writes its command
  line), the game got `--user_data_root=<copy>\Skylanders\Profiles\p2\giantsrecomp` and
  `--cache_root=<copy>\giantsrecomp\cache`; Giants itself, started that way, put its pictures in
  the profile's `Skylanders\Pictures\Giants` and its shader cache in the main one.

- Disc set-up: the Trap Team ISO on the developer layout said "die Disc ist schon eingerichtet" and
  chose Trap Team. In a test folder (coreoflight.ini pointing Spyro's Adventure at a stand-in exe)
  the Spyro's Adventure ISO unpacked in 38 s: the same 1,142 files with the same sizes as the
  owner's rom folder, default.xex and two large archives with the same SHA-256; the card turned
  "Bereit".
- Settings: Trap Team's five pages, a change, the page switch and the question about unsaved
  changes; every game writes its pages in 1.1 to 2.5 s.

- In the developer layout all six games were found, their discs "Bereit" (the hashes match), the
  cards show each game's settings; arrows move the choice.
- With `coreoflight.ini` pointing one game at a stand-in exe (a copy of a system program, so no
  game ran against real saves): A started it in its folder, the launcher hid while it ran and came
  back when it was closed; the other games showed "Nicht installiert".
