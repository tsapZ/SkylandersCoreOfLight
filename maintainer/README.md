# Maintainer tools

For working on all seven repositories at once: releases and test runs. They expect the developer
layout, one folder per repository holding its checkout (`<Tools>\SSARecomp\SSARecomp`, ...,
`<Tools>\SkylandersCoreOfLight\SkylandersCoreOfLight`), and write their logs and runs to
`%LOCALAPPDATA%\SkylandersDev` (`~/SkylandersDev` on Linux; `SKYLANDERS_WORK` changes that). The rules every repository follows
are in [RULES.md](RULES.md).

## release.py

| Step | What it does |
|---|---|
| `status` | per repository: branch, clean or not, ahead/behind origin, the version, the latest release, and the submodule commits (a `*` marks one that isn't the shared branch's tip) |
| `bump --sdk <sha> --lib <sha> -m <msg file> [--status "<line>"] [--push]` | moves the shared submodules to those commits in every repository that has them, with a STATUS entry, commits (and pushes) |
| `version <x.y.z-beta.N> [--repos ...] [--status "<line>"] [--push]` | sets the release version (a game's `installer/<Repo>.iss`, the launcher's `CMakeLists.txt`), commits (and pushes) |
| `build [--recipe package-installer\|package-linux] [--test]` | builds: the games with `just`, the launcher with CMake and `tools/package_installer.ps1` (Windows) or `tools/package_linux.sh` and `tools/package_appimage.sh` (Linux); a log per repository. On Linux it checks clang 19 and the development packages before the first build |
| `check [--history]` | what would stop a release: uncommitted changes, game data or figure files in git, the Windows user name in tracked files (or, with `--history`, in any commit), a private e-mail in a commit, a build machine path in a staged exe or DLL, the developer kit in a staged exe, or a private repository (the developer kit, GiantsDecomp) built into the release build folder |
| `publish --notes <folder> [--dry-run]` | the GitHub releases, each with its setup (the launcher: setup and portable ZIP). The notes come from `<folder>\<Repo>.md` and `common.md`; the install, problem and disclaimer paragraphs are added. Refuses what fails `check`, isn't pushed or is already released |

`--repos` takes repository names, `games` or `all` (the default). A release then goes:

    python maintainer/release.py bump --sdk <sha> --lib <sha> -m msg.txt --status "..." --push
    python maintainer/release.py version 0.3.0-beta.2 --repos games --status "..." --push
    python maintainer/release.py build --recipe package-installer --test
    powershell -File maintainer/test/smoke_all.ps1
    python maintainer/release.py publish --notes notes --dry-run
    python maintainer/release.py publish --notes notes

Test after the packaging build, not before it: every `build` runs the CMake configure again, which
writes the SDK's `rex/version.h` with a new time, so the game relinks and the tested exe isn't the
one that ships.

### Windows launcher signing and antivirus reports

On the Windows build machine, make the Windows SDK's `signtool.exe` available on `PATH`. Use a
trusted code-signing certificate with an accessible private key in the current user's Personal
certificate store. In the build PowerShell session, set its thumbprint and package the launcher:

```powershell
$env:COREOFLIGHT_SIGNING_THUMBPRINT = '<40 hexadecimal digits from the certificate thumbprint>'
python maintainer/release.py build --repos SkylandersCoreOfLight --recipe package-installer
```

Inno Setup timestamps and signs the setup, uninstaller and staged `coreoflight.exe`. The portable
ZIP uses that same signed executable. The build stops if signing or signature verification fails.
With the thumbprint unset, packaging produces an unsigned build.

