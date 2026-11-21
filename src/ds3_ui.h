#pragma once

// The 3DS screen (ds3_ui.cpp; R3 or 3 on the start screen): the five Nintendo 3DS Skylanders
// games from the player's own cartridge dumps (ds3_games.h), a card each with the dump's own icon.
// A on a card does what it needs: installs the game's package, makes its game files from the dump,
// then starts it. Y installs the newest package again (an update). X picks the folder the dumps are
// in with the launcher's folder browser (Downloads until one is picked).

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
  // While a game started from here is starting or running (launcher_ui.h): no second start, no update.
  void set_busy(bool busy) { busy_ = busy; }
  void Draw(float width, float height);
  // A game started from here: its process (launcher_ui.h waits for it as for a game); nullptr if none.
  void* TakeStartedProcess();

 private:
  struct Scan {
    std::atomic<bool> done{false};
    std::filesystem::path folder;
    std::vector<Ds3Dump> dumps;
  };
  // What a card's game needs before it runs.
  enum class Need { kNothing, kPackage, kSetup, kOtherRegion };
  void StartScan();
  void Say(const std::string& text, bool bad);
  Need NeedOf(const Ds3Dump& dump) const;
  void StartSelected();  // A
  void UpdateSelected();  // Y
  void TakeFolder(const std::filesystem::path& folder);
  void RefreshInstalled();

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
  // Per game (Ds3Games() order): the package version there ("" if none) and whether it is set up.
  std::vector<std::string> installed_;
  std::vector<bool> set_up_;
  Ds3Install install_;
  Ds3Dump install_dump_;      // the dump the running install is for (its game)
  bool start_after_ = false;  // the install came from A: start the game once it worked
  FileBrowser browser_;       // X: the folder, with the pad
  void* started_ = nullptr;
  std::string message_;
  bool message_bad_ = false;
  double message_until_ = 0.0;
};

}  // namespace coreoflight
