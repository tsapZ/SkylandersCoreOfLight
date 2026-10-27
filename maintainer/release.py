"""release.py: one tool for the steps every release of the six games and the launcher goes through.

    python maintainer/release.py status
    python maintainer/release.py bump --sdk <sha> --lib <sha> -m <message file> [--status "<line>"] [--push]
    python maintainer/release.py version 0.3.0-beta.1 [--repos ...] [--status "<line>"] [--push]
    python maintainer/release.py build [--recipe build-release|package-installer] [--test] [--repos ...]
    python maintainer/release.py check [--history] [--repos ...]
    python maintainer/release.py publish --notes <folder> [--repos ...] [--dry-run]

--repos takes repository names (SSARecomp ... SkylandersCoreOfLight) or "games" / "all"; the default
is all seven (bump: the repositories that have the submodule). Commit messages come from a file
(-m), never from the command line, so umlauts and backslashes arrive as written.

publish wants a folder with the release notes: <Repo>.md (what changed in that game) and/or
common.md (what changed everywhere), in that order; the installing, reporting and disclaimer
paragraphs are added here. It refuses a repository that fails `check`, isn't pushed, or already
has the tag.
"""
import argparse
import datetime
import glob
import json
import os
import pathlib
import re
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import skylanders as sk  # noqa: E402

GH = "tsapZ"


def pick_repos(names, default=None):
    if not names:
        return list(default or sk.REPOS)
    out = []
    for n in names:
        if n == "all":
            out += sk.REPOS
        elif n == "games":
            out += [g.repo for g in sk.GAMES]
        elif n in sk.REPOS:
            out.append(n)
        else:
            raise SystemExit(f"unknown repository {n!r}; one of: {', '.join(sk.REPOS)}, games, all")
    return list(dict.fromkeys(out))


def run(cmd, cwd=None, log=None):
    """A command; its output goes to `log` (a path) if given. Returns the exit code."""
    if log:
        log.parent.mkdir(parents=True, exist_ok=True)
        with open(log, "w", encoding="utf-8", errors="replace") as f:
            return subprocess.run(cmd, cwd=cwd, stdout=f, stderr=subprocess.STDOUT).returncode
    return subprocess.run(cmd, cwd=cwd).returncode


