# CLAUDE.md: Skylanders: Core of Light

## Goal

The hub for the recompiled Skylanders games (SSARecomp, GiantsRecomp, SwapForceRecomp, later Trap
Team, SuperChargers, Imaginators): launcher, figure library, profiles, online co-op. The
launcher's first version exists (docs/launcher.md, src/); see README.md and docs/roadmap.md.

## Build

From a Visual Studio 2022 x64 developer prompt: `git submodule update --init`, then
`cmake --preset release` and `cmake --build --preset release` (out/build/release/coreoflight.exe).
Tests that start a game use a stand-in exe or a copied `--user_data_root`, never the owner's saves.

## Rules

- Analyse first, then build. When unclear, ask instead of guessing.
- Small commits, Conventional Commits (`feat:`, `fix:`, `docs:`).
- Never commit game data, secrets or tokens: no `*.iso`, `*.xex`, figure dumps or game files.
  Check `git status` before every commit.
- No personal information about the owner in files, commit messages or docs.
- Git identity is repo-local (`git config user.name/user.email`), never global.
- **No game may depend on the hub.** Each game stays complete with its own exe; the hub starts it
  and writes its ordinary settings file. Code both need goes into `tsapZ/skylanders-portal`.
- No original artwork: pictures and texts come from the owner's game files at run time.
- New behaviour switches get conservative defaults.
- Do not modify submodules except on clearly named own branches.
- Tests never touch the owner's real saves or figures: copied `--user_data_root`, scratch figure
  folders.

## Status convention

`docs/roadmap.md` is the overview (phases = milestones, "now / next / later"); update it whenever an
issue is closed or the plan changes, and keep the README table in step.
