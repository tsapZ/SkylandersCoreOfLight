"""release.py: one tool for the steps every release of the six games and the launcher goes through.

    python maintainer/release.py status
    python maintainer/release.py bump --sdk <sha> --lib <sha> -m <message file> [--status "<line>"] [--push]
    python maintainer/release.py version 0.3.0-beta.1 [--repos ...] [--status "<line>"] [--push]
    python maintainer/release.py build [--recipe build-release|package-installer] [--test] [--pgo] [--repos ...]
    python maintainer/release.py check [--history] [--repos ...]
    python maintainer/release.py publish --notes <folder> [--platforms windows linux|all] [--repos ...] [--dry-run]

--repos takes repository names (SSARecomp ... SkylandersCoreOfLight) or "games" / "all"; the default
is all seven (bump: the repositories that have the submodule). Commit messages come from a file
(-m), never from the command line, so umlauts and backslashes arrive as written.

publish wants a folder with the release notes: <Repo>.md (what changed in that game) and/or
common.md (what changed everywhere), in that order; the installing, reporting and disclaimer
paragraphs are added here. It refuses a repository that fails `check`, isn't pushed, or already
has the tag. The Linux tarballs have no version in their name, so `build` writes a `.provenance`
sidecar (version and SHA-256) next to each and `publish` refuses one that is missing, names another
version, or whose digest no longer matches.
"""
import argparse
import datetime
import glob
import json
import os
import pathlib
import platform
import re
import shutil
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


def run(cmd, cwd=None, log=None, path_first=None):
    """A command; its output goes to `log` (a path) if given; `path_first` goes in front of PATH.
    Returns the exit code."""
    env = None
    if path_first:
        env = dict(os.environ, PATH=str(path_first) + os.pathsep + os.environ.get("PATH", ""))
    if log:
        log.parent.mkdir(parents=True, exist_ok=True)
        with open(log, "w", encoding="utf-8", errors="replace") as f:
            return subprocess.run(cmd, cwd=cwd, stdout=f, stderr=subprocess.STDOUT, env=env).returncode
    return subprocess.run(cmd, cwd=cwd, env=env).returncode


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
        # Changes other than these submodules (moved there already for a local test) stop it.
        # (sk.git strips the output, so the first line may have lost the status's leading blank)
        other = [l for l in sk.git(repo, "status", "--porcelain").splitlines()
                 if l.split(None, 1)[-1] not in paths]
        if other:
            raise SystemExit(f"{repo}: not clean ({other[:3]}), nothing done there")
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
            # --status is the list of what came; a text that already starts with the lead-in keeps one.
            lead = "The setup's version for what came since the last one: "
            text = a.status[len(lead):] if a.status.startswith(lead) else a.status
            entry = add_status_entry(repo, a.version, lead + text)
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

# The steps that write an unversioned Linux tarball: the launcher's package_linux.sh and a game's
# `just package-linux`.
TARBALL_STEPS = {"package", "package-linux"}


def remove_stale_package_output(repo, d, arch):
    """Drop this architecture's old tarball and sidecar before packaging writes them again.

    A failed or skipped package step then cannot leave last run's file to be published. Tarballs of
    the other architecture are left alone: they may have been built on another machine, with their
    own sidecar.
    """
    for archive in sk.linux_tarballs(repo, d):
        if not archive.name.endswith(f"-linux-{arch}.tar.gz"):
            continue
        for path in (archive, sk.provenance_path(archive)):
            try:
                path.unlink()
            except FileNotFoundError:
                pass


def stamp_package_provenance(repo, d, version=None, arch=None):
    """Write the version+digest sidecar of the tarball(s) the package step just produced."""
    version = version or sk.read_version(repo)
    if not version:
        raise SystemExit(f"{repo}: no version found; set one before packaging")
    stamped = []
    for archive in sk.linux_tarballs(repo, d):
        if arch is not None and not archive.name.endswith(f"-linux-{arch}.tar.gz"):
            continue
        if not archive.is_file():
            continue
        sk.write_provenance(archive, version)
        stamped.append(archive.name)
    if stamped:
        print(f"{repo}: version+digest sidecar for {', '.join(stamped)} ({version})")
    return stamped


