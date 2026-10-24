#pragma once

// A game's settings in the launcher (settings_ui.cpp): the pages of the game's own F4 menu
// (game_settings.h), changed with the controller, keyboard or mouse and saved into the game's
// settings file.

#include <filesystem>
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
  // `background`: the game's dashboard background (nxe_art.h), or empty.
  void Open(const std::string& title, SettingsLoader* loader, std::filesystem::path background = {});
  bool is_open() const { return open_; }
  // Draws the screen into the whole window; closes itself on B / Escape.
  void Draw(float width, float height);
  // Settings saved since the last call (cvar -> value), for the card's summary.
  std::map<std::string, std::string> TakeSaved();
  // The button row (type "action") A was pressed on since the last call: its cvar, or empty.
  std::string TakeAction();
  // A line next to the hints for a few seconds (what an action did).
  void ShowMessage(const std::string& text);
  // A setting's value as it is on the screen now, unsaved changes included (the music volume is
  // heard while it changes); empty when the screen doesn't have it.
  std::string EditedValue(const std::string& cvar) const;
  // What a button row shows as its value (the games folder after choosing one).
  void SetActionLabel(const std::string& cvar, const std::string& en, const std::string& de);

 private:
  void Step(int step);
  // A slider's value set (clamped), as Step does for the other rows.
  void SetSlider(GameSetting& s, int value);
  void Save();
  std::string ValueLabel(const GameSetting& s) const;
  // Whether a row can be changed: its enabled_by setting is on (as edited so far), or it has none.
  // `needs`: that setting, for the explanation.
  bool Enabled(const GameSetting& s, const GameSetting** needs = nullptr) const;

  ImFont* regular_ = nullptr;
  ImFont* bold_ = nullptr;
  bool german_ = false;
  bool open_ = false;
  std::string title_;
  std::filesystem::path background_;  // the game's dashboard background, or empty
  SettingsLoader* loader_ = nullptr;
  std::shared_ptr<GameSettings> edit_;  // a copy that the changes go into
  int page_ = 0, row_ = 0;
  // The first row shown: it follows the chosen row only when that leaves the view (a list centred
  // on it jumped under a resting mouse). The wheel's turns not yet a whole row (a touchpad's).
  int scroll_ = 0;
  int dragging_ = -1;  // the slider row the mouse drags (#24), until the button is up
  float wheel_ = 0.0f;
  // Changed settings: cvar -> (category, type, value).
  struct Pending {
    std::string category, type, value;
  };
  std::map<std::string, Pending> changes_;
  std::map<std::string, std::string> saved_;
  std::string action_;
  bool confirm_discard_ = false;
  std::string message_;
  double message_until_ = 0;
};

}  // namespace coreoflight
