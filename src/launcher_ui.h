#pragma once

// The launcher's one screen: a card per game, controller, keyboard and mouse (launcher_ui.cpp).

#include <filesystem>
#include <string>
#include <vector>

#include "achievements_ui.h"
#include "disc_import.h"
#include "file_browser.h"
#include "figures_ui.h"
#include "game_settings.h"
#include "games.h"
#include "hub_settings.h"
#include "music.h"
#include "profiles_ui.h"
#include "settings_ui.h"

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
  void* started_ = nullptr;
  // The running game: its key, the profile it was started with and when (GetTickCount64).
  std::string running_key_;
  Profile running_profile_;
  uint64_t running_since_ = 0;
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
  // The chosen game's title music (music.h), and a game recording it (its process, which one,
  // since when).
  MusicPlayer music_;
  void* capture_process_ = nullptr;
  size_t capture_game_ = 0;
  double capture_since_ = 0;
  int pending_capture_ = -1;  // a game whose disc was just set up: record its music next
  void StartCapture(size_t index);
  void DrawCapture(float width, float height, float u);
  DiscImport::Phase import_seen_ = DiscImport::Phase::kIdle;
  double import_shown_until_ = 0;
};

}  // namespace coreoflight
