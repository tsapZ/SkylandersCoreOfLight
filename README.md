# Skylanders: Core of Light

The hub for the recompiled Skylanders games. In the games the Core of Light holds Skylands
together; here it ties the games together: one launcher, a library of your Skylanders, profiles,
and online co-op with each player's own portal.

**Status: the launcher's first version** (2026-10-22, [docs/launcher.md](docs/launcher.md)): one
screen that finds the six games, checks their discs, opens each game's own settings pages (the
same as its F4 menu), sets up a disc from the owner's ISO, shows the owner's figures with their
progress and starts each game's own exe. The figures' pictures, profiles and online co-op are still
planned. Each game stays complete on its own.

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

## The games

| Game | Project | State |
|---|---|---|
| Spyro's Adventure (2011) | [SSARecomp](https://github.com/tsapZ/SSARecomp) | Chapter 1 playable, portal and portal menu |
| Giants (2012) | [GiantsRecomp](https://github.com/tsapZ/GiantsRecomp) | first level playable, portal menu with the game's pictures and upgrades |
| Swap Force (2013) | [SwapForceRecomp](https://github.com/tsapZ/SwapForceRecomp) | tutorial and hub played, real USB portal, settings menu, native 3D option |
| Trap Team (2014) | [TrapTeamRecomp](https://github.com/tsapZ/TrapTeamRecomp) | Chapter 1 with a figure, the portal menu with the game's collection cards |
| SuperChargers (2015) | [SuperChargersRecomp](https://github.com/tsapZ/SuperChargersRecomp) | the first level, the portal menu with the game's collection icons, German from the owner's English / German disc |
| Imaginators (2016) | [ImaginatorsRecomp](https://github.com/tsapZ/ImaginatorsRecomp) | the first level, the portal menu with the game's icons, German from the owner's PS3 disc |

All of them are the Xbox 360 versions, recompiled to native Windows with ReXGlue, so they share
one toolchain, one portal and one settings menu. In every game the portal menu is drawn in that
game's own fonts, with its own pictures of the figures and, where its disc has them, its own
element symbols and coin.

## Planned

| Part | Issues |
|---|---|
| Launcher: start every game, find the discs, settings per game | #5, #6, #7 (first version: [docs/launcher.md](docs/launcher.md)) |
| Figure library: every Skylander's stats without a game running | #8 |
| Profiles like an Xbox account, each with its own saves and figures | #4 |
| Online co-op with each player's own real portal (proposal) | #1, #2, #3, [docs/online-coop.md](docs/online-coop.md) |

The order and what is next: [docs/roadmap.md](docs/roadmap.md).

## License

MIT, see [LICENSE](LICENSE). Skylanders is a trademark of Activision; this project is not
affiliated with or endorsed by Activision, and it ships no game data.
