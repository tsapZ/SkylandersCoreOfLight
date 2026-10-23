#pragma once

// The launcher's one screen: a card per game, controller, keyboard and mouse (launcher_ui.cpp).

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "achievements_ui.h"
#include "saves_ui.h"
#include "disc_import.h"
#include "file_browser.h"
#include "figures_ui.h"
#include "game_settings.h"
#include "games.h"
#include "hub_settings.h"
#include "music.h"
#include "profiles_ui.h"
#include "settings_ui.h"
#include "setup_ui.h"
#include "updates.h"

struct ImFont;

namespace coreoflight {

class LauncherUi {
 public:
  void Init(const std::filesystem::path& launcher_dir, ImFont* regular, ImFont* bold);
  // Draws a frame into the whole window. Returns false when the launcher should close.
  bool Draw(float width, float height);
  // A game was started: its process handle, which main.cpp waits for (the launcher hides
  // meanwhile); nullptr if none.
  void* TakeStartedProcess();
  // After a game ended: look for the games again (one may have been installed meanwhile).
  void Refresh();
  // The game started last has ended: its time goes to the profile it was started with.
  void GameEnded();
  // An ISO dropped on the window or given on the command line: set up like one picked with X.
  void AddDisc(const std::filesystem::path& iso);
  // The started game's window is up (main.cpp hides the launcher then): the launch screen ends.
  void GameWindowShown() { starting_ = false; }
  // While the game runs (main.cpp, about four times a second): its saves' automatic copy every
  // 60 minutes (saves.h AutoBackup, hub setting auto_backup).
  void GameRunning();
  // How the game ended: a message when it wasn't the usual way (exit code 0).
  void GameExitCode(unsigned long code);
  // Fullscreen (hub setting, F11 / Alt+Enter): what main.cpp should show, and a switch that
  // saves the setting.
  bool fullscreen() const { return hub_.fullscreen || fullscreen_forced_; }
  void ToggleFullscreen();
  // --fullscreen: fullscreen for this start, the setting left as it is (F11 then saves as usual).
  void StartFullscreen() { fullscreen_forced_ = true; }

 private:
  void Start(size_t index);
  void OpenSettings(size_t index);
  void DrawCard(size_t index, float x, float y, float w, float h, bool selected);
  // The active profile at the top right; true when clicked.
  bool DrawProfileBadge(float width, float u);
  // Each game's achievements of the active profile, for its card.
  void CountAchievements();
  // The launcher's own settings (hub_settings.h) into every screen: the language, the library.
  void ApplyHubSettings();
  // Draw without the animation; Draw moves the frame's vertices of the screen drawn.
  bool DrawScreens(float width, float height);
  // The screen drawn last frame (its window name) and since when it is shown.
  std::string shown_window_;
  double shown_since_ = 0;
  // How far each card is lifted (0 resting, 1 chosen), eased towards its target each frame.
  std::vector<float> lift_;

