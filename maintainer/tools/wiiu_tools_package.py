"""wiiu_tools_package.py [--out <folder>] [--cache <folder>]: builds the converter package that Core of Light's
"Wii U figures" button downloads (CoL #56): SkyP4tcher's superpatcher (SuperChargers) and imagipatcher (Imaginators),
which turn the player's own extracted Wii U files into the Xbox 360 format, with everything they need to run, so the
player never installs Python:

    python/          the embeddable CPython for Windows (python.org)
    site-packages/   numpy and scipy, unpacked from their PyPI wheels
    ffmpeg/          ffmpeg.exe (BtbN's LGPL build: the tools check every converted sound bank with its xma2 decoder)
    superpatcher/, imagipatcher/   the tools' tagged source, unchanged
    LICENSES/        every part's licence
    manifest.json    what is in it: versions, where each part came from, its SHA-256

Every download is pinned below and checked against the SHA-256 its source publishes (PyPI and GitHub list them; the
python.org zip and GitHub's source zips are recorded on the first build and pinned here after). The zip and its
.sha256 go into --out; the launcher pins that hash. A new tool version = a new pin and a new package.

Stdlib only. Downloads are cached in --cache (default %LOCALAPPDATA%\\SkylandersDev\\cache\\wiiu-tools).
"""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import urllib.request
import zipfile

PACKAGE = "CoreOfLight-WiiU-Tools"
PACKAGE_VERSION = "3"

# What goes in. A sha256 of None is recorded on download and printed, to be pinned here.
PYTHON = {"version": "3.14.10",
          "url": "https://www.python.org/ftp/python/3.14.10/python-3.14.10-embed-amd64.zip",
          "sha256": "90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159"}
WHEELS = [  # (project, version): the cp314 win_amd64 wheel, its hash from PyPI
    ("numpy", "2.5.5"),
    ("scipy", "1.18.1"),
]
# BtbN's "latest" build is replaced every day, so the one package 2 shipped and was tested with is pinned by its hash: a
# copy of it in the cache is used as it is; without one, the day's build with GitHub's asset digest (then pin that).
FFMPEG = {"release": "latest", "asset": "ffmpeg-n8.1-latest-win64-lgpl-8.1.zip",
          "sha256": "60fa9d234dc500e48af0d06de60238df5bd162968d4babca2abcaa0549c4ac77"}
TOOLS = [  # (repo, tag, the script the launcher runs): v0.2.0 has --install / --uninstall and PROGRESS lines (#56)
    ("superpatcher", "v0.2.0", "superpatcher.py"),
    # v0.2.1: game_heap_extra on builds that read it; v0.2.2 (package 3): also finds the Windows builds, which keep
    # the setting's name in the code, by their "Mods: game heap" log line (SkyP4tcher, tested in game 2026-11-20)
    ("imagipatcher", "v0.2.2", "imagipatcher.py"),
]
OWNER = "SkyP4tcher"
USER_AGENT = "CoreOfLight-maintainer"


def fetch(url, cache, name, sha256=None):
    """The file at url in the cache (downloaded once), checked against sha256 when given; returns (path, sha256)."""
    path = cache / name
    if not path.exists():
        print(f"download {url}")
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        tmp = path.with_suffix(path.suffix + ".part")
        with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
            shutil.copyfileobj(r, f, 1 << 20)
        tmp.replace(path)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if sha256 and digest != sha256:
        path.unlink()
        raise SystemExit(f"{name}: sha256 {digest}, expected {sha256} (removed from the cache)")
    return path, digest


