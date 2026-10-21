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
- **Starts the game** (#5) in that folder with A / Enter / a click on the chosen card, hides
  itself while it runs and comes back when the game ends.
- Counts the figure files in the shared folder `Documents\Skylanders\Figures`.
- Controller (d-pad or stick, A, B), keyboard (arrows, Enter, Escape) and mouse; German or English
  by Windows' display language.

## What it doesn't do yet

- Per-game settings pages (#7): the plan is the same pages as the games' F4 menu, from
  `skylanders-portal`, writing the game's own settings file.
- Extracting an ISO into a game's `rom\` (#6), the figure library (#8), profiles (#4).
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

- In the developer layout all six games were found, their discs "Bereit" (the hashes match), the
  cards show each game's settings; arrows move the choice.
- With `coreoflight.ini` pointing one game at a stand-in exe (a copy of a system program, so no
  game ran against real saves): A started it in its folder, the launcher hid while it ran and came
  back when it was closed; the other games showed "Nicht installiert".
