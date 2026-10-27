"""What the maintainer tools know about the seven repositories: names, folders, version files.

The developer layout is one folder per repository, each holding its checkout:
<Tools>\\<Repo>\\<Repo> (Tools\\SSARecomp\\SSARecomp, ..., Tools\\SkylandersCoreOfLight\\SkylandersCoreOfLight).
<Tools> is found from this file's place; SKYLANDERS_TOOLS overrides it. Scratch output (logs, test
runs) goes to SKYLANDERS_WORK, by default %LOCALAPPDATA%\\SkylandersDev.
"""
import os
import pathlib
import re
import subprocess

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
        return TOOLS / self.repo / self.repo

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


def release_assets(repo, version):
    """The files a release of `repo` at `version` uploads."""
    out = repo_dir(repo) / "installer" / "Output"
    if repo == HUB:
        return [out / f"CoreOfLightSetup-{version}.exe", out / f"CoreOfLightPortable-{version}.zip"]
    return [out / f"{repo}Setup-{version}.exe"]


def release_title(repo, version):
    return f"Core of Light {version}" if repo == HUB else f"{game(repo).title} {version}"
