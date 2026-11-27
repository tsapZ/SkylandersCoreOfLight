# Tests

## The disc check (Linux or Windows)

`coreoflight_disc_id_test` builds a small XEX2 header and a small XDVDFS image in memory and in a
temporary folder: the SHA-256 and the code pin, exact and same-code matching, a full Xbox 360 image
(the game partition at `0xFD90000`) and a trimmed one, a folder with an unpacked disc, and files
that are neither. No disc and no game data are needed.

```
cmake --preset linux-amd64-release   # or linux-arm64-release on ARM64
cmake --build --preset linux-amd64-release
ctest --test-dir out/build/linux-amd64-release --output-on-failure
```

## Real discs

`check_discs.py` runs the launcher's `--check_disc` over a folder of ISOs and compares each result
with `discs_expected.json` (its game, whether it is the exact pressing or another with the same
code, its SHA-256 and its code pin). Only each ISO's directory and `default.xex` are read; nothing
is unpacked and no user data is written. Standard library only.

```
python3 tests/check_discs.py --exe out/build/linux-amd64-release/coreoflight --dir /path/to/isos
```

Without `--exe`, the script takes this machine's preset (`linux-amd64-release` on x86-64,
`linux-arm64-release` on ARM64) and refuses any other architecture instead of assuming amd64.

A collection that holds only some of the discs is fine: the ISOs that are there are checked, the
others are not required. The Russian Spyro's Adventure disc is in the list as `other`: the
launcher names it, but its code is a different build from the one SSARecomp is made for, so it
isn't set up.

`--check_disc` prints one JSON line per disc (`path`, `game`, `name`, `match`, `sha256`,
`code_pin`, `header`, `bytes`) and exits 0 for a disc a game takes, 1 otherwise. It opens no
window and needs no disc set up.

## The update extractor (Linux only)

`coreoflight_archive_extract_test` builds small tarballs in a temporary folder and checks
`platform.cpp`'s `ExtractArchive` policy, not a real update: safe members below the root (the
regular files of a package and an internal library alias), private folders and files, and the
refusals — a `..` name, a mapper that points outside the root, a link out of the root, a file
written through a link, a hard link and a root that is a link or a file.

`coreoflight_portable_zip_test` does the same for `portable.cpp`'s `ReadZipFiles`: the portable
package (exe, `portable.txt`, fonts) with a folder entry, an unsafe `..` or absolute name, a wrong
checksum and a file that isn't a ZIP.
