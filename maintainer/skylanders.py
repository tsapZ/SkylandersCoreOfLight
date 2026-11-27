"""What the maintainer tools know about the seven repositories: names, folders, version files.

The developer layout is one folder per repository, each holding its checkout:
<Tools>\\<Repo>\\<Repo> (Tools\\SSARecomp\\SSARecomp, ..., Tools\\SkylandersCoreOfLight\\SkylandersCoreOfLight).
<Tools> is found from this file's place; SKYLANDERS_TOOLS overrides it. Scratch output (logs, test
runs) goes to SKYLANDERS_WORK, by default %LOCALAPPDATA%\\SkylandersDev.
"""
import hashlib
import os
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
TOOLS = pathlib.Path(os.environ.get("SKYLANDERS_TOOLS") or HERE.parents[2])
WORK = pathlib.Path(os.environ.get("SKYLANDERS_WORK") or
                    pathlib.Path(os.environ.get("LOCALAPPDATA", str(pathlib.Path.home()))) / "SkylandersDev")

HUB = "SkylandersCoreOfLight"


class Game:
    def __init__(self, repo, name, exe, maker):
        self.repo = repo        # repository and folder name
        self.name = name        # the game's name, also its folder in Documents\Skylanders
        self.exe = exe          # the built executable's name, without .exe
        self.maker = maker      # the studio, for the release notes' disclaimer

    @property
    def dir(self):
        return repo_dir(self.repo)

    @property
    def title(self):
        return f"{self.name} Recompiled"


GAMES = [
    Game("SSARecomp", "Spyro's Adventure", "ssarecompiled", "Toys for Bob"),
    Game("GiantsRecomp", "Giants", "giantsrecompiled", "Toys for Bob"),
    Game("SwapForceRecomp", "Swap Force", "swapforcerecompiled", "Vicarious Visions"),
    Game("TrapTeamRecomp", "Trap Team", "trapteamrecompiled", "Toys for Bob"),
    Game("SuperChargersRecomp", "SuperChargers", "superchargersrecompiled", "Vicarious Visions"),
    Game("ImaginatorsRecomp", "Imaginators", "imaginatorsrecompiled", "Toys for Bob"),
]
REPOS = [g.repo for g in GAMES] + [HUB]

# Other builds of a game whose exes ship next to its own (skylanders-portal src/xex/sibling_build.h): the exe names per
# repository. Each is built from a checkout of the game at WORK\siblings\<name> whose rom\ holds a disc of that build
# (set up once: a clone or worktree of the game with its submodules); `release.py build` brings it to the main
# checkout's commits, builds it and puts the exe in out\siblings\<triple>\, where the package recipes take it.
# 2026-11-26: SuperChargers' French / Dutch and Scandinavian discs (Core of Light #135, #118).
SIBLINGS = {"SuperChargersRecomp": ["superchargersrecompiled_fr_nl_nordic"]}

# The shared submodules and the branch each repository follows.
SUBMODULES = {
    "thirdparty/rexglue-sdk": ("tsapZ/rexglue-sdk", "skylanders"),
    "thirdparty/skylanders-portal": ("tsapZ/skylanders-portal", "feature/portal-menu"),
}

# A release version: x.y.z, or x.y.z-beta.N with N one digit (GitHub sorts tags as text, so
# beta.10 would come before beta.9; after beta.9 the minor version goes up instead).
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+(-beta\.[1-9])?$")


def game(repo):
    for g in GAMES:
        if g.repo == repo:
            return g
    return None


def repo_dir(repo):
    candidates = [TOOLS / repo / repo, TOOLS / repo, TOOLS / "tsapZ" / repo, HERE.parents[1] / "tsapZ" / repo]
    if repo == HUB:
        candidates.insert(1, HERE.parent)
    for c in candidates:
        if (c / ".git").exists():
            return c
    return TOOLS / repo / repo


