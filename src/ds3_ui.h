#pragma once

// The 3DS screen (ds3_ui.cpp; R3 or 3 on the start screen): the five Nintendo 3DS Skylanders
// games from the player's own cartridge dumps (ds3_games.h), a card each with the dump's own icon,
// started in Azahar for Skylanders, which the screen installs and updates (Y). X picks the folder
// the dumps are in (Downloads until one is picked).

#include <filesystem>
#include <string>
#include <vector>

#include "ds3_games.h"

struct ImFont;

namespace coreoflight {

class Ds3Screen {
 public:
  void Init(ImFont* regular, ImFont* bold, bool german);
  // Looks for the dumps again (in the folder of the hub settings' ds3_folder).
  void Open();
  bool is_open() const { return open_; }
  void Draw(float width, float height);
  // Azahar started with a dump: its process (launcher_ui.h waits for it as for a game); nullptr if none.
  void* TakeStartedProcess();

 private:
  void Scan();
  void Say(const std::string& text, bool bad);
  void StartSelected();

  ImFont* regular_ = nullptr;
  ImFont* bold_ = nullptr;
  bool german_ = false;
  bool open_ = false;
  std::filesystem::path folder_;
  std::vector<Ds3Dump> dumps_;
  int selected_ = 0;
  std::string installed_;  // the Azahar version there, "" if none
  AzaharInstall install_;
  void* started_ = nullptr;
  std::string message_;
  bool message_bad_ = false;
  double message_until_ = 0.0;
};

}  // namespace coreoflight