def gh(*args):
    r = subprocess.run(["gh", *args], capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode, r.stdout.strip()


def is_clean(repo):
    return not sk.git(repo, "status", "--porcelain")


def write_keeping_format(path, edit):
    """Rewrites a text file through `edit(text) -> text`, keeping its BOM and line endings."""
    raw = path.read_bytes()
    bom = raw.startswith(b"\xef\xbb\xbf")
    text = raw.decode("utf-8-sig")
    crlf = "\r\n" in text
    new = edit(text.replace("\r\n", "\n"))
    if crlf:
        new = new.replace("\n", "\r\n")
    path.write_bytes((b"\xef\xbb\xbf" if bom else b"") + new.encode("utf-8"))


def add_status_entry(repo, title, line):
    """A dated entry at the end of docs/STATUS.md (the launcher has none)."""
    status = sk.repo_dir(repo) / "docs" / "STATUS.md"
    if not status.exists():
        return None
    today = datetime.date.today().isoformat()
    write_keeping_format(status, lambda t: t.rstrip("\n") + f"\n\n## {today}: {title}\n\n- {line}\n")
    return "docs/STATUS.md"


# --- status -------------------------------------------------------------------------------------

def remote_tip(slug, branch):
    out = subprocess.run(["git", "ls-remote", f"https://github.com/{slug}.git", f"refs/heads/{branch}"],
                         capture_output=True, text=True).stdout.split()
    return out[0] if out else None


def cmd_status(a):
    tips = {path: remote_tip(slug, branch) for path, (slug, branch) in sk.SUBMODULES.items()}
    print("%-22s %-6s %-5s %-9s %-14s %-16s %-11s %-11s" % (
        "repository", "branch", "clean", "vs origin", "version", "latest release", "rexglue-sdk", "portal lib"))
    for repo in pick_repos(a.repos):
        d = sk.repo_dir(repo)
        if not d.exists():
            print(f"{repo:22} (no checkout at {d})")
            continue
        sk.git(repo, "fetch", "-q", "origin", check=False)
        branch = sk.git(repo, "branch", "--show-current")
        counts = sk.git(repo, "rev-list", "--left-right", "--count", "HEAD...@{u}", check=False).split()
        vs = f"+{counts[0]}/-{counts[1]}" if len(counts) == 2 else "?"
        code, latest = gh("release", "list", "-R", f"{GH}/{repo}", "--limit", "1", "--json", "tagName",
                          "--jq", ".[0].tagName")
        pins = []
        for path in sk.SUBMODULES:
            entry = sk.git(repo, "ls-tree", "HEAD", path, check=False).split()
            if len(entry) < 3:
                pins.append("-")
                continue
            sha = entry[2]
            pins.append(sha[:7] + ("" if sha == tips[path] else "*"))
        print("%-22s %-6s %-5s %-9s %-14s %-16s %-11s %-11s" % (
            repo, branch, "yes" if is_clean(repo) else "NO", vs, sk.read_version(repo) or "?",
            latest if code == 0 else "?", pins[0], pins[1]))
    print("* = not the tip of the shared branch "
          + ", ".join(f"{slug} {branch} ({(tips[p] or '?')[:7]})" for p, (slug, branch) in sk.SUBMODULES.items()))


# --- bump ---------------------------------------------------------------------------------------

def cmd_bump(a):
    wanted = {"thirdparty/rexglue-sdk": a.sdk, "thirdparty/skylanders-portal": a.lib}
    wanted = {p: s for p, s in wanted.items() if s}
    if not wanted:
        raise SystemExit("give --sdk and/or --lib")
    pathlib.Path(a.message).read_text(encoding="utf-8")  # fails early if the file is missing
    for repo in pick_repos(a.repos):
        d = sk.repo_dir(repo)
        paths = [p for p in wanted if (d / p / ".git").exists()]
        if not paths:
            continue
        if not is_clean(repo):
            raise SystemExit(f"{repo}: not clean, nothing done there")
        for p in paths:
            slug, branch = sk.SUBMODULES[p]
            sub = d / p
            sk.git(sub, "fetch", "-q", f"https://github.com/{slug}.git", branch)
            sk.git(sub, "checkout", "-q", "--detach", wanted[p])
            sk.git(repo, "add", p)
        changed = sk.git(repo, "diff", "--cached", "--name-only")
        if not changed:
            print(f"{repo}: already there")
            continue
        if a.status:
            entry = add_status_entry(repo, a.status_title or "shared libraries", a.status)
            if entry:
                sk.git(repo, "add", entry)
        sk.git(repo, "commit", "-q", "-F", str(pathlib.Path(a.message).resolve()))
        if a.push:
            sk.git(repo, "push", "-q", "origin", "HEAD")
        print(f"{repo}: {', '.join(paths)} -> {sk.git(repo, 'log', '-1', '--format=%h')}"
              + (" (pushed)" if a.push else ""))


# --- version ------------------------------------------------------------------------------------

def cmd_version(a):
    if not sk.VERSION_RE.match(a.version):
        raise SystemExit(f"{a.version}: want x.y.z or x.y.z-beta.N with a one-digit N")
    msg_file = pathlib.Path(a.message).resolve() if a.message else None
    for repo in pick_repos(a.repos):
        if not is_clean(repo):
            raise SystemExit(f"{repo}: not clean, nothing done there")
        path, pattern = sk.version_file(repo)
        old = sk.read_version(repo)
        if old == a.version:
            print(f"{repo}: already {a.version}")
            continue
        write_keeping_format(path, lambda t: pattern.sub(lambda m: m.group(1) + a.version + m.group(3), t, count=1))
        files = [str(path.relative_to(sk.repo_dir(repo)))]
        if a.status:
            entry = add_status_entry(repo, a.version, "The setup's version for what came since the last one: "
                                     + a.status)
            if entry:
                files.append(entry)
        sk.git(repo, "add", *files)
        if msg_file:
            sk.git(repo, "commit", "-q", "-F", str(msg_file))
        else:
            tmp = sk.WORK / "release" / "version_msg.txt"
            tmp.parent.mkdir(parents=True, exist_ok=True)
            tmp.write_text(f"{a.version}\n\nThe setup's version and a STATUS entry for what came since the last\n"
                           "release (see STATUS).\n", encoding="utf-8")
            sk.git(repo, "commit", "-q", "-F", str(tmp))
        if a.push:
            sk.git(repo, "push", "-q", "origin", "HEAD")
        print(f"{repo}: {old} -> {a.version} ({sk.git(repo, 'log', '-1', '--format=%h')})"
              + (" pushed" if a.push else ""))


# --- build --------------------------------------------------------------------------------------

def vcvars():
    found = sorted(glob.glob(r"C:\Program Files\Microsoft Visual Studio\2022\*\VC\Auxiliary\Build\vcvars64.bat"))
    if not found:
        raise SystemExit("vcvars64.bat of Visual Studio 2022 not found")
    return found[-1]


def in_vs(command):
    # One command line, not a list: Python would escape the inner quotes with backslashes, which
    # cmd doesn't read. /s takes the outer quotes off and runs the rest as written.
    return f'cmd /d /s /c "call "{vcvars()}" >nul && {command}"'


def cmd_build(a):
    logs = sk.WORK / "logs"
    failed = []
    for repo in pick_repos(a.repos):
        d = sk.repo_dir(repo)
        steps = []
        if repo == sk.HUB:
            steps.append(("build", in_vs("cmake --build --preset release")))
            if a.recipe == "package-installer":
                steps.append(("package", ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                          str(d / "tools" / "package_installer.ps1")]))
        else:
            steps.append((a.recipe, in_vs(f"just {a.recipe}")))
            if a.test:
                steps.append(("test", in_vs("just test-release")))
        for name, command in steps:
            log = logs / f"{repo}_{name}.log"
            started = datetime.datetime.now()
            code = run(command, cwd=d, log=log)
            took = (datetime.datetime.now() - started).total_seconds()
            print(f"{repo}: {name} {'ok' if code == 0 else 'FAILED'} in {took:.0f} s ({log})")
            if code:
                failed.append(repo)
                break
    if failed:
        raise SystemExit("failed: " + ", ".join(failed))


