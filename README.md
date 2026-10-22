# Skylanders: Core of Light

The hub for the recompiled Skylanders games. In the games the Core of Light holds Skylands
together; here it ties the six Xbox 360 ports together: one launcher, a library of your
Skylanders, profiles, and later online co-op with each player's own portal.

![The launcher's start screen](docs/images/launcher.jpg)

> **Unofficial fan project, work in progress.** Not affiliated with or endorsed by Activision,
> Toys for Bob or Vicarious Visions. It ships **no game data and no artwork**: the pictures, texts
> and music it shows come from your own discs, through the games themselves.

## The games

| Game | Project | State |
|---|---|---|
| Spyro's Adventure (2011) | [SSARecomp](https://github.com/tsapZ/SSARecomp) | Chapter 1 playable; portal menu with the game's portraits, hats and upgrades |
| Giants (2012) | [GiantsRecomp](https://github.com/tsapZ/GiantsRecomp) | first level playable; real 5.1; portal menu with the game's pictures and upgrade trees |
| Swap Force (2013) | [SwapForceRecomp](https://github.com/tsapZ/SwapForceRecomp) | tutorial and Woodburrow played; swappers; real USB portal |
| Trap Team (2014) | [TrapTeamRecomp](https://github.com/tsapZ/TrapTeamRecomp) | Chapter 1 playable; portal menu with the game's collection cards |
| SuperChargers (2015) | [SuperChargersRecomp](https://github.com/tsapZ/SuperChargersRecomp) | first level playable; both European discs (En/De and Es/It) |
| Imaginators (2016) | [ImaginatorsRecomp](https://github.com/tsapZ/ImaginatorsRecomp) | first level playable; German from the PS3 disc |

All six are the Xbox 360 versions, recompiled to native Windows with
[ReXGlue](https://github.com/rexglue/rexglue-sdk), so they share one toolchain, one portal and one
settings menu: 60 FPS and more, 2x or 3x the console's resolution with sharpening, ultrawide, the
emulated or a real USB Portal of Power, and a portal menu drawn in each game's own fonts with its own
pictures of every figure.

## What the launcher does

- **Finds the games** (the installers' folders, a path in `coreoflight.ini`, next to the launcher
  or a developer's build) and **checks each disc** against the version its port is made for.
- **Sets up a disc from your ISO**: its own file browser, for the pad as well as the keyboard, or
  drop an ISO on the window. It recognises the game, unpacks the disc into that game's folder and
  leaves the ISO where it is.
- **A card per game** with the active profile's play time, achievements and last save; the game's
  title music plays while its card is chosen (straight from the game's disc, which each game
  decodes in about a second).
- **Each game's settings** before starting, the same pages as the game's own F4 menu.
- **The figure library**: every figure in `Documents\Skylanders\Figures` with its level, gold,
  play time, hat, upgrade tree and nickname, by game, each card in its element's colour; a tab
  for the magic items and adventure packs; "Hab ich" marks for figures on your shelf without a
  file.
- **Profiles** like Xbox accounts: create, rename, pick a picture, delete (with a confirmation);
  each profile has its own saves, figures and achievements, and the games show its name.
- **The launcher's own settings**: language (German / English), the library's defaults, title
  music, animations.

| The figure library | Profiles | Achievements |
|---|---|---|
| ![The figure library](docs/images/figures.jpg) | ![Profiles](docs/images/profiles.jpg) | ![Achievements](docs/images/achievements.jpg) |

Everything in detail: [docs/launcher.md](docs/launcher.md).

## Principles

- **Every game stays playable without the hub.** The hub starts a game's own exe and writes its
  ordinary settings file; no game depends on it.
- **No game data and no original artwork.** The owner points the hub at their own discs; pictures
  and texts come from those files at run time, as the games' portal menus already do.
- **Shared code lives in the shared libraries**, so the games and the hub use the same parts:
  the portal, the portal and settings menus and the figure decoding in
  [`tsapZ/skylanders-portal`](https://github.com/tsapZ/skylanders-portal) (branch
  `feature/portal-menu`), the recompilation runtime in
  [`tsapZ/rexglue-sdk`](https://github.com/tsapZ/rexglue-sdk) (branch `skylanders`).
- **One figure folder for every game:** `Documents\Skylanders\Figures`, like real figures lying
  next to the portal.

## Planned

| Part | Issues |
|---|---|
| An installer for the launcher, with a library folder for the games | |
| A native renderer for a cleaner picture at high resolutions (the games' D3D layer on D3D12 instead of the emulated GPU; research) | [docs/roadmap.md](docs/roadmap.md) |
| Online co-op with each player's own real portal (proposal) | #1, #2, #3, [docs/online-coop.md](docs/online-coop.md) |

The order and what is next: [docs/roadmap.md](docs/roadmap.md).

## Building

Windows, Visual Studio 2022 (C++), CMake and Ninja, from the **x64 Native Tools Command Prompt**:

    git clone --recursive https://github.com/tsapZ/SkylandersCoreOfLight.git
    cd SkylandersCoreOfLight
    cmake --preset release
    cmake --build --preset release

## License

MIT, see [LICENSE](LICENSE). Skylanders is a trademark of Activision; this project is not
affiliated with or endorsed by Activision, and it ships no game data.
