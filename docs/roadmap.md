# Roadmap

Each phase is a GitHub milestone. A step only counts as done when it was observed in a real build,
not inferred.

## Overview (2026-10-20)

| Phase | State | Issues | What |
|---|---|---|---|
| 0 Repository | ✅ | – | this repo; the online co-op proposal and the profiles idea moved here from SwapForceRecomp (its #28, #29, #30, #51) |
| 1 Launcher | 🟡 first version | #5, #6, #7 | one menu that finds and starts every game (done: finding, disc check, starting; docs/launcher.md), settings per game (done 2026-10-22: the game's own F4 pages) |
| 2 Figure library | ⬜ | #8 | the owner's Skylanders without a game running |
| 3 Profiles | ⬜ | #4 | name, picture, own saves and figures per profile |
| 4 Online co-op (stretch) | 🟡 proposal written | #1, #2, #3 | building it is a separate decision |

## The overall plan (the owner's call, 2026-10-20)

1. **Finish the three games** on the recompilation path: the owner's play test feedback, ultrawide,
   installers (SSARecomp, GiantsRecomp, SwapForceRecomp).
2. **The next games, one by one, each in its own repository:** Trap Team, SuperChargers,
   Imaginators (all three started 2026-10-21, see the table below; the owner's SuperChargers and
   Imaginators discs are Spanish / Italian pressings, Imaginators' German can come from the
   owner's PS3 disc).
3. **This hub:** launcher, figure library, profiles; it starts the games' own exes, so it doesn't
   care how a game is built.
4. **A native renderer** (the Xbox 360 D3D layer on D3D12/Vulkan, a shared library, the emulated GPU
   kept as fallback): research in docs/ai-recomp.md, measurements in SwapForceRecomp
   docs/native-renderer.md. Its cheap first step, a counting build, can come earlier.
5. **Decompilation:** an open decision for after that, per game, starting with the smallest
   (Giants: 23,632 functions; Spyro's Adventure 31,215; Swap Force 81,745). Research, the
   approach that fits (step by step on the running recompilation, not byte-matching: the original
   compiler isn't available) and a rough estimate for Giants: docs/decompilation.md (2026-10-22).

## The games (2026-10-21, afternoon)

| Game | Repository | State |
|---|---|---|
| Spyro's Adventure | [SSARecomp](https://github.com/tsapZ/SSARecomp) | playable (Chapter 1 to the hub), portal menu with the game's pictures, owner play-tested; native 3D option, ultrawide |
| Giants | [GiantsRecomp](https://github.com/tsapZ/GiantsRecomp) | playable, portal menu with portraits, upgrades and hats, owner play-tested; ultrawide |
| Swap Force | [SwapForceRecomp](https://github.com/tsapZ/SwapForceRecomp) | playable (tutorial and hub), the most complete: native 3D option, 4K, a shorter figure intro as an option |
| Trap Team | [TrapTeamRecomp](https://github.com/tsapZ/TrapTeamRecomp) | boots (logo movies skipped), menus, Chapter 1 with a figure; the portal menu opens by itself (players 1 and 2) and shows the game's collection cards and element symbols; native 3D option, ultrawide |
| SuperChargers | [SuperChargersRecomp](https://github.com/tsapZ/SuperChargersRecomp) | boots (logo movies skipped), menus, the first level; the portal menu opens by itself and shows the game's collection icons; German text and voices from the owner's English / German 360 disc (tools/german_text.py); native 3D option, ultrawide |
| Imaginators | [ImaginatorsRecomp](https://github.com/tsapZ/ImaginatorsRecomp) | boots through the licence (Y) and the menus into the first level; the portal menu opens by itself with the game's icons; German text (and experimentally voices) from the owner's PS3 disc with tools/german_text.py; native 3D option, ultrawide |

All six share the SDK fork (`tsapZ/rexglue-sdk`, branch `skylanders`) and the portal library
(`tsapZ/skylanders-portal`, branch `feature/portal-menu`): the portal, player, settings and
achievement menus are the same everywhere. Each game hands the library its own look, read from
the owner's disc at start: its bitmap fonts for the menus' titles and text (each game's
`src/game/game_fonts.cpp`), its pictures of the figures, and its element symbols and coin where
the disc has them. The library knows every figure's element up to Imaginators, read from the
games' own data (Trap Team's character scenes, SuperChargers' and Imaginators' toy data).
Ultrawide works the same way in every game: a wider field of view (Hor+), the movies (logos,
cutscene movies) 16:9 with black bars at the sides while they play, and the HUD at the window's
edges in its own proportions where the game's UI width was found (not yet in Trap Team).

## Now, next, later

| When | What | Who |
|---|---|---|
| Now | Step 1, the three games; then step 2 | me, play tests by the owner |
| Now | Keep the parts the hub needs shared: menus, figure decoding and the figure folder stay in `skylanders-portal` | me |
| Now | Phase 1, the launcher: first version built 2026-10-22 (finds, checks and starts the games); the settings pages (#7), setting up a disc from an ISO (#6) and a first figure library (#8,
without pictures yet) followed the same night | me, a try by the owner |
| Later | Figure library, profiles; online co-op only after the owner agrees to the proposal | both |
