#pragma once

// The POSIX shell program that installs or updates one recompiled game (games.cpp
// StartGameInstall), kept here so a test can run it on synthetic packages without the window.
//
// It never extracts over the live install: the folder is copied into a staging folder next to it
// with independent data (cp -a --reflink=auto, an ordinary copy where the filesystem has no
// reflinks, so a staged write can never reach the live files) and the package is extracted there.
// The package may not ship the game's own data (rom, saves), and every folder it writes into is a
// real directory in the stage, never a link, so no write can leave the staged tree. The folder is
// swapped in with two renames only after the staged package was checked; the old folder stays as
// <folder>.rollback until the swap succeeded, so an interrupted update is put back on the next
// run, and a stale private staging folder from a killed run is cleared first. A per-target flock
// keeps two runs of the same folder from sharing that rollback folder.
//
// The game's settings file is copied out to the private work folder (not into the staged package
// tree) before the swap and put back afterwards, so a package entry with the copy's name cannot
// replace it; a settings link stays a link. Only a new install's own settings are edited, inside
// its existing Game table. A failed copy or promotion leaves the live folder as it was, and a
// failed run leaves an actionable "ROM: ..." line in the setup log, which the launcher's
// GameInstallError surfaces.

namespace coreoflight {

inline constexpr char kGameInstallScript[] = R"COLINSTALL(set -e
archive="$1"; dest="$2"; toml="$3"; ver="$4"; scale="$5"; lang="$6"; res="$7"; log="$8"; exe="$9"
parent=$(dirname "$dest")
base=$(basename "$dest")
mkdir -p "$parent"

step=starting
reported=0
fail() {
  printf "ROM: %s\n" "$1" >>"$log"
  reported=1
  exit 1
}

# The Linux launcher depends on util-linux's flock(1). The per-target lock keeps two updates of the
# same folder from moving each other's rollback folder; the kernel releases it when the run exits,
# so a killed update never leaves the folder locked.
if ! command -v flock >/dev/null 2>&1; then
  fail "flock (util-linux) is needed to install a game"
fi
lock="$parent/.$base.lock"
exec 9>>"$lock"
if ! flock -n 9; then
  fail "another install or update of $base is already running"
fi

backup="$parent/.$base.rollback"
step=preparing
# A run killed earlier left only its private staging folder behind (its lock went with it); the
# files in it are copies, so a retry clears it before making its own.
rm -rf "$parent/.$base.extract."* 2>/dev/null || true
work=
cleanup() {
  status=$?
  # A run killed between moving the old folder aside and the new one in: put it back.
  if [ -d "$backup" ] && [ ! -d "$dest" ]; then
    if ! mv "$backup" "$dest"; then
      printf "ROM: the old install is at %s and could not be put back\n" "$backup" >>"$log"
    fi
  fi
  if [ "$status" -ne 0 ] && [ "$reported" -eq 0 ]; then
    printf "ROM: installation stopped while %s\n" "$step" >>"$log"
  fi
  if [ -n "$work" ]; then rm -rf "$work"; fi
  exit "$status"
}
trap cleanup EXIT
# Recovery from a killed run, or the old copy of a commit that got no further than its last rename.
step=recovering
if [ -d "$backup" ]; then
  if [ -d "$dest" ]; then
    rm -rf "$backup"
  else
    mv "$backup" "$dest"
  fi
fi
step=preparing
work=$(mktemp -d "$parent/.$base.extract.XXXXXX")
stage="$work/stage"
pkg="$work/pkg"
keep="$work/settings.keep"
mkdir -p "$stage" "$pkg"

# Reject package paths that would leave the extraction folder before handing them to tar.
step=reading_the_package
if ! tar -tzf "$archive" >"$work/list" 2>>"$log"; then
  fail "the package could not be read"
fi
while IFS= read -r entry; do
  case "$entry" in
    /*) fail "the package contains an absolute path: $entry" ;;
  esac
  case "/$entry/" in
    */../*) fail "the package contains a path outside it: $entry" ;;
  esac
done <"$work/list"

step=unpacking_the_package
if ! tar -xzf "$archive" -C "$pkg" >>"$log" 2>&1; then
  fail "the package could not be unpacked; see the log"
fi
src="$pkg"
entries=$(find "$pkg" -mindepth 1 -maxdepth 1 | wc -l)
if [ "$entries" -eq 1 ]; then
  only=$(find "$pkg" -mindepth 1 -maxdepth 1)
  if [ -d "$only" ] && [ ! -L "$only" ]; then
    src="$only"
  fi
fi

# The package's entries are checked in the private extraction folder: only plain files and folders
# are copied into the staged tree, so a link or device entry cannot make a later staged path point
# elsewhere. The game's own data is not the package's to replace. An existing rom may be a symlink
# to the owner's disc folder; it stays exactly as it is.
step=checking_the_package
if [ -n "$(find "$src" ! -type d ! -type f -print -quit)" ]; then
  fail "the package contains an entry that is not a plain file or folder"
fi
for protected in rom saves; do
  if [ -e "$src/$protected" ] || [ -L "$src/$protected" ]; then
    fail "the package changes '$protected', which holds the game's own data"
  fi
done
# An archive link is not an executable to run: require a regular file in the package.
if [ -L "$src/$exe" ] || [ ! -f "$src/$exe" ]; then
  fail "the package's $exe is not a regular file in it"
fi

step=copying_the_install
had_toml=0
if [ -d "$dest" ]; then
  mkdir -p "$stage"
  # Independent staged data: a reflink clone or a real copy, never a hardlink that a staged write
  # would carry into the live install.
  if ! cp -a --reflink=auto "$dest"/. "$stage"/ 2>>"$log"; then
    if ! cp -a "$dest"/. "$stage"/ 2>>"$log"; then
      fail "the installed folder could not be copied for the update"
    fi
  fi
  # Kept outside the stage: the package is applied into the stage, so a package entry with the
  # copy's name must not be able to replace the owner's settings. A link is copied as a link and
  # put back as one, so the owner's settings link stays a link and nothing writes through it.
  if [ -L "$dest/$toml" ]; then
    had_toml=1
    cp -P -p "$dest/$toml" "$keep" || fail "the settings file could not be kept"
  elif [ -f "$dest/$toml" ]; then
    had_toml=1
    cp -p "$dest/$toml" "$keep" || fail "the settings file could not be kept"
  fi
fi

# 0: every existing part is a real directory; 1: a link is in the way; 2: a folder is missing
# (the package may create it, since nothing above it is a link); 3: a plain file is in the way.
parents_real() {
  p=$1
  [ "$p" = "." ] && return 0
  if [ -L "$stage/$p" ]; then return 1; fi
  if [ -d "$stage/$p" ]; then
    parents_real "$(dirname "$p")" || return $?
    return 0
  fi
  if [ -e "$stage/$p" ]; then return 3; fi
  parents_real "$(dirname "$p")" || return $?
  return 2
}

# Directories come before their contents (find's default order). Every folder the package writes
# into becomes a real directory in the stage, so no later write goes through a pre-existing link.
step=applying_the_package
find "$src" -mindepth 1 -printf "%y %P\n" >"$work/entries"
while IFS= read -r line; do
  kind=${line%% *}
  entry=${line#* }
  if [ -z "$entry" ]; then continue; fi
  parent_rel=$(dirname "$entry")
  parents_real "$parent_rel" && parent_state=0 || parent_state=$?
  if [ "$parent_state" -eq 1 ]; then
    fail "the package would write through the linked folder '$parent_rel'"
  fi
  if [ "$parent_state" -eq 3 ]; then
    fail "the package would write below '$parent_rel', which is a file"
  fi
  if [ "$kind" = "d" ]; then
    if [ -L "$stage/$entry" ]; then
      fail "the package would write into the linked folder '$entry'"
    fi
    if [ -e "$stage/$entry" ] && [ ! -d "$stage/$entry" ]; then
      rm -f "$stage/$entry" || fail "could not replace '$entry'"
    fi
    mkdir -p "$stage/$entry" || fail "could not create '$entry'"
  else
    if [ "$parent_state" -eq 2 ]; then
      mkdir -p "$stage/$parent_rel" || fail "could not create '$parent_rel'"
    fi
    # Remove a staged entry first: copying a file onto a link would write through it.
    rm -rf "$stage/$entry" || fail "could not replace '$entry'"
  fi
done <"$work/entries"
if ! cp -a "$src"/. "$stage"/ 2>>"$log"; then
  fail "the package could not be copied into the install"
fi
if [ -L "$stage/$exe" ] || [ ! -f "$stage/$exe" ]; then
  fail "the installed $exe is not a usable regular file"
fi

step=writing_the_settings
if [ "$had_toml" -eq 1 ]; then
  # Replace whatever the package put at the settings path, including a folder, before putting the
  # kept file or link back.
  rm -rf "$stage/$toml" || fail "the settings file could not be put back"
  mv -f "$keep" "$stage/$toml" || fail "the settings file could not be put back"
elif [ -f "$stage/$toml" ]; then
  sed -i -E "s|^portal_mode[[:space:]]*=.*|portal_mode = \"hybrid\"|" "$stage/$toml"
  sed -i -E "s|^resolution[[:space:]]*=.*|resolution = \"$res\"|" "$stage/$toml"
  sed -i -E "s|^resolution_scale[[:space:]]*=.*|resolution_scale = $scale|" "$stage/$toml"
  if grep -qE "^#?[[:space:]]*language[[:space:]]*=" "$stage/$toml"; then
    sed -i -E "s|^#?[[:space:]]*language[[:space:]]*=.*|language = \"$lang\"|" "$stage/$toml"
  else
    # Add the key inside the settings' existing Game table; a second table of that name is invalid.
    tmp="$stage/$toml.language"
    awk -v lang="$lang" '
      !done && /^[[:space:]]*\[[[:space:]]*"?Game"?[[:space:]]*\]/ {
        print
        print "language = \"" lang "\""
        done = 1
        next
      }
      { print }
      END {
        if (!done) {
          print ""
          print "[\"Game\"]"
          print "language = \"" lang "\""
        }
      }
    ' "$stage/$toml" >"$tmp"
    mv -f "$tmp" "$stage/$toml"
  fi
fi
step=writing_the_version
if [ -n "$ver" ]; then
  rm -f "$stage/.coreoflight-version"
  printf "%s\n" "$ver" >"$stage/.coreoflight-version"
fi
# Commit: the old folder aside, the staged one in (both renames in the same folder), then the old
# folder goes. A failure before that leaves the install as it was; cleanup puts the old folder back
# if the second rename fails.
step=committing_the_install
if [ -d "$dest" ]; then
  mv "$dest" "$backup" || fail "the old install could not be moved aside"
fi
if ! mv "$stage" "$dest"; then
  fail "the new install could not be put in place"
fi
rm -rf "$backup" || fail "the old install could not be removed"
)COLINSTALL";

}  // namespace coreoflight
