# Skylanders: Core of Light

The hub for the recompiled Skylanders games. In the games the Core of Light holds Skylands
together; here it ties the six Xbox 360 ports together: one launcher, a library of your
Skylanders, profiles, and later online co-op with each player's own portal.

> **Unofficial fan project, work in progress.** Not affiliated with or endorsed by Activision,
> Toys for Bob or Vicarious Visions. It ships **no game data and no artwork**: the pictures, texts
> and music it shows come from your own discs, through the games themselves.

## Why this exists

I wanted to play the Xbox 360 Skylanders games on my PC with just a controller, no emulator, and
with a nicer picture. That's really all it is: a private fan project. It's public in case someone
else wants the same.

## How it was made

To be upfront about it: almost all of the code here was written by [Claude](https://claude.ai) in
Claude Code, the recompile setup, the fixes, the portal menu, the launcher and the installers, and
most of the docs too. I decided what to build, said what was broken and played the games a lot. If
AI-made projects aren't your thing, that's fair.

The six ports also run on their own:
[Spyro's Adventure](https://github.com/tsapZ/SSARecomp),
[Giants](https://github.com/tsapZ/GiantsRecomp),
[Swap Force](https://github.com/tsapZ/SwapForceRecomp),
[Trap Team](https://github.com/tsapZ/TrapTeamRecomp),
[SuperChargers](https://github.com/tsapZ/SuperChargersRecomp),
[Imaginators](https://github.com/tsapZ/ImaginatorsRecomp).

## Play it

You need:

- Windows 10 or 11 (64-bit) and a graphics card with DirectX 12
- **your own copies of the games** as ISOs (or extracted discs): each port's README says which
  disc it runs. Nothing of the games comes with this project
- a controller (Xbox or compatible; keyboard and mouse work too). A real Portal of Power is
  optional: the emulated portal's menu has every figure

Then:

1. Download the newest package from the [Releases](https://github.com/tsapZ/SkylandersCoreOfLight/releases)
   page and unpack it anywhere. It holds the launcher and the six games' installers.
2. Start `coreoflight.exe`. The first-run setup asks for your ISOs, one per game (it finds the
   others in the same folder by itself), checks them and installs the games.
3. Choose a game and play. When the game asks for a Skylander, the portal menu opens by itself.

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

- **Finds the games** (where their installers put them, a path in `coreoflight.ini`, next to the
  launcher or a developer's build) and **checks each disc** against the version its port is made
  for.
- **Sets everything up on a first start** (no game installed yet): the language, then your discs,
  found by itself or chosen by hand, with the room they need and what is free, then each chosen
  game installed and its disc set up, one after the other.
- **Installs a game** that isn't there yet with A on its card: the game's own setup, silently,
  then its disc from your ISO. A newer setup for an installed game shows "Update verfügbar" on
  its card, and A asks: update now (silently; saves, figures, disc and settings stay) or play
  first.
- **Sets up a disc from your ISO**: its own file browser, for the pad as well as the keyboard, or
  drop an ISO on the window; Y in it finds your discs by itself (the ISOs in Downloads, Desktop,
  Documents and on the drives, each listed by its game). It recognises the game, unpacks the disc into that game's folder and
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

Screenshots are left out of this repository on purpose: they would show the games' artwork,
which belongs to Activision.

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

## Reporting a problem

Open an [issue](https://github.com/tsapZ/SkylandersCoreOfLight/issues/new/choose) with the **Problem** form and say
what you did and what happened. The most useful thing to add is the report from Core of Light:
**LB** (the launcher's settings) > **Report a problem**, or the question that comes up by itself
after a crash. It writes one text file to the desktop with the latest logs and settings of every
game, with your Windows user name taken out; drag that file into the issue. Without the launcher,
each game keeps its logs in the `logs` folder next to its exe.

## Building

Windows, Visual Studio 2022 (C++), CMake and Ninja, from the **x64 Native Tools Command Prompt**:

    git clone --recursive https://github.com/tsapZ/SkylandersCoreOfLight.git
    cd SkylandersCoreOfLight
    cmake --preset release
    cmake --build --preset release

## License

MIT, see [LICENSE](LICENSE). Skylanders is a trademark of Activision; this project is not
affiliated with or endorsed by Activision, and it ships no game data.