def git(repo_or_dir, *args, check=True):
    d = repo_or_dir if isinstance(repo_or_dir, pathlib.Path) else repo_dir(repo_or_dir)
    r = subprocess.run(["git", *args], cwd=d, capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    if check and r.returncode:
        raise SystemExit(f"git {' '.join(args)} in {d}: {r.stderr.strip()}")
    return r.stdout.strip()


def version_file(repo):
    """The file and pattern that hold a repository's release version."""
    if repo == HUB:
        return repo_dir(repo) / "CMakeLists.txt", re.compile(r'(set\(COREOFLIGHT_VERSION ")([^"]+)("\))')
    return repo_dir(repo) / "installer" / f"{repo}.iss", re.compile(r'(#define MyAppVersion ")([^"]+)(")')


def read_version(repo):
    path, pattern = version_file(repo)
    m = pattern.search(path.read_text(encoding="utf-8-sig"))
    return m.group(2) if m else None


PLATFORMS = ("windows", "linux")
LINUX_ARCHES = ("amd64", "arm64")
APPIMAGE_ARCH = {"amd64": "x86_64", "arm64": "aarch64"}

# The Linux tarballs have no version in their name, so the packaging build writes a small sidecar
# next to each: the version and the archive's SHA-256. `publish` checks it instead of trusting a
# file's timestamp, which says nothing after a copy or a mixed-OS build.
PROVENANCE_SUFFIX = ".provenance"


def provenance_path(archive):
    """Where the version+digest sidecar of an unversioned archive lives."""
    archive = pathlib.Path(archive)
    return archive.with_name(archive.name + PROVENANCE_SUFFIX)


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def write_provenance(archive, version, digest=None):
    """Record the version and content digest of an unversioned archive next to it."""
    digest = digest or sha256_file(archive)
    provenance_path(archive).write_text(f"{version} {digest}\n", encoding="ascii")


def read_provenance(archive):
    """(version, sha256) from the sidecar, or None when it is missing or unreadable."""
    try:
        fields = provenance_path(archive).read_text(encoding="ascii").split()
    except (OSError, UnicodeDecodeError):
        return None
    return (fields[0], fields[1]) if len(fields) == 2 else None


def provenance_problem(archive, version):
    """Why an unversioned archive does not belong to `version`, or None when it does."""
    recorded = read_provenance(archive)
    if recorded is None:
        return "no version+digest sidecar (build it with release.py build)"
    recorded_version, recorded_digest = recorded
    if recorded_version != version:
        return f"built for {recorded_version}, not {version}"
    if recorded_digest != sha256_file(archive):
        return "its digest does not match the sidecar"
    return None


def linux_tarballs(repo, root=None):
    """The unversioned Linux tarballs `repo` can ship, one per architecture."""
    d = pathlib.Path(root) if root is not None else repo_dir(repo)
    if repo == HUB:
        folder, prefix = d / "installer" / "Output", "CoreOfLight"
    else:
        folder, prefix = d / "dist", repo
    return [folder / f"{prefix}-linux-{arch}.tar.gz" for arch in LINUX_ARCHES]


def host_platform():
    return "windows" if sys.platform.startswith("win") else "linux"


def platform_list(platforms=None):
    """'all', a platform name, an iterable of them, or None for this machine's."""
    if platforms is None or platforms == "host":
        return [host_platform()]
    if isinstance(platforms, str):
        platforms = [platforms]
    out = []
    for p in platforms:
        p = p.lower()
        if p == "all" or p == "host":
            out += list(PLATFORMS)
        elif p in PLATFORMS:
            out.append(p)
        else:
            raise SystemExit(f"unknown platform {p!r}; one of: {', '.join(PLATFORMS)}, all")
    return list(dict.fromkeys(out))


class Assets:
    """The files a release uploads, and the platforms/architectures they actually cover."""

    def __init__(self, files, platforms, arches):
        self.files = files        # list[pathlib.Path]
        self.platforms = platforms  # set of "windows"/"linux"
        self.arches = arches      # set of the Linux architectures included

    def __iter__(self):
        return iter(self.files)

    def __len__(self):
        return len(self.files)


def release_assets(repo, version, platforms=None, root=None):
    """The files a release of `repo` at `version` uploads.

    `platforms` selects the operating systems (None: this machine's). Windows setups and AppImages
    carry the version in their name; the unversioned Linux tarball must have a sidecar naming this
    version and matching its SHA-256. An empty set, an unbuilt tarball or one whose content changed
    after packaging is refused. The returned Assets carries the platforms and Linux architectures
    really found, so the release notes describe what is shipped rather than the publishing machine.
    """
    d = pathlib.Path(root) if root is not None else repo_dir(repo)
    wanted = platform_list(platforms)
    out_dir = d / "installer" / "Output"

    files = []
    found = set()
    arches = set()
    stale = []

    if "windows" in wanted:
        if repo == HUB:
            expected = [out_dir / f"CoreOfLightSetup-{version}.exe",
                        out_dir / f"CoreOfLightPortable-{version}.zip"]
        else:
            expected = [out_dir / f"{repo}Setup-{version}.exe"]
        missing = [p.name for p in expected if not p.is_file()]
        if missing:
            raise SystemExit(f"{repo}: missing Windows assets for {version}: " + ", ".join(missing))
        files += expected
        found.add("windows")

    if "linux" in wanted:
        tarballs = dict(zip(LINUX_ARCHES, linux_tarballs(repo, d)))
        for arch in LINUX_ARCHES:
            expected = [(tarballs[arch], False)]
            if repo == HUB:
                expected.append((out_dir / f"CoreOfLight-{version}-{APPIMAGE_ARCH[arch]}.AppImage", True))
            present = []
            for path, versioned in expected:
                if not path.is_file():
                    continue
                if versioned:
                    present.append(path)
                    continue
                problem = provenance_problem(path, version)
                if problem is None:
                    present.append(path)
                else:
                    stale.append(f"{path.name} ({problem})")
            if present:
                files += present
                arches.add(arch)
        if arches:
            found.add("linux")

    missing_platforms = [p for p in wanted if p not in found]
    if missing_platforms:
        detail = f": {'; '.join(stale)}" if stale else ""
        raise SystemExit(f"{repo}: no current {version} assets for "
                         + ", ".join(missing_platforms) + f" in {d}{detail}; build them first")
    return Assets(files, found, arches)


def release_title(repo, version):
    return f"Core of Light {version}" if repo == HUB else f"{game(repo).title} {version}"
