#pragma once

// The recompiled games the launcher knows, where it finds them and whether their disc is there
// (games.cpp).

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
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
  // The discs' default.xex, as its repository pins them: one SHA-256, or several comma separated
  // (SuperChargers runs two European discs).
  const char* xex_sha256;
  uint16_t hero_id, hero_variant;  // the game's hero, whose picture is on its card
  const char* app_id;              // its installer's AppId: the uninstall entry "<AppId>_is1"
};

// The six games, in release order.
const std::vector<Game>& AllGames();
// Whether `sha256` (lower-case hex) is one of the game's discs.
bool IsGameDisc(const Game& game, const std::string& sha256);

enum class DiscState {
  kUnknown,    // not checked yet
  kChecking,
  kMatches,    // default.xex is the version the game was made for
  kOther,      // a default.xex, but another version or region
  kMissing,    // no default.xex where the game looks for it
};

// The disc check's result, shared with the thread that hashes the disc: that thread may still run
// after the launcher looked for the games anew and replaced the Found it was started for.
class DiscResult {
 public:
  DiscState load() const { return state_->load(); }
  DiscResult& operator=(DiscState state) {
    state_->store(state);
    return *this;
  }
  std::shared_ptr<std::atomic<DiscState>> shared() const { return state_; }

 private:
  std::shared_ptr<std::atomic<DiscState>> state_ = std::make_shared<std::atomic<DiscState>>(DiscState::kUnknown);
};

struct Found {
  std::filesystem::path exe;      // empty: not found
  std::filesystem::path rom;      // the folder with default.xex the game uses
  std::filesystem::path workdir;  // where the game starts: it looks for rom\ there
  // The game's own settings file next to its exe (<stem>.toml), key -> value, for the card.
  std::unordered_map<std::string, std::string> settings;
  DiscResult disc;
  // Found in the games folder without its setup's uninstall entry: copied there (another drive, a
  // network drive, a Linux folder under Proton). Its version isn't known, so the newest setup is
  // offered once; it goes into that folder, and its entry knows the version from then on.
  bool copied = false;
};

// Looks for every game: a path in coreoflight.ini next to the launcher, where its setup put it
// (its uninstall entry), the games folder (GamesFolder: <folder>\<app name>, the folder itself, or
// any folder up to two levels down that holds the game's exe), the installer's default folder
// (%LOCALAPPDATA%\Programs\<app name>), a folder next to the launcher, and the repositories'
// build folders (<repo>\<repo>\out\build\win-amd64-release) above it. Then checks each found
// game's default.xex on a background thread.
void FindGames(std::vector<Found>& found, const std::filesystem::path& launcher_dir);

// Starts a game's exe in its own folder; false (and `error`) if Windows refused.
bool StartGame(const Found& game, std::wstring& error, void** process);


// Installing a game from the launcher (the owner's wish, 2026-10-23): its own setup
// (<Repo>Setup-<version>.exe), found in the launcher's games\ folder or, for a developer, in the
// repository's installer\Output, started hidden with /VERYSILENT /NOROM=1 and the language, the
// screen's size and 2x scale for its settings; the launcher then sets up the disc itself. With a
// library folder (--install_dir=<folder>, or "library = <folder>" in coreoflight.ini) the game
// goes to <library>\<app name>, else to the setup's own default. The log is
// %TEMP%\CoreOfLight\<key>-setup.log.
void SetInstallDir(const std::filesystem::path& dir);
// Under Wine a folder below Z:\home\ as its real place Z:\var\home\... when both are the same
// folder (Fedora Atomic systems such as Bazzite link /home to /var/home; through the link the
// discs couldn't be unpacked, 2026-10-24); else `dir` as it is.
std::filesystem::path RealFolder(const std::filesystem::path& dir);
// The folder new games go into (each into its own folder there): the one set, else
// %LOCALAPPDATA%\Programs, as its setups do by default.
std::filesystem::path GamesFolder();
// Whether one is set at all (--install_dir, coreoflight.ini's "library" or the hub's "games_dir").
bool GamesFolderChosen();
// Where an install of `game` goes: <library>\<app name>, else the setup's default,
// %LOCALAPPDATA%\Programs\<app name>.
std::filesystem::path InstallFolder(const Game& game);
// The setup with the highest version of the folders above (by its name's version, then its time).
std::filesystem::path FindSetup(const Game& game, const std::filesystem::path& launcher_dir);
// `update`: the game is installed already; its setup goes over it where it is (`dir`, the folder of
// the game's exe, also for a copied game without an uninstall entry), and, as for an install,
// skips the disc page and keeps the settings file.
bool StartGameInstall(size_t game, const std::filesystem::path& setup, bool german, void** process,
                      bool update = false, const std::filesystem::path& dir = {});
