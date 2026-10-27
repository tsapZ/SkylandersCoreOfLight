# CLAUDE.md: Skylanders: Core of Light

The rules every Skylanders repository follows (tests only on copies of the owner's data, git and
GitHub, shared code, releases) are in maintainer/RULES.md:

@maintainer/RULES.md

## Goal

The launcher for the six recompiled Skylanders games: sets them up from the owner's discs, keeps
them up to date, figure library, profiles, saves, achievements; online co-op is a proposal
(docs/online-coop.md). See README.md, docs/launcher.md and docs/roadmap.md.

`maintainer/` holds the tools for all seven repositories: `release.py` (status, submodule bumps,
versions, builds, release checks, publishing) and `test/` (scripted game runs on copies of the
owner's saves); see maintainer/README.md.

## This repository

- **No game may depend on the launcher.** Each game stays complete with its own exe; the launcher
  starts it and writes its ordinary settings file. Code both need goes into `tsapZ/skylanders-portal`.
- No original artwork: pictures and texts come from the owner's game files at run time.
- New behaviour switches get conservative defaults.
- Launcher tests run with `--offline` and a scratch `--documents`; a game it starts in a test is a
  stand-in exe or gets a copied `--user_data_root`.

## Build

From a Visual Studio 2022 x64 developer prompt: `git submodule update --init`, then
`cmake --preset release` and `cmake --build --preset release` (out/build/release/coreoflight.exe);
or `python maintainer/release.py build --repos SkylandersCoreOfLight`.

## Status convention

`docs/roadmap.md` is the overview (phases = milestones, "now / next / later"); update it whenever an
issue is closed or the plan changes, and keep the README's games table in step.