def vcvars():
    found = sorted(glob.glob(r"C:\Program Files\Microsoft Visual Studio\2022\*\VC\Auxiliary\Build\vcvars64.bat"))
    if not found:
        raise SystemExit("vcvars64.bat of Visual Studio 2022 not found")
    return found[-1]


def in_vs(command):
    # One command line, not a list: Python would escape the inner quotes with backslashes, which
    # cmd doesn't read. /s takes the outer quotes off and runs the rest as written.
    return f'cmd /d /s /c "call "{vcvars()}" >nul && {command}"'


def host_arch():
    """The architecture in the Linux preset and package names: amd64 or arm64."""
    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64"):
        return "amd64"
    if machine in ("aarch64", "arm64"):
        return "arm64"
    raise SystemExit(f"unsupported Linux architecture: {machine or 'unknown'} (expected x86_64 or aarch64)")


def _clang_suffixes():
    """The plain name first, then each versioned one (clang-19, clang-20, ...) found or expected."""
    suffixes = [""]
    versioned = set(range(19, 31))
    for directory in os.environ.get("PATH", "").split(os.pathsep):
        try:
            names = os.listdir(directory)
        except OSError:
            continue
        for name in names:
            m = re.fullmatch(r"clang\+\+-(\d+)", name)
            if m:
                versioned.add(int(m.group(1)))
    return suffixes + [f"-{v}" for v in sorted(versioned, reverse=True)]