For a reported detection, match the SHA-256 against the exact release asset or the extracted file
named in the report. After checking that file against the release build, submit it to each detecting
vendor for review with the release URL, hash and detection name. Reanalyse the same hash once the
vendor responds. [VirusTotal guidance](https://docs.virustotal.com/docs/false-positive) and
[Microsoft's file submission portal](https://www.microsoft.com/en-us/wdsi/filesubmission) explain the process.

## Linux

The same commands run on Linux; `SKYLANDERS_WORK` then defaults to `~/SkylandersDev`. The launcher
links SDL3 dynamically (the release tarball and AppImage carry it next to the binary), and Ubuntu
24.04 and Debian 12 have no SDL3 package, so build SDL 3.4.20 first and pass its prefix
(`-DCMAKE_PREFIX_PATH=/path/to/sdl3`) if it isn't installed system-wide. The games need clang 19 or
newer: Ubuntu 24.04's clang 18 with libstdc++ 13 hides the `std::expected` the SDK's
`src/system/map_parser` uses. Both need X11-xcb and the usual development packages. On Ubuntu 24.04:

    sudo apt install clang-19 g++ curl file libx11-xcb-dev libusb-1.0-0-dev libcurl4-openssl-dev \
        libssl-dev libarchive-dev libgl1-mesa-dev libudev-dev pkg-config cmake ninja-build
    python3 maintainer/release.py build --repos SkylandersCoreOfLight --recipe package-linux

The release tool uses clang 19 or newer for the games and also accepts Ubuntu's versioned
`clang-19` and `clang++-19` executables.

Build the release on the oldest distribution you support for that architecture (Ubuntu 24.04 here
for x86-64): the binaries and the bundled SDL3 link against its system libraries. For ARM64, build
on the oldest arm64 distribution you support; on a Steam Frame, `sudo steamos-devmode enable` first
and install the Arch equivalents of the packages above (`clang cmake ninja pkgconf libusb
libarchive curl openssl mesa libglvnd vulkan-icd-loader`), plus SDL 3.4.20 from source if the
distro has no 3.4 package.

`build --recipe package-linux` writes the games' `dist/<Repo>-linux-<arch>.tar.gz`, the launcher's
`installer/Output/CoreOfLight-linux-<arch>.tar.gz` (with `libSDL3.so.0` beside `coreoflight`), and
`installer/Output/CoreOfLight-<version>-<x86_64|aarch64>.AppImage`, where the architecture comes from
the build machine (`linux-amd64-release` or `linux-arm64-release`). appimagetool 1.9.1 and the Type 2
runtime 20251108 are cached in `$SKYLANDERS_WORK/tools` and checked against fixed SHA-256 hashes for
both architectures. Use `APPIMAGETOOL` and `APPIMAGE_RUNTIME` to supply your own copies. An AppImage
runs from a read-only mount and doesn't update itself: it opens the release page instead. `publish`
uploads every Linux asset built into the output folders, so one release can carry amd64 and arm64
next to each other, and the release notes describe both tarballs and both AppImages.

## tools/

| Script | What it does |
|---|---|
| `find_mojibake.py <root> ... [--fix]` | double-encoded UTF-8 (an umlaut shown as two odd characters) in the text files, after a patch went through a shell |
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
| `selftest.ps1` | the tools themselves, a few seconds: every PowerShell script parses, every Python script compiles, every list parameter splits a comma list (`powershell -File` hands "A,B" over as one string); before a release |
| `smoke_all.ps1` | `smoke.ps1` for all six games with their usual route, about 15 minutes; after anything that touches every game. `-Vulkan` runs each route again on Vulkan and keeps both sets of pictures (`runs\smoke\<Repo>_d3d12`, `_vulkan`) to compare: use it when the SDK or a GPU default changes |
| `crash_hunt.ps1 -Repo <Repo> -Route <route> [-Seconds 1800] [-Seed 2]` | a route, then random play for that long (`wander.py`), a shot every minute, the threads' call chains every three |
| `crash_all.ps1 [-Minutes 20] [-Seed 1]` | `crash_hunt.ps1` for all six on their usual routes, one after the other (about 2.5 hours at 20 minutes); for a release candidate |
| `game_keys.ps1 -Repo <Repo> -Keys "wait:3000,0x73,shot:f4"` | real key presses for what the virtual controller can't reach (ImGui fields, the keyboard binds); stops when another window is in front, and when the game's log doesn't name the scratch user data folder |
| `hub_keys.ps1 -Docs <scratch> -Out <dir> -Keys "F,wait:700,shot:figures"` | the launcher, always `--offline` on a scratch Documents folder, driven by posted keys and mouse steps, with shots |

Real key presses and `rclick` reach whatever window is in front: only while nobody uses the PC.

`routes/` holds the routes: input scripts with `{figures}` for the copied figure folder, and header
lines: `# save: yes` (start from a copy of the owner's saves), `# shots: ...` (when the shots are
taken), `# expect: <log text>, ...` (lines the log must have, else the verdict says MISSING) and
`# reject: <log text>, ...` (lines it must not have, e.g. a fixed bug's message: REJECTED). A
`log shot <name>` line in a route takes a window shot one second later (the test kit adds up the
waits before it). smoke_all and crash_all continue the owner's saves, from where he is: Spyro's
Adventure slot 4 (the Ruins), Giants slot 4, Swap Force slot 3 (Woodburrow; slot 1 is empty and
starts a new game), Trap Team slot 1 (the Academy), SuperChargers (the Academy), Imaginators slot 1
(the overland map; no Y on its slot screen, Y is Delete there). `ssa_new` and `im_new` start new
games, for a check of the intros now and then. `menus` goes on
top of any of them (`smoke.ps1 -Then menus`, `smoke_all.ps1 -Menus`): it opens, shoots and closes
the portal menu and its next tab, the portal overview, the settings and their next page, the
achievements and the player menu.
