#pragma once

// A game's settings in the launcher (settings_ui.cpp): the pages of the game's own F4 menu
// (game_settings.h), changed with the controller, keyboard or mouse and saved into the game's
// settings file.

#include <map>
#include <memory>
#include <string>

#include "game_settings.h"

struct ImFont;

namespace coreoflight {

class SettingsScreen {
 public:
  void Init(ImFont* regular, ImFont* bold, bool german);
  // Opens the screen for a game; its pages come from `loader` (maybe still loading).
  void Open(const std::string& title, SettingsLoader* loader);
  bool is_open() const { return open_; }
  // Draws the screen into the whole window; closes itself on B / Escape.
  void Draw(float width, float height);
  // Settings saved since the last call (cvar -> value), for the card's summary.
  std::map<std::string, std::string> TakeSaved();

 private:
  void Step(int step);
  void Save();
  std::string ValueLabel(const GameSetting& s) const;

  ImFont* regular_ = nullptr;
  ImFont* bold_ = nullptr;
  bool german_ = false;
  bool open_ = false;
  std::string title_;
  SettingsLoader* loader_ = nullptr;
  std::shared_ptr<GameSettings> edit_;  // a copy that the changes go into
  int page_ = 0, row_ = 0;
  // Changed settings: cvar -> (category, type, value).
  struct Pending {
    std::string category, type, value;
  };
  std::map<std::string, Pending> changes_;
  std::map<std::string, std::string> saved_;
  bool confirm_discard_ = false;
  std::string message_;
  double message_until_ = 0;
};

}  // namespace coreoflight