# --- check --------------------------------------------------------------------------------------

FORBIDDEN = re.compile(r"(^rom/(?!README\.md$).+|\.(xex|iso|dump|bin|igz|iga|bk2)$)", re.I)


def check_repo(repo, history=False):
    """The problems that stop a release of `repo`, as a list of strings."""
    d = sk.repo_dir(repo)
    user = os.environ.get("USERNAME") or os.environ.get("USER") or ""
    problems = []
    if not is_clean(repo):
        problems.append("uncommitted changes")
    tracked = sk.git(repo, "ls-files").splitlines()
    bad = [p for p in tracked if FORBIDDEN.search(p)]
    if bad:
        problems.append("game data or figure files tracked: " + ", ".join(bad[:5]))
    if user:
        # Vendored third-party files are left out: their credits can carry anyone's first name.
        hits = sk.git(repo, "grep", "-I", "-i", "-l", "-w", user, "--", ".", ":!thirdparty", check=False)
        if hits:
            problems.append(f"the Windows user name in tracked files: {hits.splitlines()[:5]}")
        if history:
            commits = sk.git(repo, "log", "--all", "--format=%h", "-i", f"-G{re.escape(user)}", "--", ".",
                             ":!thirdparty", check=False)
            if commits:
                problems.append(f"the Windows user name in the history: commits {commits.split()[:5]}")
    emails = set(sk.git(repo, "log", "--all", "--format=%ae%n%ce").split())
    private = [e for e in emails if not e.endswith("users.noreply.github.com") and e != "noreply@github.com"]
    if private:
        problems.append(f"commits with a non-noreply e-mail: {sorted(private)}")
    staging = d / "installer" / "staging"
    if user and staging.exists():
        needles = [f"Users\\{user}", f"Users/{user}"]
        for f in staging.rglob("*"):
            if f.suffix.lower() not in (".exe", ".dll"):
                continue
            data = f.read_bytes().lower()
            for n in needles:
                if n.lower().encode("ascii") in data or n.lower().encode("utf-16-le") in data:
                    problems.append(f"{f.name} holds a build machine path ({n})")
                    break
    return problems


def cmd_check(a):
    bad = 0
    for repo in pick_repos(a.repos):
        problems = check_repo(repo, a.history)
        bad += bool(problems)
        print(f"{repo}: " + ("ok" if not problems else "; ".join(problems)))
    if bad:
        raise SystemExit(1)


# --- publish ------------------------------------------------------------------------------------

def footer(repo, version):
    if repo == sk.HUB:
        return (
            f"**Install:** either download `CoreOfLightSetup-{version}.exe` and run it (no admin rights needed), "
            "or take the ZIP above. Neither is signed, so Windows may show \"Windows protected your PC\": click "
            "\"More info\" > \"Run anyway\".\n\n"
            "On first start it asks for your own Xbox 360 discs as ISO files and installs the games for them, "
            "downloading each game's setup from its repository. Nothing of the games comes with this project.\n\n"
            "This is a beta: expect bugs. Please report them with Launcher settings (LB / Q) > \"Report a problem\" "
            f"and a [new issue](https://github.com/{GH}/{repo}/issues/new/choose) (attach the text file it makes).\n\n"
            "Not affiliated with Activision, Toys for Bob or Vicarious Visions. Skylanders is a trademark of its owners.\n")
    g = sk.game(repo)
    return (
        f"**The easy way:** install [Core of Light](https://github.com/{GH}/{sk.HUB}/releases), the launcher. It "
        "sets this game up from your disc and keeps it up to date by itself.\n\n"
        f"**On its own:** run `{repo}Setup-{version}.exe` and point it at your own Xbox 360 disc (an ISO file or "
        "an extracted folder). It's not signed, so Windows may show \"Windows protected your PC\": \"More info\" > "
        "\"Run anyway\". The README says which disc version it needs.\n\n"
        "The game's data (graphics, sound, levels, movies) doesn't come with it: the setup takes it from your own "
        "disc.\n\n"
        f"Problems: F4 > Game > \"Report a problem\" in the game, then a [new issue](https://github.com/{GH}/{repo}"
        "/issues/new/choose).\n\n"
        f"Not affiliated with Activision or {g.maker}. Skylanders is a trademark of its owners.\n")


