#pragma once

// The five Nintendo 3DS Skylanders games as recompiled Windows programs (tsapZ/3dsrecomp, one package
// a game, its release assets): the player's own cartridge dumps found in a folder, recognised by their
// product code; each game's package installed into the games folder's "3DS" folder; its game files made
// from the player's own dump by the package itself (<game>.exe --setup <dump>); the game started with
// its saves in the data folder (Skylanders\3DS\<game>). No game files come with a package. Each package
// runs one title's recompiled code: the European cartridges' (the dumps it was made from); a dump of
// another region is found but can't be set up.

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
  const char* code;      // the product code's game letters: "AG6" of CTR-P-AG6P
  const char* name_en;   // "Giants"
  const char* name_de;
  int year;
  const char* title_id;  // the European cartridge's program id ("00040000000a3500"), the one the package runs
  const char* asset;     // its release asset: <asset>-<version>.zip ("skylanders-3ds-giants-windows")
  const char* exe;       // "giants.exe"
  const char* folder;    // the package's folder: "Skylanders Giants 3DS"
};
// Spyro's Adventure, Giants, Swap Force, Trap Team, SuperChargers Racing, by year.
const std::vector<Ds3Game>& Ds3Games();

struct Ds3Dump {
  std::filesystem::path file;
  size_t game = 0;             // index in Ds3Games()
  char region = 0;             // the product code's last letter: 'P' Europe, 'E' USA, 'J' Japan
  bool encrypted = false;      // not decrypted: the setup needs the console's keys (boot9.bin next to it)
  std::string title_id;        // its program id, lower-case hex
  std::vector<uint8_t> icon;   // its 48x48 icon as RGBA (from the dump's SMDH); empty if none
  // Whether the package runs it: the title its code was recompiled from.
  bool supported() const;
};

// "Europe", "USA", ... (German: "Europa", "USA", ...); "" for a letter it doesn't know.
const char* Ds3RegionName(char region, bool german);
// A .cci / .3ds file read in place (its headers and icon only): one of the five games, or nullopt.
std::optional<Ds3Dump> ReadDs3Dump(const std::filesystem::path& file);
// The dumps of the five games in `folder` and two folders below it, one per game and region (a
// decrypted one before an encrypted one), by year. A folder that can't be read is skipped, the
// rest still counts. Blocks while it reads the folders: call it from a background thread.
std::vector<Ds3Dump> FindDs3Dumps(const std::filesystem::path& folder);

// A game's package folder: <games folder>\3DS\<folder> (games.h GamesFolder; --install_dir for tests).
std::filesystem::path Ds3PackageFolder(const Ds3Game& game);
// The package version there (its version.txt, written by the install), "" if none, "?" without the file.
std::string InstalledDs3Version(const Ds3Game& game);
// Whether its game files were made (resources\game holds the setup's code.bin and an info.json naming
// the package's title).
bool Ds3SetUp(const Ds3Game& game);
// Where a game's saves go: <data folder>\3DS\<folder> (passed as --save-dir).
std::filesystem::path Ds3SaveFolder(const Ds3Game& game);

// Installs or updates one game's package in the background, then (with a dump) makes its game files:
// the newest release of tsapZ/3dsrecomp with the game's asset (the hash GitHub gives checked). The ZIP
// (one top folder with the exe in it) is unpacked into <folder>.new first; the old folder's game files
// (resources\game: what the setup made from the dump) move into it and it takes the old folder's place,
// so a failure on the way leaves the old install as it was. Then, with a dump and no game files yet,
// <exe> --setup <dump> --out <folder>\resources\game. --ds3_package_zip=<zip> takes a local one instead
// (scripted tests, also --offline), only together with --install_dir=<scratch folder>.
class Ds3Install {
 public:
  ~Ds3Install();
  // `dump`: set the game files up from it afterwards (null: only the package). `package`: install or
  // update the package first (false: only the setup, the package being there).
  void Start(size_t game, const Ds3Dump* dump, bool package, bool german);
  bool running() const { return state_ && !state_->done.load(); }
  float progress() const { return state_ ? state_->progress.load() : 0.0f; }
  bool setting_up() const { return state_ && state_->setting_up.load(); }
  // Once it's done: true, its message and whether it worked; false while it runs or when none ran.
  bool TakeResult(std::string& message, bool& ok);

 private:
  struct State {
    std::atomic<bool> done{false};
    std::atomic<float> progress{0.0f};
    std::atomic<bool> cancel{false};
    std::atomic<bool> setting_up{false};
    bool ok = false;
    std::string message;
  };
  std::shared_ptr<State> state_;
  std::thread thread_;
};

// The newest package version on GitHub (e.g. "0.1.0-beta.1"), asked in the background by
// Ds3LookForUpdates() (once a run; again after a failed try); "" until known, without network or with
// --offline. One release has the five games' packages.
void Ds3LookForUpdates();
std::string Ds3NewestVersion();

// Starts the game of the dump (its package set up); its process handle (launcher_ui.h
// TakeStartedProcess), or nullptr with `error` set.
void* StartDs3Game(const Ds3Dump& dump, bool german, std::string& error);

}  // namespace coreoflight
