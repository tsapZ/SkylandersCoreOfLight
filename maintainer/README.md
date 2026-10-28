# Maintainer tools

For working on all seven repositories at once: releases and test runs. They expect the developer
layout, one folder per repository holding its checkout (`<Tools>\SSARecomp\SSARecomp`, ...,
`<Tools>\SkylandersCoreOfLight\SkylandersCoreOfLight`), and write their logs and runs to
`%LOCALAPPDATA%\SkylandersDev` (`SKYLANDERS_WORK` changes that). The rules every repository follows
are in [RULES.md](RULES.md).

## release.py

| Step | What it does |
|---|---|
| `status` | per repository: branch, clean or not, ahead/behind origin, the version, the latest release, and the submodule commits (a `*` marks one that isn't the shared branch's tip) |
| `bump --sdk <sha> --lib <sha> -m <msg file> [--status "<line>"] [--push]` | moves the shared submodules to those commits in every repository that has them, with a STATUS entry, commits (and pushes) |
| `version <x.y.z-beta.N> [--repos ...] [--status "<line>"] [--push]` | sets the release version (a game's `installer/<Repo>.iss`, the launcher's `CMakeLists.txt`), commits (and pushes) |
| `build [--recipe package-installer] [--test]` | builds in the Visual Studio environment: the games with `just`, the launcher with CMake and `tools/package_installer.ps1`; a log per repository |
| `check [--history]` | what would stop a release: uncommitted changes, game data or figure files in git, the Windows user name in tracked files (or, with `--history`, in any commit), a private e-mail in a commit, a build machine path in a staged exe or DLL, the developer kit in a staged exe, or a private repository (the developer kit, GiantsDecomp) built into the release build folder |
| `publish --notes <folder> [--dry-run]` | the GitHub releases, each with its setup (the launcher: setup and portable ZIP). The notes come from `<folder>\<Repo>.md` and `common.md`; the install, problem and disclaimer paragraphs are added. Refuses what fails `check`, isn't pushed or is already released |

`--repos` takes repository names, `games` or `all` (the default). A release then goes:

    python maintainer/release.py bump --sdk <sha> --lib <sha> -m msg.txt --status "..." --push
    python maintainer/release.py version 0.3.0-beta.2 --repos games --status "..." --push
    python maintainer/release.py build --recipe package-installer --test
    powershell -File maintainer/test/smoke_all.ps1
    python maintainer/release.py publish --notes notes --dry-run
    python maintainer/release.py publish --notes notes

## tools/

| Script | What it does |
|---|---|
| `find_mojibake.py <root> ... [--fix]` | double-encoded UTF-8 ("Ã¼" for "ü") in the text files, after a patch went through a shell |
| `find_late_starts.py <code.bin> <code base> <codegen.partition.json>` | functions the analyzer starts one instruction late ("Call to invalid or unregistered function"): the code section saved with the input script's `save`, the starts to register under `[functions]` |
| `leak_scan.py <mirror dir> <repo> [--word W ...]` | every file version of a mirror clone searched for personal words (default: the Windows user name), ASCII and UTF-16, and the commit identities; run before calling a repository clean |

The RenderDoc analysis and the Ghidra wrapper are in skylanders-portal's `tools/`, run through each
game's `just rd` and `just ghidra`.

## test/

Scripted runs of the built games (`out\build\win-amd64-release`), driven by the games' input scripts
(a virtual controller; no key presses reach other windows). Each run gets its own user data folder,
a copy of the owner's saves where the route wants one, and a copy of the figure folder: the real
saves and figures are never touched.

| Script | What it does |
|---|---|
| `smoke.ps1 -Repo <Repo> -Route <route> [-Build devkit]` | one run along a route with window shots, then one verdict line: crash or trouble lines, whether the script reached its end, the FPS samples. `-Build devkit` runs the developer kit's build (`out\build\win-amd64-devkit`, the private SkylandersDevKit: level loading and level sweeps) |
| `smoke_all.ps1` | `smoke.ps1` for all six games with their usual route, about 15 minutes; after anything that touches every game |
| `crash_hunt.ps1 -Repo <Repo> -Route <route> [-Seconds 1800] [-Seed 2]` | a route, then random play for that long (`wander.py`), a shot every minute, the threads' call chains every three |
| `crash_all.ps1 [-Minutes 20] [-Seed 1]` | `crash_hunt.ps1` for all six on their usual routes, one after the other (about 2.5 hours at 20 minutes); for a release candidate |
| `game_keys.ps1 -Repo <Repo> -Keys "wait:3000,0x73,shot:f4"` | real key presses for what the virtual controller can't reach (ImGui fields, the keyboard binds); stops when another window is in front, and when the game's log doesn't name the scratch user data folder |
| `hub_keys.ps1 -Docs <scratch> -Out <dir> -Keys "F,wait:700,shot:figures"` | the launcher, always `--offline` on a scratch Documents folder, driven by posted keys and mouse steps, with shots |

Real key presses and `rclick` reach whatever window is in front: only while nobody uses the PC.

`routes/` holds the routes: input scripts with `{figures}` for the copied figure folder, and header
lines: `# save: yes` (start from a copy of the owner's saves), `# shots: ...` (when the shots are
taken) and `# expect: <log text>, ...` (lines the log must have, else the verdict says MISSING). A
`log shot <name>` line in a route takes a window shot one second later (the test kit adds up the
waits before it). `ssa_new` and `im_new` start new games; the others continue the owner's saves
(Giants' slot 4, Swap Force's and Trap Team's slot 1, SuperChargers on the ship). `menus` goes on
top of any of them (`smoke.ps1 -Then menus`, `smoke_all.ps1 -Menus`): it opens, shoots and closes
the portal menu and its next tab, the portal overview, the settings and their next page, the
achievements and the player menu.
