#pragma once

// The launcher's own settings (the owner's wish, 2026-10-22: the library's "only mine" and the
// "I own it" marks as options to switch and save, the menus' language), shown in the same
// settings screen as a game's (settings_ui.h) and saved in Documents\Skylanders\coreoflight.toml,
// line by line like a game's settings file.

#include <filesystem>
#include <memory>
#include <string>

#include "game_settings.h"

namespace coreoflight {

struct HubSettings {
  std::string language = "auto";  // "auto" (Windows' language), "german", "english"
  bool figures_only_mine = true;  // the figure library opens on the owner's figures, not all
  bool figures_marks = true;      // Y marks a figure without a file as owned ("Hab ich")
  bool title_music = true;        // a game's title music while its card is chosen
  bool animations = true;         // screens slide and fade in
  bool fullscreen = false;        // the window fills the screen (F11 / Alt+Enter switch it too)
  bool auto_backup = true;        // the saves copied at a game's start, end and every 60 minutes
  bool setup_done = false;        // the first-run setup (setup_ui.h) finished or skipped
};

std::filesystem::path HubSettingsFile();
HubSettings LoadHubSettings();
// Whether the menus are German with these settings.
bool HubGerman(const HubSettings& settings);
// The settings as pages for SettingsScreen (one page, the current values).
std::shared_ptr<GameSettings> HubSettingsPages(const HubSettings& settings);

}  // namespace coreoflight
