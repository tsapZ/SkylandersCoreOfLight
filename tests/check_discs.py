#!/usr/bin/env python3
"""Checks real discs against the launcher's --check_disc report and tests/discs_expected.json.

    python3 tests/check_discs.py --exe out/build/linux-amd64-release/coreoflight --dir /path/to/isos

Only each ISO's directory and default.xex are read; nothing is unpacked and no user data is
written. A collection may hold only some of the discs: only the ISOs in --dir are checked, and the
ones in the manifest that aren't there are fine. Standard library only.
"""

import argparse
import json
import pathlib
import platform
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent


def host_arch() -> str:
    """This machine's release preset architecture; an unknown one is an error, not amd64."""
    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64"):
        return "amd64"
    if machine in ("aarch64", "arm64"):
        return "arm64"
    raise SystemExit(f"unsupported architecture: {machine or 'unknown'} "
                     "(expected x86_64 or aarch64; pass --exe)")


def default_exe() -> pathlib.Path:
    """The launcher of this machine's release preset (linux-amd64 or linux-arm64)."""
    return pathlib.Path(f"out/build/linux-{host_arch()}-release") / "coreoflight"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default=str(default_exe()), help="the launcher (default: %(default)s)")
    parser.add_argument("--dir", required=True, help="a folder of .iso files")
    parser.add_argument("--expect", default=str(HERE / "discs_expected.json"),
                        help="the expected results (default: %(default)s)")
    args = parser.parse_args()

    expected = {entry["file"]: entry
                for entry in json.loads(pathlib.Path(args.expect).read_text(encoding="utf-8"))["discs"]}
    folder = pathlib.Path(args.dir)
    if not folder.is_dir():
        print(f"not a folder: {folder}")
        return 2
    isos = sorted(p for p in folder.iterdir() if p.suffix.lower() == ".iso")
    if not isos:
        print(f"no ISO files in {folder}; skipped")
        return 0

    failures = 0
    for iso in isos:
        want = expected.get(iso.name)
        if want is None:
            print(f"??   {iso.name}: not in {args.expect}")
            failures += 1
            continue
        result = subprocess.run([args.exe, f"--check_disc={iso}"], capture_output=True, text=True,
                                encoding="utf-8")
        line = next((text for text in reversed(result.stdout.splitlines()) if text.startswith("{")), "")
        try:
            got = json.loads(line)
        except json.JSONDecodeError:
            print(f"FAIL {iso.name}: no JSON from --check_disc (exit {result.returncode})")
            print(result.stdout + result.stderr)
            failures += 1
            continue
        problems = []
        for key in ("game", "match", "sha256", "code_pin"):
            if got.get(key) != want.get(key):
                problems.append(f"{key}: expected {want.get(key)!r}, got {got.get(key)!r}")
        wanted_exit = 0 if want["match"] in ("exact", "code") else 1
        if result.returncode != wanted_exit:
            problems.append(f"exit {result.returncode}, expected {wanted_exit}")
        if problems:
            print(f"FAIL {iso.name}")
            for problem in problems:
                print(f"     {problem}")
            failures += 1
        else:
            print(f"ok   {iso.name}  {want['game']} ({want['match']})")

    print(f"\n{len(isos) - failures}/{len(isos)} discs checked, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
