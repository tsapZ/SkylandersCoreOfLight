#pragma once

// Moving to a new PC (#40): everything that's the player's in one file, and back (data_transfer.cpp).
//
// "Meine Daten exportieren" writes a ZIP (stored, not compressed: Explorer opens it, and saves and
// figures are small) to the desktop, "Skylanders data <date>.zip": from the data folder (games.h
// SkylandersDir) the figures, each game's saves with their copies and achievements, the save
// backups, the profiles and the launcher's own files, under "Skylanders/"; and each installed game's
// settings file under "Settings/". Not in it: the games themselves (set up from the discs again),
// the pictures, HD packs and shader caches (the games make them again).
//
// "Meine Daten importieren" never overwrites quietly: a file that is already there stays, and a
// game's settings file only replaces the one there when the player says so (the new PC's setup
// writes one). A settings file of a game that isn't set up yet waits in the data folder's
// "Imported settings" until the next import.

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "games.h"

namespace coreoflight {

// Where the export goes and the import's browser opens: the desktop (with --documents, a test on
// stand-in data, that folder instead).
std::filesystem::path ExportFolder();

class DataTransfer {
 public:
  // Writes the export on a thread of its own; TakeSummary / TakeExported once it is done.
  void StartExport(const std::vector<Found>& games, bool german);
  // Reads the file's list: false (and the reason in `error`) when it isn't an export of the
  // launcher. Afterwards settings_differ() names the games whose settings file is there already
  // and differs from the file's.
  bool PlanImport(const std::filesystem::path& file, const std::vector<Found>& games, bool german,
                  std::string& error);
  const std::vector<std::string>& settings_differ() const { return settings_differ_; }
  // Brings the planned file in on a thread of its own; `replace_settings`: those settings too.
  void StartImport(bool replace_settings);
  bool running() const { return shared_ && !shared_->done; }
  // What happened, once, when it is done; else empty.
  std::string TakeSummary();
  // The export's file, once, when one was written; else empty.
  std::filesystem::path TakeExported();

  // One file of an import: where it goes, and whether it is a game's settings file.
  struct Item {
    std::string name;  // its name in the ZIP
    std::filesystem::path target;
    uint32_t offset = 0, size = 0, crc = 0;  // its local header and data in the ZIP
    bool settings = false;
    std::string game;  // a settings file's game (its name), for the question
  };

 private:
  struct Shared {
    std::atomic<bool> done{false};
    std::mutex mutex;
    std::string summary;
    std::filesystem::path exported;
    bool taken = false, exported_taken = false;
  };
  std::shared_ptr<Shared> shared_;
  bool german_ = false;
  std::filesystem::path import_file_;
  std::vector<Item> items_;
  std::vector<std::string> settings_differ_;
};

}  // namespace coreoflight