def linux_compilers(for_games):
    """Select the compiler pair passed to CMake. Ubuntu installs clang-19 under versioned names."""
    for suffix in _clang_suffixes():
        cc = shutil.which("clang" + suffix)
        cxx = shutil.which("clang++" + suffix)
        if not cc or not cxx:
            continue
        version = subprocess.run([cxx, "--version"], capture_output=True, text=True).stdout
        match = re.search(r"clang version (\d+)", version)
        if match and (not for_games or int(match.group(1)) >= 19):
            return [f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_CXX_COMPILER={cxx}"]
    if for_games:
        raise SystemExit("the games' Linux build needs clang and clang++ 19 or newer (Ubuntu 24.04: clang-19)")
    raise SystemExit("the launcher's Linux build needs clang and clang++ (Ubuntu 24.04: clang-19)")


def linux_preflight():
    """Check the development packages before starting a game build."""
    missing = []
    modules = (("x11-xcb", "libx11-xcb-dev"), ("libusb-1.0", "libusb-1.0-0-dev"),
               ("libcurl", "libcurl4-openssl-dev"), ("openssl", "libssl-dev"),
               ("libarchive", "libarchive-dev"), ("gl", "libgl1-mesa-dev"))
    if not shutil.which("pkg-config"):
        missing.append("pkg-config")
    else:
        for module, package in modules:
            if subprocess.run(["pkg-config", "--exists", module]).returncode:
                missing.append(f"{module} ({package})")
    if missing:
        raise SystemExit("the games' Linux build needs: " + "; ".join(missing))


def cmd_build(a):
    logs = sk.WORK / "logs"
    failed = []
    linux = sys.platform.startswith("linux")
    preset = f"linux-{host_arch()}-release" if linux else "win-amd64-release"
    repos = pick_repos(a.repos)
    if linux:
        for_games = any(repo != sk.HUB for repo in repos)
        compilers = linux_compilers(for_games)
        if for_games:
            linux_preflight()
    for repo in repos:
        d = sk.repo_dir(repo)
        steps = []
        if repo == sk.HUB:
            if linux:
                steps.append(("configure", ["cmake", "--preset", preset, *compilers]))
                steps.append(("build", ["cmake", "--build", "--preset", preset]))
                if a.recipe in ("package-installer", "package-linux"):
                    steps.append(("package", ["bash", str(d / "tools" / "package_linux.sh")]))
                    steps.append(("package-appimage", ["bash", str(d / "tools" / "package_appimage.sh")]))
            else:
                steps.append(("build", in_vs("cmake --build --preset release")))
                if a.recipe == "package-installer":
                    steps.append(("package", ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                              str(d / "tools" / "package_installer.ps1")]))
        else:
            # A game's profile-guided build (skylanders-portal cmake/pgo.cmake): --pgo takes its
            # profile from SKYLANDERS_WORK\pgo\<Repo>.profdata (made by the scripted training runs);
            # without --pgo, or without that file, the release folder is set back to an ordinary
            # build, so an old profile never stays in its cache unnoticed.
            profile = sk.WORK / "pgo" / f"{repo}.profdata"
            use = a.pgo and profile.exists()
            value = profile.as_posix() if use else ""
            print(f"{repo}: {'profile-guided, ' + profile.name if use else 'no profile'}"
                  + (" (none made yet)" if a.pgo and not use else ""))
            if linux:
                recipe = "package-linux" if a.recipe in ("package-installer", "package-linux") else a.recipe
                steps.append(("configure", ["cmake", "--preset", preset, *compilers,
                                            f"-DSKYLANDERS_PGO_PROFILE={value}"]))
                steps.append((recipe, ["just", recipe]))
                if a.test:
                    steps.append(("test", ["just", "test-release"]))
            else:
                steps.append(("configure", in_vs(f"cmake --preset win-amd64-release -DSKYLANDERS_PGO_PROFILE={value}")))
                steps.append((a.recipe, in_vs(f"just {a.recipe}")))
                if a.test:
                    steps.append(("test", in_vs("just test-release")))
        build_dir = d / "out" / "build" / preset
        packages_tarballs = linux and any(name in TARBALL_STEPS for name, _ in steps)
        for name, command in steps:
            # Package into fresh files: an unchanged tarball from an earlier run must not be shipped.
            if packages_tarballs and name in TARBALL_STEPS:
                remove_stale_package_output(repo, d, host_arch())
            log = logs / f"{repo}_{name}.log"
            started = datetime.datetime.now()
            # A codegen run starts the SDK's rexglue, which needs its shared/runtime libraries in PATH.
            code = run(command, cwd=d, log=log, path_first=None if linux else build_dir)
            took = (datetime.datetime.now() - started).total_seconds()
            print(f"{repo}: {name} {'ok' if code == 0 else 'FAILED'} in {took:.0f} s ({log})")
            if code:
                failed.append(repo)
                break
        else:
            if packages_tarballs:
                stamp_package_provenance(repo, d, arch=host_arch())
    if failed:
        raise SystemExit("failed: " + ", ".join(failed))


# --- check --------------------------------------------------------------------------------------

FORBIDDEN = re.compile(r"(^rom/(?!README\.md$).+|\.(xex|iso|dump|bin|igz|iga|bk2)$)", re.I)


MOJIBAKE = re.compile("[\u00c2-\u00f4][\u0080-\u00bf]+")
MOJIBAKE_EXT = (".cpp", ".h", ".inc", ".md", ".toml", ".example", ".iss", ".txt", ".py", ".ps1", ".sh", ".cmake", ".json")


GENERIC_ACCOUNT_NAMES = {"admin", "administrator", "builder", "guest", "root", "runner", "user"}


