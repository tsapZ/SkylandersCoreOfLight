#pragma once

// The 3DS screen (ds3_ui.cpp; R3 or 3 on the start screen): the five Nintendo 3DS Skylanders
// games from the player's own cartridge dumps (ds3_games.h), a card each with the dump's own icon,
// started in Azahar for Skylanders, which the screen installs and updates (Y). X picks the folder
// the dumps are in with the launcher's folder browser (Downloads until one is picked).

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ds3_games.h"
#include "file_browser.h"

struct ImFont;

namespace coreoflight {

class Ds3Screen {
 public:
  void Init(ImFont* regular, ImFont* bold, bool german);
  // Looks for the dumps again (in the background, in the folder of the hub setting ds3_folder).
  void Open();
  bool is_open() const { return open_; }
  // While Azahar started from here is starting or running (launcher_ui.h): no second start, no update.
  void set_busy(bool busy) { busy_ = busy; }
  void Draw(float width, float height);
  // Azahar started with a dump: its process (launcher_ui.h waits for it as for a game); nullptr if none.
  void* TakeStartedProcess();

 private:
  struct Scan {
    std::atomic<bool> done{false};
    std::filesystem::path folder;
    std::vector<Ds3Dump> dumps;
  };
  void StartScan();
  void Say(const std::string& text, bool bad);
  void StartSelected();
  void TakeFolder(const std::filesystem::path& folder);

  ImFont* regular_ = nullptr;
  ImFont* bold_ = nullptr;
  bool german_ = false;
  bool open_ = false;
  bool busy_ = false;
  std::filesystem::path folder_;
  std::vector<Ds3Dump> dumps_;
  std::shared_ptr<Scan> scan_;  // the search running in the background, null when none
  bool report_count_ = false;   // say how many were found when the search ends (after a new folder)
  int selected_ = 0;
  std::string installed_;  // the Azahar version there, "" if none
  AzaharInstall install_;
  FileBrowser browser_;  // X: the folder, with the pad
  void* started_ = nullptr;
  std::string message_;
  bool message_bad_ = false;
  double message_until_ = 0.0;
};

}  // namespace coreoflight
