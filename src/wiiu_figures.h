#pragma once

// The Wii U figures (Core of Light #56): Donkey Kong and Bowser with their vehicles (SuperChargers, plus its two
// driver challenges) and Imaginators' two Nintendo characters, from the player's own extracted Wii U game. The
// conversion is SkyP4tcher's: superpatcher and imagipatcher (LGPL-2.1-or-later, his OK on #56 2026-11-19) turn the
// Wii U files into the Xbox 360 format. Core of Light runs them from a package it downloads once, pinned by its
// SHA-256 (maintainer/tools/wiiu_tools_package.py builds it: the embeddable Python, numpy, scipy, an LGPL ffmpeg and
// the tools' tagged source), so nobody installs Python.
//
// Steps, on a thread of its own: the package (download, check, unpack into %LOCALAPPDATA%\CoreOfLight\wiiu-tools);
// then one tool run in the game folder (v0.2.0, made for this button): `--wiiu <folder> --install <game folder>`
// converts, copies into rom\archives and sets the .igx files and the .toml settings, keeping everything it replaces
// in superpatcher-backup / imagipatcher-backup with a manifest.json; `--uninstall <game folder>` puts it back.
// Progress from the tools' "PROGRESS n/total label" lines.
//
// Windows only for now (the package is a Windows one); elsewhere Start says so.

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace coreoflight {

class WiiuFigures {
 public:
  enum class Game { kSuperChargers, kImaginators };

  // Whether `folder` looks like an extracted Wii U game: an archives folder, or content\archives below it.
  static bool LooksLikeWiiuGame(const std::filesystem::path& folder);
  // Whether the Wii U figures are in the game whose disc folder is `rom` (the tool's backup manifest is in its parent).
  static bool Installed(const std::filesystem::path& rom);

  // `wiiu` the player's extracted Wii U game, `rom` the game's disc folder (as the launcher found it), `exe_dir` the
  // folder with the game's exe and .toml: the tools' game folder (they write its rom\archives and keep their backup
  // there; in an installed game that is rom's parent).
  void Start(Game game, const std::filesystem::path& wiiu, const std::filesystem::path& rom,
             const std::filesystem::path& exe_dir, bool german);
  // Puts the game back as it was before Start.
  void StartRestore(Game game, const std::filesystem::path& rom, const std::filesystem::path& exe_dir, bool german);

  bool running() const { return shared_ && !shared_->done; }
  float progress() const { return shared_ ? shared_->progress.load() : 0.0f; }
  std::string step() const;
  std::string TakeSummary();

  struct Shared {
    std::atomic<bool> done{false};
    std::atomic<float> progress{0.0f};
    std::mutex mutex;
    std::string step;
    std::string summary;
    bool taken = false;
  };

 private:
  std::shared_ptr<Shared> shared_;
};

}  // namespace coreoflight