def user_name_patterns(user):
    """Require a path only for account names that commonly occur in prose."""
    name = re.escape(user)
    if user.casefold() in GENERIC_ACCOUNT_NAMES:
        return [rf"Users[\\/]{name}([\\/]|$)", rf"/home/{name}(/|$)"]
    return [rf"\b{name}\b"]


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
        # Vendored source credits may contain a maintainer's name.
        patterns = user_name_patterns(user)
        hits = []
        for pattern in patterns:
            hits += sk.git(repo, "grep", "-I", "-i", "-l", "-E", "-e", pattern, "--", ".",
                            ":!thirdparty", check=False).splitlines()
        if hits:
            problems.append("the maintainer account name in tracked files: " + ", ".join(sorted(set(hits))[:5]))
        if history:
            commits = []
            for pattern in patterns:
                commits += sk.git(repo, "log", "--all", "--format=%h", "-i", "-G", pattern, "--", ".",
                                  ":!thirdparty", check=False).split()
            if commits:
                problems.append("the maintainer account name in the history: commits " + str(commits[:5]))
    # Double-encoded UTF-8 ("\u00c3\u00bc" for "\u00fc", tools/find_mojibake.py): a German help text went out
    # that way in Core of Light 0.3.0-beta.3.
    garbled = []
    for path in tracked:
        if path.startswith("thirdparty/") or not path.endswith(MOJIBAKE_EXT):
            continue
        try:
            text = (d / path).read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        for m in MOJIBAKE.finditer(text):
            try:
                m.group(0).encode("latin-1").decode("utf-8")
            except (UnicodeEncodeError, UnicodeDecodeError):
                continue
            garbled.append(path)
            break
    if garbled:
        problems.append("double-encoded UTF-8 (tools/find_mojibake.py --fix): " + ", ".join(garbled[:5]))
    emails = set(sk.git(repo, "log", "--all", "--format=%ae%n%ce").split())
    private = [e for e in emails if not e.endswith("users.noreply.github.com") and e != "noreply@github.com"]
    if private:
        problems.append(f"commits with a non-noreply e-mail: {sorted(private)}")
    scan_roots = [d / "installer" / "staging"]
    scan_roots += [p for p in (d / "dist").glob("*-linux-*-release") if p.is_dir()]
    if user:
        needles = [f"Users\\{user}", f"Users/{user}", f"/home/{user}"]
        for root in scan_roots:
            if not root.exists():
                continue
            for f in root.rglob("*"):
                if not f.is_file():
                    continue
                is_win_bin = f.suffix.lower() in (".exe", ".dll")
                is_elf = False
                if not is_win_bin:
                    try:
                        with open(f, "rb") as fh:
                            is_elf = fh.read(4) == b"\x7fELF"
                    except OSError:
                        is_elf = False
                if not is_win_bin and not is_elf:
                    continue
                data = f.read_bytes().lower()
                for n in needles:
                    if n.lower().encode("ascii") in data or n.lower().encode("utf-16-le") in data:
                        problems.append(f"{f.name} holds a build machine path ({n})")
                        break
                # The developer kit's log lines all start like this (SkylandersDevKit).
                if b"dev kit: " in data:
                    problems.append(f"{f.name} holds the developer kit")
    # The private repositories (the developer kit, GiantsDecomp) must never be built into a
    # release: their CMake variables stay empty in the release build folder.
    for preset in ("win-amd64-release", "linux-amd64-release", "linux-arm64-release"):
        cache = d / "out" / "build" / preset / "CMakeCache.txt"
        if cache.exists():
            for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
                name, _, value = line.partition("=")
                if name.split(":")[0] in ("SKYLANDERS_DEVKIT_DIR", "GIANTS_DECOMP_DIR") and value.strip():
                    problems.append(f"the {preset} build has {name.split(':')[0]} set (private code would ship)")
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

ARCH_LABEL = {"amd64": "x86-64", "arm64": "ARM64"}
LINUX_ARCH_ORDER = ("amd64", "arm64")


def _linux_arches(assets):
    return [a for a in LINUX_ARCH_ORDER if a in assets.arches]


def _file_arches(assets, name_for):
    found = {p.name for p in assets.files}
    return [a for a in LINUX_ARCH_ORDER if name_for(a) in found]


