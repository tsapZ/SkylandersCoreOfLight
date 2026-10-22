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
  game that isn't installed and an unknown disc each get their own message.
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
  owner's marked ("11 von 54 dabei"), each with the progress of the owner's figure. The pictures
  are still plain element-coloured discs: the portraits come from the discs at run time in the
  games, which the launcher doesn't read yet. Copies the games keep in a `real` folder before they
  write to a real figure are left out.
- Controller (d-pad or stick, A, B), keyboard (arrows, Enter, Escape) and mouse; German or English
  by Windows' display language.

## What it doesn't do yet

- Settings that take a free value (no list of options) are shown but not changed here; the
  game's own menu (F10) has them.
- A folder with an unpacked disc instead of an ISO (#6), the figures' pictures and hats (#8),
  profiles (#4).
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