  std::filesystem::path launcher_dir_;
  std::vector<Found> found_;
  ImFont* regular_ = nullptr;
  ImFont* bold_ = nullptr;
  bool german_ = false;
  size_t selected_ = 0;
  int figures_ = 0;
  std::wstring error_;
  double error_until_ = 0;
  // A game whose last achievement came with the game that just ended: its platinum trophy shown
  // over the cards for a while (Refresh).
  int platinum_new_ = -1;
  double platinum_until_ = 0;
  void* started_ = nullptr;
  // The running game: its key, the profile it was started with and when (GetTickCount64).
  std::string running_key_;
  Profile running_profile_;
  uint64_t running_since_ = 0;
  std::string last_key_;  // the game that ran last
  // A game that ended with an error code: "Problem melden?" (DrawCrashReport) until answered.
  unsigned long crash_code_ = 0;
  std::string crash_key_;
  bool DrawCrashReport(float width, float height);
  std::vector<uint64_t> play_seconds_;  // per game, the active profile's (PlaySeconds)
  // Each game's settings pages, read from the game when first needed (game_settings.h).
  std::vector<SettingsLoader> loaders_;
  SettingsScreen settings_;
  // The launcher's own settings (LB / Q), in the same screen as a game's.
  HubSettings hub_;
  SettingsLoader hub_loader_;
  SettingsScreen hub_screen_;
  FiguresScreen figures_screen_;
  ProfilesScreen profiles_screen_;
  AchievementsScreen achievements_screen_;
  SavesScreen saves_screen_;  // RT / S
  size_t running_game_ = 0;          // AllGames index of the running game
  uint64_t last_auto_backup_ = 0;    // GetTickCount64 of the last automatic copy
  SetupScreen setup_;  // the first-run setup
  std::vector<GameAchievements> achievements_;
  std::vector<uint64_t> last_saved_;  // per game, the active profile's (Windows file time)
  bool selection_made_ = false;       // the first card chosen (Refresh)
  size_t settings_game_ = 0;
  // Each game's hero picture (FigurePicture), looked for again every ten seconds.
  std::vector<std::filesystem::path> hero_pictures_;
  double hero_checked_ = -100.0;
  // Each game's dashboard art from its disc (nxe_art.h): the logo tile on its card, the background
  // behind the cards while it is chosen. Empty paths: none (no disc, or not installed).
  std::vector<std::filesystem::path> tile_art_, background_art_;
  void FindArt();
  // Setting up a disc from an ISO (disc_import.h), and its phase at the last frame.
  DiscImport import_;
  // Choosing the disc inside the launcher, with the pad too (file_browser.h).
  FileBrowser browser_;
  // The chosen game's title music (music.h), and a game writing it from its disc (its process,
  // which one, since when; each game tried once per launch).
  MusicPlayer music_;
  void* export_process_ = nullptr;
  size_t export_game_ = 0;
  double export_since_ = 0;
  std::vector<bool> export_tried_;
  void UpdateTitleMusicExport();
  // A game without its pictures yet writing them (#12): its process, which one, since when; each
  // game tried once per launch, after its title music.
  void* pictures_process_ = nullptr;
  size_t pictures_game_ = 0;
  double pictures_since_ = 0;
  std::vector<bool> pictures_tried_;
  void UpdatePictureExport();
  // B / Esc on the start screen asks first (a pad user would otherwise quit by mistake): false
  // once A / Enter says quit.
  bool confirm_quit_ = false;
  bool fullscreen_forced_ = false;  // --fullscreen (StartFullscreen), until F11 / Alt+Enter
  // Installing a game from its card (games.h StartGameInstall): each game's setup, found once per
  // Refresh, and the one running (its process, which game, since when).
  std::vector<std::filesystem::path> setups_;
  void* install_process_ = nullptr;
  size_t install_game_ = 0;
  double install_since_ = 0;
  bool install_is_update_ = false;
  void UpdateInstall();
  // Updates from GitHub Releases (updates.h, #17). The releases known (the last check's, then a
  // check in the background once a day or when asked) go into setups_: a game whose release is
  // newer than its setup here gets the release's setup, downloaded when it is needed (the card
  // shows the progress). The launcher's own newer release is offered at the start, when it was
  // installed by its setup.
  struct UpdateWork;  // shared with the check and download threads
  std::shared_ptr<UpdateWork> work_;
  std::vector<Release> releases_;
  std::vector<std::string> setup_urls_;  // per game: where setups_[i] comes from while it isn't there
  std::optional<Release> hub_update_;
  bool hub_update_put_off_ = false;
  bool manual_check_ = false;
  void StartUpdateCheck(bool manual);
  void UpdateReleases();
  void ApplyReleases();
  // Installs (or updates) a game from setups_: at once from a file that is there, else once it is
  // downloaded. False if it can't (no setup, or an install or download already running).
  bool BeginInstall(size_t game, bool update);
  void StartDownload(const std::string& url, const std::filesystem::path& file, size_t game, bool update, bool hub);
  void UpdateDownload();
  bool Downloading() const;
  bool DownloadingFor(size_t game) const;
  float DownloadProgress() const;
  bool DrawHubUpdate(float width, float height);
  // A disc set up meanwhile: the games looked for again (its disc check), its card chosen.
  void UpdateImport();
  // Updates: each game's installed version (empty when it wasn't installed by its setup), the
  // games whose update the owner put off this launch ("Erst spielen"), and the card whose A asks
  // "Update ... jetzt installieren?" (-1: none).
  std::vector<std::string> installed_versions_;
  std::vector<bool> update_put_off_;
  int ask_update_ = -1;
  bool UpdateAvailable(size_t index) const;
  bool DrawUpdateConfirm(float width, float height);
  // "Alle aktualisieren" (a colleague's wish, 2026-10-23: six cards one by one was too much):
  // every game with an update, one after the other (download, silent setup); a game that is
  // running waits until it has ended, and a queued game can't be started meanwhile. The games
  // still to go, how many there were, the ones that failed, and the button on the start screen
  // (between the title and the profile badge, whose left edge DrawProfileBadge leaves).
  std::vector<size_t> update_queue_;
  size_t update_total_ = 0;
  std::vector<std::string> update_failed_;
  float badge_left_ = 0;
  // "Nach Updates suchen" (a colleague: it seemed to search for ever, the result went to the start
  // screen behind the settings): a note at the bottom right on every screen while it looks and with
  // what it found, "Updates gefunden" asking to download them, and the downloads' progress there
  // while another screen is open.
  std::string toast_;
  double toast_until_ = 0;
  bool ask_updates_found_ = false;
  void ShowToast(const std::string& text, double seconds = 6.0);
  void DrawToast(float width, float height, bool start_screen);
  bool DrawUpdatesFound(float width, float height);
  bool UpdateQueued(size_t index) const;
  bool BeingUpdated(size_t index) const;  // queued, downloading or its setup running
  void StartUpdateAll();
  void UpdateQueue();
  void DrawUpdateAll(float width, float u);
  // The games folder (games.h GamesFolder), chosen in a folder browser of its own: from the hub's
  // settings ("Spiele-Ordner") or the first-run setup's row. Set, saved, and the games looked for
  // there at once.
  FileBrowser folder_browser_;
  void ApplyGamesFolder(const std::filesystem::path& dir);
  // A game was just started: its launch screen, until its window is up (main.cpp).
  bool starting_ = false;
  size_t starting_game_ = 0;
  double starting_since_ = 0;
  bool DrawStarting(float width, float height);
  bool DrawQuitConfirm(float width, float height);
  DiscImport::Phase import_seen_ = DiscImport::Phase::kIdle;
  double import_shown_until_ = 0;
  double cancel_import_until_ = 0;  // B pressed once while a disc is set up: a second one until then stops it
};

}  // namespace coreoflight