def get_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def unzip(path, target, strip_top=False, only=None):
    """Extracts path into target; strip_top drops the archive's single top folder; only(name) filters members."""
    with zipfile.ZipFile(path) as z:
        for info in z.infolist():
            name = info.filename
            if strip_top:
                name = name.split("/", 1)[1] if "/" in name else ""
            if not name or name.endswith("/") or (only and not only(name)):
                continue
            dest = target / name
            # (not Path.is_relative_to: that needs Python 3.9, and the maintainer tools run on 3.8 too)
            if os.path.commonpath([str(dest.resolve()), str(target.resolve())]) != str(target.resolve()):
                raise SystemExit(f"{path.name}: {info.filename} leaves the folder")
            dest.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as src, open(dest, "wb") as out:
                shutil.copyfileobj(src, out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(pathlib.Path(os.environ["LOCALAPPDATA"]) / "SkylandersDev" / "release" / "wiiu-tools"))
    ap.add_argument("--cache", default=str(pathlib.Path(os.environ["LOCALAPPDATA"]) / "SkylandersDev" / "cache" / "wiiu-tools"))
    args = ap.parse_args()
    out, cache = pathlib.Path(args.out), pathlib.Path(args.cache)
    cache.mkdir(parents=True, exist_ok=True)
    stage = out / f"{PACKAGE}-{PACKAGE_VERSION}"
    if stage.exists():
        shutil.rmtree(stage)
    (stage / "LICENSES").mkdir(parents=True)
    manifest = {"package": PACKAGE, "version": PACKAGE_VERSION, "parts": []}

    # Python: the embeddable zip, told where the packages and the tools are (its ._pth replaces sys.path).
    path, digest = fetch(PYTHON["url"], cache, pathlib.Path(PYTHON["url"]).name, PYTHON["sha256"])
    unzip(path, stage / "python")
    pth = next((stage / "python").glob("python3*._pth"))
    lines = [l for l in pth.read_text(encoding="ascii").splitlines() if l.strip() and not l.startswith("#")]
    pth.write_text("\n".join(lines + ["../site-packages"]) + "\n", encoding="ascii")
    shutil.copy(stage / "python" / "LICENSE.txt", stage / "LICENSES" / "python.txt")
    manifest["parts"].append({"name": "python", "version": PYTHON["version"], "url": PYTHON["url"], "sha256": digest,
                              "pinned": bool(PYTHON["sha256"])})

    # numpy, scipy: the wheels for this Python, hashes from PyPI.
    tag = "cp" + "".join(PYTHON["version"].split(".")[:2])
    for project, version in WHEELS:
        files = get_json(f"https://pypi.org/pypi/{project}/{version}/json")["urls"]
        wheel = next(f for f in files if f["filename"].endswith(f"-{tag}-{tag}-win_amd64.whl"))
        path, digest = fetch(wheel["url"], cache, wheel["filename"], wheel["digests"]["sha256"])
        unzip(path, stage / "site-packages")
        lic = next((stage / "site-packages").glob(f"{project}-{version}.dist-info/**/LICENSE*.txt"), None)
        if lic:
            shutil.copy(lic, stage / "LICENSES" / f"{project}.txt")
        manifest["parts"].append({"name": project, "version": version, "url": wheel["url"], "sha256": digest})

    # ffmpeg: only ffmpeg.exe and the licence from BtbN's static LGPL build.
    url = f"https://github.com/BtbN/FFmpeg-Builds/releases/download/{FFMPEG['release']}/{FFMPEG['asset']}"
    if FFMPEG.get("sha256") and (cache / FFMPEG["asset"]).exists():
        path, digest = fetch(url, cache, FFMPEG["asset"], FFMPEG["sha256"])
    else:
        release = get_json(f"https://api.github.com/repos/BtbN/FFmpeg-Builds/releases/tags/{FFMPEG['release']}")
        asset = next(a for a in release["assets"] if a["name"] == FFMPEG["asset"])
        url = asset["browser_download_url"]
        path, digest = fetch(url, cache, asset["name"], asset["digest"].split(":", 1)[1])
    unzip(path, stage / "ffmpeg", strip_top=True, only=lambda n: n in ("bin/ffmpeg.exe", "LICENSE.txt"))
    (stage / "ffmpeg" / "bin" / "ffmpeg.exe").replace(stage / "ffmpeg" / "ffmpeg.exe")
    (stage / "ffmpeg" / "bin").rmdir()
    (stage / "ffmpeg" / "LICENSE.txt").replace(stage / "LICENSES" / "ffmpeg.txt")
    manifest["parts"].append({"name": "ffmpeg", "version": FFMPEG["asset"], "url": url,
                              "sha256": digest, "source": "https://github.com/FFmpeg/FFmpeg"})

    # The tools at their tags, unchanged.
    for repo, tag_name, script in TOOLS:
        commit = get_json(f"https://api.github.com/repos/{OWNER}/{repo}/commits/{tag_name}")["sha"]
        url = f"https://codeload.github.com/{OWNER}/{repo}/zip/{commit}"
        path, digest = fetch(url, cache, f"{repo}-{commit[:12]}.zip")
        unzip(path, stage / repo, strip_top=True)
        shutil.copy(stage / repo / "LICENSE", stage / "LICENSES" / f"{repo}.txt")
        manifest["parts"].append({"name": repo, "version": tag_name, "commit": commit, "url": url, "sha256": digest,
                                  "script": f"{repo}/{script}"})

    (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (stage / "README.txt").write_text(
        "Core of Light's Wii U converter package (tsapZ/SkylandersCoreOfLight#56).\n\n"
        "SkyP4tcher's superpatcher and imagipatcher, which turn your own extracted Wii U SuperChargers or\n"
        "Imaginators files into the Xbox 360 format, with the Python, numpy, scipy and ffmpeg they need.\n"
        "Core of Light downloads and runs it; manifest.json says where every part comes from, LICENSES has\n"
        "their licences (the tools are LGPL-2.1-or-later, ffmpeg is an LGPL build).\n", encoding="utf-8")

    check(stage)

    archive = out / f"{PACKAGE}-{PACKAGE_VERSION}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in sorted(stage.rglob("*")):
            if f.is_file() and "__pycache__" not in f.parts:
                z.write(f, f.relative_to(out).as_posix())
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (out / (archive.name + ".sha256")).write_text(f"{digest}  {archive.name}\n", encoding="ascii")
    print(f"{archive} {archive.stat().st_size // (1 << 20)} MB sha256 {digest}")
    for part in manifest["parts"]:
        if part.get("pinned") is False:
            print(f"pin {part['name']}: sha256 {part['sha256']}")


def check(stage):
    """The package runs on its own: numpy and scipy.signal import, both tools answer --help with the --install and
    --uninstall the launcher calls, ffmpeg has xma2."""
    py = stage / "python" / "python.exe"
    env = {k: v for k, v in os.environ.items() if not k.startswith("PYTHON")}
    env["PATH"] = str(stage / "ffmpeg") + os.pathsep + env.get("PATH", "")
    run = lambda *cmd: subprocess.run(cmd, capture_output=True, text=True, env=env, cwd=stage, timeout=300)
    r = run(str(py), "-c", "import numpy, scipy, scipy.signal; print(numpy.__version__, scipy.__version__)")
    if r.returncode:
        raise SystemExit("python check failed: " + r.stderr[-800:])
    print("python ok:", r.stdout.strip())
    for repo, _, script in TOOLS:
        r = run(str(py), str(stage / repo / script), "--help")
        if r.returncode or "usage" not in r.stdout.lower():
            raise SystemExit(f"{repo} --help failed: {(r.stderr or r.stdout)[-800:]}")
        if "--install" not in r.stdout or "--uninstall" not in r.stdout:
            raise SystemExit(f"{repo} has no --install / --uninstall (the launcher calls them)")
        v = run(str(py), str(stage / repo / script), "--version")
        print(f"{repo} ok: {(v.stdout or v.stderr).strip()}")
    r = run(str(stage / "ffmpeg" / "ffmpeg.exe"), "-hide_banner", "-decoders")
    if r.returncode or " xma2 " not in r.stdout:
        raise SystemExit("ffmpeg has no xma2 decoder")
    print("ffmpeg ok: xma2 decoder")


if __name__ == "__main__":
    sys.exit(main())