// Updates: the version the game's setup put at `exe` (DisplayVersion of its uninstall entry, when
// that entry's InstallLocation holds this exe), empty for a game not installed by its setup (a
// developer's build); a setup's version from its name (<Repo>Setup-<version>.exe); and whether
// version a is older than b (0.1.0-alpha.2 < 0.1.0-beta.1 < 0.1.0, numbers as numbers).
std::string InstalledVersion(const Game& game, const std::filesystem::path& exe);
std::string SetupVersion(const Game& game, const std::filesystem::path& setup);
bool VersionLess(const std::string& a, const std::string& b);
// Why the last install of `game` stopped, from its setup log ("ROM: ..." lines and the like);
// empty if the log says nothing.
std::string GameInstallError(size_t game);
// Where a game's title music is kept (Pictures\<game>\title_music.wav, next to the pictures the
// games leave; each game also leaves it there on its first start), and a run that writes it: the
// game with --export_title_music (skylanders-portal title_music.h) decodes it from its disc and
// quits before a window opens, in about a second, hidden, on user data of its own in the temp
// folder. False if the game's build can't do it.
std::filesystem::path TitleMusicFile(size_t game);
// The game's picture folder (Documents\Skylanders\Pictures\<game>, the main profile's), and a run
// that fills it at once (#12): the game with --export_pictures (skylanders-portal) writes its figure,
// hat and achievement pictures there and quits, hidden and muted, in about half a minute, on user
// data of its own in the temp folder. False if the game's build can't do it.
std::filesystem::path PicturesDir(size_t game);
bool StartPictureExport(size_t game, const Found& found, void** process);
bool StartTitleMusicExport(size_t game, const Found& found, void** process);

// The active profile's figure folder (profiles.h): for the main profile the shared one every game
// uses (Documents\Skylanders\Figures, or "figures = <folder>" in coreoflight.ini, as a game's
// portal_figures_dir: MainFiguresDir). And how many figure files are in it. The games' pictures for
// the figure library are in the folder next to it (Pictures).
std::filesystem::path FiguresDir();
std::filesystem::path MainFiguresDir();
int CountFigures();

// A figure's picture as one of the games left it (Pictures\<game>\<id>-<variant>.png next to the
// figure folder; the active profile's, then the main profile's): the figure's own game's first (a
// Series 1 Spyro from Spyro's Adventure, a Series 2 one from Giants), then the newest game's;
// empty if no game has left one yet.
std::filesystem::path FigurePicture(uint16_t id, uint16_t variant);

// "key = value" lines of an ini file (";" and "#" start a comment, a byte order mark is skipped).
std::unordered_map<std::string, std::string> ReadIni(const std::filesystem::path& path);
std::filesystem::path DocumentsDir();
void SetDocumentsDir(const std::filesystem::path& dir);  // --documents=<folder>, to try on a copy
bool DocumentsMoved();  // --documents was given
// The folder name of a game's user data in Documents: ssarecompiled.exe keeps it in ssarecomp
// (the SDK's app name, the same as its settings file's).
std::string UserDataName(const std::filesystem::path& exe);

}  // namespace coreoflight