def notes_for(repo, version, folder):
    head = (f"Core of Light, the launcher for the six recompiled Skylanders games. {version}"
            if repo == sk.HUB else f"{sk.game(repo).title}, {version}")
    head += " (test version)." if "-beta" in version else "."
    parts = [head]
    for name in (f"{repo}.md", "common.md"):
        p = folder / name
        if p.exists():
            parts.append(p.read_text(encoding="utf-8").strip())
    if len(parts) == 1:
        raise SystemExit(f"{repo}: neither {repo}.md nor common.md in {folder}")
    parts.append(footer(repo, version).strip())
    return "\n\n".join(parts) + "\n"


def cmd_publish(a):
    folder = pathlib.Path(a.notes).resolve()
    plan = []
    for repo in pick_repos(a.repos):
        version = sk.read_version(repo)
        if not version or not sk.VERSION_RE.match(version):
            raise SystemExit(f"{repo}: version {version!r} isn't a release version")
        problems = check_repo(repo)
        if problems:
            raise SystemExit(f"{repo}: " + "; ".join(problems))
        sk.git(repo, "fetch", "-q", "origin")
        if sk.git(repo, "rev-parse", "HEAD") != sk.git(repo, "rev-parse", "@{u}"):
            raise SystemExit(f"{repo}: HEAD isn't what origin has; push first")
        if gh("release", "view", f"v{version}", "-R", f"{GH}/{repo}")[0] == 0:
            raise SystemExit(f"{repo}: v{version} is already released")
        assets = sk.release_assets(repo, version)
        missing = [str(p) for p in assets if not p.exists()]
        if missing:
            raise SystemExit(f"{repo}: missing {missing}; build with --recipe package-installer")
        plan.append((repo, version, assets, notes_for(repo, version, folder)))
    for repo, version, assets, notes in plan:
        notes_file = sk.WORK / "release" / f"notes_{repo}_{version}.md"
        notes_file.parent.mkdir(parents=True, exist_ok=True)
        notes_file.write_text(notes, encoding="utf-8")
        cmd = ["gh", "release", "create", f"v{version}", *map(str, assets), "-R", f"{GH}/{repo}",
               "--title", sk.release_title(repo, version), "-F", str(notes_file), "--target", "main"]
        if "-beta" in version:
            cmd.append("--prerelease")
        if a.dry_run:
            print(f"--- {repo} {version}: would run\n  {' '.join(cmd)}\n{notes}")
            continue
        code = run(cmd)
        print(f"{repo}: v{version} {'released' if code == 0 else 'FAILED'}")
        if code:
            raise SystemExit(1)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("status", help="versions, releases, submodule pins and git state")
    s.add_argument("--repos", nargs="*")
    s = sub.add_parser("bump", help="move the shared submodules to a commit, commit (and push)")
    s.add_argument("--sdk")
    s.add_argument("--lib")
    s.add_argument("-m", "--message", required=True, help="commit message file")
    s.add_argument("--status", help="a STATUS line for the change")
    s.add_argument("--status-title", help="the STATUS entry's title (default: shared libraries)")
    s.add_argument("--push", action="store_true")
    s.add_argument("--repos", nargs="*")
    s = sub.add_parser("version", help="set the release version, commit (and push)")
    s.add_argument("version")
    s.add_argument("--status", help="what came since the last release, for the STATUS entry")
    s.add_argument("-m", "--message", help="commit message file (default: the version)")
    s.add_argument("--push", action="store_true")
    s.add_argument("--repos", nargs="*")
    s = sub.add_parser("build", help="build in the Visual Studio environment, logs in SKYLANDERS_WORK")
    s.add_argument("--recipe", default="build-release", choices=["build-release", "package-installer"])
    s.add_argument("--test", action="store_true", help="also run the games' tests")
    s.add_argument("--repos", nargs="*")
    s = sub.add_parser("check", help="what would stop a release: game data, user names, build paths")
    s.add_argument("--history", action="store_true", help="also search every commit for the user name")
    s.add_argument("--repos", nargs="*")
    s = sub.add_parser("publish", help="GitHub releases with the built setups")
    s.add_argument("--notes", required=True, help="folder with <Repo>.md and/or common.md")
    s.add_argument("--dry-run", action="store_true")
    s.add_argument("--repos", nargs="*")
    a = p.parse_args()
    {"status": cmd_status, "bump": cmd_bump, "version": cmd_version, "build": cmd_build,
     "check": cmd_check, "publish": cmd_publish}[a.cmd](a)


if __name__ == "__main__":
    main()
