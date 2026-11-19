#pragma once

// The five Nintendo 3DS Skylanders games (tsapZ/Skylanders3DS): the player's own cartridge dumps
// found in a folder, recognised by their product code, the Azahar build with the emulated portal
// (tsapZ/azahar, its newest release) installed into the games folder, and a dump started in it.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace coreoflight {

struct Ds3Game {
  const char* code;     // the product code's game letters: "AG6" of CTR-P-AG6P
  const char* name_en;  // "Giants"
  const char* name_de;
  int year;
};
// Spyro's Adventure, Giants, Swap Force, Trap Team, SuperChargers Racing, by year.
const std::vector<Ds3Game>& Ds3Games();

struct Ds3Dump {
  std::filesystem::path file;
  size_t game = 0;             // index in Ds3Games()
  char region = 0;             // the product code's last letter: 'P' Europe, 'E' USA, 'J' Japan
  bool encrypted = false;      // not decrypted: Azahar can't run it without the console's keys
  std::vector<uint8_t> icon;   // its 48x48 icon as RGBA (from the dump's SMDH); empty if none
};

// "Europe", "USA", ... (German: "Europa", "USA", ...); "" for a letter it doesn't know.
const char* Ds3RegionName(char region, bool german);
// A .cci / .3ds file read in place (its headers and icon only): one of the five games, or nullopt.
std::optional<Ds3Dump> ReadDs3Dump(const std::filesystem::path& file);
// The dumps of the five games in `folder` and two folders below it, one per game and region (a
// decrypted one before an encrypted one), by year. A folder that can't be read is skipped, the
// rest still counts. Blocks while it reads the folders: call it from a background thread.
std::vector<Ds3Dump> FindDs3Dumps(const std::filesystem::path& folder);

// Where Azahar for Skylanders is: the folder it was installed into (hub setting ds3_azahar), while
// azahar.exe is still there, else <games folder>\Azahar for Skylanders (games.h GamesFolder) for a
// new install. Portable there: its settings and saves in its "user" folder.
std::filesystem::path AzaharFolder();
// The version installed there (its version.txt, written by the install), "" if none, "?" if the
// file is missing.
std::string InstalledAzaharVersion();

// Installs or updates Azahar for Skylanders in the background: the newest release of tsapZ/azahar
// (its azahar-skylanders-windows-<version>.zip, the hash GitHub gives checked). The ZIP (one top
// folder with azahar.exe in it) is unpacked into <folder>.new first; then the old folder's "user"
// (settings, saves) moves into it and it takes the old folder's place, so a failure on the way
// leaves the old install as it was. --azahar_zip=<zip> takes a local one instead (scripted tests,
// also --offline), only together with --install_dir=<scratch folder>; otherwise --offline installs
// nothing.
class AzaharInstall {
 public:
  ~AzaharInstall();
  void Start(bool german);
  bool running() const { return state_ && !state_->done.load(); }
  float progress() const { return state_ ? state_->progress.load() : 0.0f; }
  // Once it's done: true, its message, whether it worked and the folder it went into; false while it
  // runs or when none ran.
  bool TakeResult(std::string& message, bool& ok, std::filesystem::path& folder);

 private:
  struct State {
    std::atomic<bool> done{false};
    std::atomic<float> progress{0.0f};
    std::atomic<bool> cancel{false};
    bool ok = false;
    std::string message;
    std::filesystem::path folder;
  };
  std::shared_ptr<State> state_;
  std::thread thread_;
};

// The newest Azahar for Skylanders on GitHub (its version, e.g. "0.1.0-beta.1"), asked in the
// background by Ds3LookForAzaharUpdate() (once a run; again after a failed try); "" until known,
// without network or with --offline.
void Ds3LookForAzaharUpdate();
std::string Ds3NewestAzahar();

// Starts Azahar with the dump; its process handle (launcher_ui.h TakeStartedProcess), or nullptr
// with `error` set.
void* StartDs3Dump(const Ds3Dump& dump, bool german, std::string& error);

}  // namespace coreoflight
