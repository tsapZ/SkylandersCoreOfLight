#pragma once

// The recompiled games the launcher knows, where it finds them and whether their disc is there
// (games.cpp).

#include <atomic>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace coreoflight {

struct Game {
  const char* key;        // "ssa", the key in coreoflight.ini
  const char* name_en;    // "Spyro's Adventure"
  const char* name_de;
  int year;               // 2011
  const char* exe;        // "ssarecompiled.exe"
  const char* repo;       // "SSARecomp", the folder of its repository
  const char* app_name;   // "Spyro's Adventure Recompiled", its installer's folder
  const char* xex_sha256; // the disc's default.xex, as its repository pins it
};

// The six games, in release order.
const std::vector<Game>& AllGames();

enum class DiscState {
  kUnknown,    // not checked yet
  kChecking,
  kMatches,    // default.xex is the version the game was made for
  kOther,      // a default.xex, but another version or region
  kMissing,    // no default.xex where the game looks for it
};

struct Found {
  std::filesystem::path exe;      // empty: not found
  std::filesystem::path rom;      // the folder with default.xex the game uses
  std::filesystem::path workdir;  // where the game starts: it looks for rom\ there
  // The game's own settings file next to its exe (<stem>.toml), key -> value, for the card.
  std::unordered_map<std::string, std::string> settings;
  std::atomic<DiscState> disc{DiscState::kUnknown};
};

// Looks for every game: a path in coreoflight.ini next to the launcher, the installer's folder
// (%LOCALAPPDATA%\Programs\<app name>), a folder next to the launcher, and the repositories'
// build folders (<repo>\<repo>\out\build\win-amd64-release) above it. Then checks each found
// game's default.xex on a background thread.
void FindGames(std::vector<Found>& found, const std::filesystem::path& launcher_dir);

// Starts a game's exe in its own folder; false (and `error`) if Windows refused.
bool StartGame(const Found& game, std::wstring& error, void** process);

// The shared figure folder every game uses (Documents\Skylanders\Figures, or "figures = <folder>"
// in coreoflight.ini, as a game's portal_figures_dir) and how many figure files are in it. The
// games' pictures for the figure library are in the folder next to it (Pictures).
std::filesystem::path FiguresDir();
int CountFigures();

}  // namespace coreoflight