def _hub_linux_install(version, assets, standalone=True):
    tar_arches = _file_arches(assets, lambda a: f"CoreOfLight-linux-{a}.tar.gz")
    img_arches = _file_arches(assets, lambda a: f"CoreOfLight-{version}-{sk.APPIMAGE_ARCH[a]}.AppImage")
    if not tar_arches and not img_arches:
        return ""
    lead = "**Install:** " if standalone else "**On Linux:** "
    parts = []
    if tar_arches:
        if len(tar_arches) == 1:
            a = tar_arches[0]
            source = f"download `CoreOfLight-linux-{a}.tar.gz` ({ARCH_LABEL[a]}) and unpack it"
        else:
            source = ("download `CoreOfLight-linux-amd64.tar.gz` (x86-64) or "
                      "`CoreOfLight-linux-arm64.tar.gz` (ARM64) and unpack it")
        parts.append(f"{lead}{source} (the SDL3 it needs, which Ubuntu 24.04 doesn't package, is inside)")
    if img_arches:
        if len(img_arches) == 1:
            a = img_arches[0]
            images = f"`chmod +x CoreOfLight-{version}-{sk.APPIMAGE_ARCH[a]}.AppImage`"
        else:
            images = (f"`chmod +x CoreOfLight-{version}-x86_64.AppImage` or "
                      f"`chmod +x CoreOfLight-{version}-aarch64.AppImage`")
        parts.append(("take the AppImage: " if tar_arches else lead) + images)
    text = "".join([parts[0], *([", or " + p for p in parts[1:]])]) + ", and run it."
    if tar_arches and img_arches:
        text += (" The tarball updates itself; an AppImage doesn't, so download the new file for a "
                 "newer version.")
    elif img_arches:
        text += " An AppImage doesn't update itself, so download the new file for a newer version."
    return text + "\n"


def _hub_tail(platforms):
    if platforms == {"linux"}:
        download = "tarball"
    elif platforms == {"windows"}:
        download = "setup"
    else:
        download = "package"
    return (
        "On first start it asks for your own Xbox 360 discs as ISO files and installs the games for them, "
        f"downloading each game's {download} from its repository. Nothing of the games comes with this project.")


def _game_linux_install(repo, assets, standalone=True):
    arches = _linux_arches(assets)
    if len(arches) == 1:
        a = arches[0]
        files = f"`{repo}-linux-{a}.tar.gz` ({ARCH_LABEL[a]})"
    else:
        files = f"`{repo}-linux-amd64.tar.gz` (x86-64) or `{repo}-linux-arm64.tar.gz` (ARM64)"
    lead = "**On its own:** " if standalone else "**On Linux:** "
    return (
        f"{lead}download {files}, unpack it, and run "
        "`./linux-setup.sh <your ISO>` (an ISO file or an extracted disc folder). It copies the disc into `rom/` "
        "next to the game and writes the settings file. The README says which disc version it needs.\n")


