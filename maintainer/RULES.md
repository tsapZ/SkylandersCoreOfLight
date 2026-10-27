# Rules for the Skylanders repositories

These hold in all of them: the six game ports (SSARecomp, GiantsRecomp, SwapForceRecomp,
TrapTeamRecomp, SuperChargersRecomp, ImaginatorsRecomp), Core of Light (SkylandersCoreOfLight) and
the two shared forks, `tsapZ/skylanders-portal` (branch `feature/portal-menu`) and
`tsapZ/rexglue-sdk` (branch `skylanders`). Every repository's CLAUDE.md imports this file; what is
special to one repository stays in its own CLAUDE.md.

## Layout

- One folder per repository, each holding its checkout: `<Tools>\<Repo>\<Repo>`. The maintainer
  tools in this folder find everything from there (`SKYLANDERS_TOOLS` overrides it).
- The shared libraries are edited in one checkout, Spyro's Adventure's `thirdparty/skylanders-portal`
  and `thirdparty/rexglue-sdk`, then the other repositories move their submodule to that commit
  (`release.py bump`). Each SDK fix also gets its own `fix/<name>` branch, cherry-picked in a
  separate worktree, so it can be offered upstream one day.
- Submodules (`thirdparty/`) change only on clearly named own branches; nothing is pushed to
  TheBiemGamer's or upstream repositories.

## Tests never touch the owner's data

- A scripted or experimental game run always gets its own `--user_data_root`; never the real
  `Documents\Skylanders\<game>` or `Documents\Skylanders\Figures`. A run that loads a save starts
  from a **copy of the owner's saves** (their intros are done, so the run starts in a hub or level);
  figures come from a copy of the figure folder. `test/smoke.ps1` and `test/crash_hunt.ps1` do both.
- Always `--portal_mode=software` in scripts (a settings file may say `usb`, and the real portal
  would get the test's writes), `--audio_mute=true` at night (only zeroes the samples).
- Never pass a setting with an empty value (`--name=`): the game then drops its whole command line,
  `--user_data_root` included, and runs on the real data.
- Launcher tests run with `--offline` and a scratch `--documents`, with stand-in setups: a real setup
  run would point the owner's launcher at the scratch folder. No launcher tests during a game run.
- Input goes through the game's input scripts (a virtual controller), not real key presses; those
  land in whatever window is in front.
- The owner's own settings files change only when asked.

## Git and GitHub

- Repo-local git identity (`git config user.name/user.email`, the GitHub noreply address), never
  global. Conventional Commits (`feat:`, `fix:`, `docs:`, `build:`), small commits.
- Commit messages come from a file (`git commit -F <file>`), written with an editor that writes
  UTF-8 without a BOM. No `Co-Authored-By` or session lines in commits, no "Generated with" footers
  or session links in issues, comments, PRs or release notes.
- Before every commit `git status`: no `rom/` content, `*.xex`, `*.iso`, figure dumps or other game
  files. No game screenshots or artwork anywhere in the repositories or issues (they belong to
  Activision). `release.py check` covers this for a release.
- No personal information about the owner in files, commit messages or docs.
- Public text (READMEs, issue forms, release notes, comments) is English, short and plain, with
  the English menu names. No emojis.

## Shared code

- An idea for one game goes into all six unless it is truly game-specific; build it in
  `skylanders-portal` and wire it into every game. Only the game's own data (addresses, which file
  holds a picture) differs per game.
- No game depends on the launcher: each stays complete with its own exe and settings file.
- Every `config/default.toml` entry and every kernel stub gets a comment: what is at the address,
  how it was found, why the fix is safe.
- Python tools use only the standard library.

## Releases

- `python maintainer/release.py status | bump | version | build | check | publish` (see
  `maintainer/README.md`). Versions are `x.y.z-beta.N` with a one-digit N: GitHub sorts tags as
  text, so after beta.9 the minor version goes up.
- Every shipped exe and DLL is checked for build machine paths before upload (`check`).
- Release when something is done and tested; test first with `test/smoke_all.ps1`.

## Editing gotchas

- Patches with backslashes or non-ASCII characters: write the patch script to a file and run it;
  shell heredocs eat one level of backslashes and double-encode umlauts.
- PowerShell 5.1: `Set-Content -Encoding utf8` writes a BOM; variable names ignore case; `$args`
  is taken.
- The games' input script splits at every `;`, comments included.
