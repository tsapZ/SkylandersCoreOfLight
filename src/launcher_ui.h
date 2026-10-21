#pragma once

// The launcher's one screen: a card per game, controller, keyboard and mouse (launcher_ui.cpp).

#include <filesystem>
#include <string>
#include <vector>

#include "games.h"

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

 private:
  void Start(size_t index);
  void DrawCard(size_t index, float x, float y, float w, float h, bool selected);

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
};

}  // namespace coreoflight