def footer(repo, version, assets):
    platforms = set(assets.platforms)
    if repo == sk.HUB:
        blocks = []
        if "windows" in platforms:
            lead = "**Install:** " if platforms == {"windows"} else "**On Windows:** "
            blocks.append(
                f"{lead}either download `CoreOfLightSetup-{version}.exe` and run it (no admin rights "
                "needed), or take the ZIP above. Neither is signed, so Windows may show \"Windows protected your "
                "PC\": click \"More info\" > \"Run anyway\".\n")
        if "linux" in platforms:
            block = _hub_linux_install(version, assets, standalone=platforms == {"linux"})
            if block.strip():
                blocks.append(block)
        blocks.append(_hub_tail(platforms))
        if "linux" in platforms and any(str(p).endswith(".AppImage") for p in assets.files):
            blocks.append("If FUSE is unavailable, run the AppImage with `--appimage-extract-and-run`.\n")
        blocks += [
            "This is a beta: expect bugs. Please report them with Launcher settings (LB / Q) > \"Report a problem\" "
            f"and a [new issue](https://github.com/{GH}/{repo}/issues/new/choose) (attach the text file it makes).\n",
            "Not affiliated with Activision, Toys for Bob or Vicarious Visions. Skylanders is a trademark of its owners.\n",
        ]
        return "\n\n".join(b.strip() for b in blocks)
    g = sk.game(repo)
    blocks = [
        f"**The easy way:** install [Core of Light](https://github.com/{GH}/{sk.HUB}/releases), the launcher. It "
        "sets this game up from your disc and keeps it up to date by itself."]
    if "windows" in platforms:
        lead = "**On its own:** " if platforms == {"windows"} else "**On Windows:** "
        blocks.append(
            f"{lead}run `{repo}Setup-{version}.exe` and point it at your own Xbox 360 disc (an ISO file "
            "or an extracted folder). It's not signed, so Windows may show \"Windows protected your PC\": "
            "\"More info\" > \"Run anyway\". The README says which disc version it needs.")
    if "linux" in platforms:
        blocks.append(_game_linux_install(repo, assets, standalone=platforms == {"linux"}).strip())
    blocks += [
        "The game's data (graphics, sound, levels, movies) doesn't come with it: the setup takes it from your own "
        "disc.",
        f"Problems: F4 > Game > \"Report a problem\" in the game, then a [new issue](https://github.com/{GH}/{repo}"
        "/issues/new/choose).",
        f"Not affiliated with Activision or {g.maker}. Skylanders is a trademark of its owners.",
    ]
    return "\n\n".join(blocks)


def notes_for(repo, version, folder, assets):
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
    parts.append(footer(repo, version, assets).strip())
    return "\n\n".join(parts) + "\n"


def cmd_publish(a):
    folder = pathlib.Path(a.notes).resolve()
    platforms = getattr(a, "platforms", None)
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
        assets = sk.release_assets(repo, version, platforms)
        plan.append((repo, version, assets, notes_for(repo, version, folder, assets)))
    for repo, version, assets, notes in plan:
        notes_file = sk.WORK / "release" / f"notes_{repo}_{version}.md"
        notes_file.parent.mkdir(parents=True, exist_ok=True)
        notes_file.write_text(notes, encoding="utf-8")
        cmd = ["gh", "release", "create", f"v{version}", *map(str, assets.files), "-R", f"{GH}/{repo}",
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
    s = sub.add_parser("build", help="build release binaries/packages, logs in SKYLANDERS_WORK")
    s.add_argument("--recipe", default="build-release",
                   choices=["build-release", "package-installer", "package-linux"])
    s.add_argument("--test", action="store_true", help="also run the games' tests")
    s.add_argument("--pgo", action="store_true",
                   help="profile-guided: each game with its profile from SKYLANDERS_WORK\\pgo\\<Repo>.profdata")
    s.add_argument("--repos", nargs="*")
    s = sub.add_parser("check", help="what would stop a release: game data, user names, build paths")
    s.add_argument("--history", action="store_true", help="also search every commit for the user name")
    s.add_argument("--repos", nargs="*")
    s = sub.add_parser("publish", help="GitHub releases with the built setups")
    s.add_argument("--notes", required=True, help="folder with <Repo>.md and/or common.md")
    s.add_argument("--platforms", nargs="+", metavar="OS",
                   help="assets to publish: windows, linux, or all (default: this machine's); "
                        "the notes describe only what is really there")
    s.add_argument("--dry-run", action="store_true")
    s.add_argument("--repos", nargs="*")
    a = p.parse_args()
    {"status": cmd_status, "bump": cmd_bump, "version": cmd_version, "build": cmd_build,
     "check": cmd_check, "publish": cmd_publish}[a.cmd](a)


if __name__ == "__main__":
    main()
