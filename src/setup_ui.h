#pragma once

// The first-run setup (setup_ui.cpp; the owner's wish, 2026-10-23, for friends trying the games):
// on a launcher's first start with no game installed it greets, offers the language, looks for
// the discs by itself (disc_search.h), shows every game with the disc found for it, the room the
// chosen ones need on the drive they go to and what is free there, then installs each chosen game
// from its setup and sets up its disc, one after the other. The launcher does the work (its
// install and its disc import); this screen says what comes next and shows how far it is.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "disc_import.h"
#include "disc_search.h"
#include "games.h"

struct ImFont;

namespace coreoflight {

class SetupScreen {
 public:
  void Init(ImFont* regular, ImFont* bold, bool german);
  void Open();
  bool is_open() const { return open_; }

  // What the launcher knows now: its games, their setups, whether a setup runs, its disc import.
  struct Launcher {
    const std::vector<Found>* found;
    const std::vector<std::filesystem::path>* setups;
    bool installing;
    const DiscImport* import;
  };
  void Draw(float width, float height, const Launcher& launcher);

  // The next step for the launcher to start, once: a game's setup or a disc, stopping the disc
  // being set up (B while it unpacks), the file browser for a disc the search didn't find (X), or
  // the folder browser for the games folder (A on its row) or for the data folder (A on the welcome
  // page's row). The disc chosen there comes back with AddDisc, the games folder with UseGamesFolder;
  // the data folder the launcher moves itself (data_move.h).
  struct Job {
    enum class Kind { kNone, kInstall, kImport, kCancel, kPickDisc, kPickFolder, kPickDataFolder } kind = Kind::kNone;
    size_t game = 0;
    std::filesystem::path disc;
  };
  Job TakeJob();
  // The language chosen on the first page ("german" / "english"), once; empty when unchanged.
  std::string TakeLanguage();
  // A disc chosen by hand (an ISO or an unpacked folder): known by its default.xex like the
  // search's, it takes its game's row.
  void AddDisc(const std::filesystem::path& disc);
  // A games folder chosen in the folder browser (the launcher has set and saved it): shown and
  // kept among the row's choices.
  void UseGamesFolder(const std::filesystem::path& dir);
  // Closed (done or skipped), once: the launcher remembers it and doesn't open it again.
  bool TakeFinished();

 private:
  enum class Page { kWelcome, kDiscs, kWork, kDone };
  enum class State { kNoDisc, kFound, kReady, kWaiting, kInstalling, kImporting, kDone, kFailed };
  struct Row {
    std::filesystem::path disc;
    uint64_t bytes = 0;  // the disc's files without its system update
    bool chosen = false;
    bool automatic = false;  // found next to a disc chosen by hand, not chosen itself
    State state = State::kNoDisc;
    std::string note;
  };
  void TakeSearchResults();
  void Advance(const Launcher& launcher);
  uint64_t NeededBytes() const;
  void DrawWelcome(float width, float height);
  void DrawDiscs(float width, float height, const Launcher& launcher);
  void DrawWork(float width, float height, const Launcher& launcher);
  // Where the games go: %LOCALAPPDATA%\Programs, or "Skylanders Recompiled" on another fixed drive,
  // switched with left / right (or A) on its row below the games and kept in the hub's settings.
  void FindGameFolders();
  void ChooseGameFolder(int choice);

  ImFont* regular_ = nullptr;
  ImFont* bold_ = nullptr;
  bool german_ = false;
  bool open_ = false;
  Page page_ = Page::kWelcome;
  int selected_ = 0;
  std::vector<Row> rows_;  // one per game, in AllGames() order
  std::vector<std::filesystem::path> folders_;  // [0] empty: the default
  int folder_ = 0;
  std::shared_ptr<DiscSearch> search_;
  size_t taken_ = 0;  // the search's results looked at so far
  std::filesystem::path pending_disc_;  // chosen by hand, being read by the search's worker
  int current_ = -1;  // the game being set up (kWork)
  bool stopping_ = false;
  Job job_;
  std::string language_;
  std::string message_;  // about the disc chosen by hand
  int pick_for_ = -1;    // the game whose disc the file browser is choosing
  bool nearby_ = false;  // search_ is the look next to a chosen disc, not the whole PC
  bool finished_ = false;
};

}  // namespace coreoflight
