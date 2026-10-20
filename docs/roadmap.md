# Roadmap

Each phase is a GitHub milestone. A step only counts as done when it was observed in a real build,
not inferred.

## Overview (2026-10-20)

| Phase | State | Issues | What |
|---|---|---|---|
| 0 Repository | ✅ | – | this repo; the online co-op proposal and the profiles idea moved here from SwapForceRecomp (its #28, #29, #30, #51) |
| 1 Launcher | ⬜ | #5, #6, #7 | one menu that finds and starts every game, settings per game |
| 2 Figure library | ⬜ | #8 | the owner's Skylanders without a game running |
| 3 Profiles | ⬜ | #4 | name, picture, own saves and figures per profile |
| 4 Online co-op (stretch) | 🟡 proposal written | #1, #2, #3 | building it is a separate decision |

## The overall plan (the owner's call, 2026-10-20)

1. **Finish the three games** on the recompilation path: the owner's play test feedback, ultrawide,
   installers (SSARecomp, GiantsRecomp, SwapForceRecomp).
2. **The next games, one by one, each in its own repository:** Trap Team, SuperChargers,
   Imaginators (discs of the first two are dumped; Imaginators is not owned in a usable language
   yet).
3. **This hub:** launcher, figure library, profiles; it starts the games' own exes, so it doesn't
   care how a game is built.
4. **A native renderer** (the Xbox 360 D3D layer on D3D12/Vulkan, a shared library, the emulated GPU
   kept as fallback): research in docs/ai-recomp.md, measurements in SwapForceRecomp
   docs/native-renderer.md. Its cheap first step, a counting build, can come earlier.
5. **Decompilation:** an open decision for after that, per game, starting with the smallest
   (Giants: 23,632 functions; Spyro's Adventure 31,215; Swap Force 81,745).

## Now, next, later

| When | What | Who |
|---|---|---|
| Now | Step 1, the three games; then step 2 | me, play tests by the owner |
| Now | Keep the parts the hub needs shared: menus, figure decoding and the figure folder stay in `skylanders-portal` | me |
| After all games | Phase 1, the launcher | both |
| Later | Figure library, profiles; online co-op only after the owner agrees to the proposal | both |
