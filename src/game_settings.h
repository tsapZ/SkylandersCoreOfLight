#pragma once

// A game's own settings, as its F4 menu has them (game_settings.cpp): the launcher starts the
// game's exe with --settings_schema=<file> (skylanders-portal settings_schema.h), reads the pages it
// writes and saves changes into the game's settings file, line by line, as the game's menu does.

#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace coreoflight {

struct Found;

struct SettingChoice {
  std::string value, en, de;
};

struct GameSetting {
  std::string cvar;  // empty for a preset
  std::string en, de, help_en, help_de;
  std::string type;      // "bool", "int", "float", "string"
  std::string category;  // the settings file's [header]
  std::string value;     // as the game would start with it
  bool restart = false;
  bool buttons = false;  // the options are controller button combinations
  std::vector<SettingChoice> options;
  // A preset: what each option sets, (cvar, value, category, type).
  struct Assignment {
    std::string cvar, value, category, type;
  };
  std::map<std::string, std::vector<Assignment>> presets;
};

struct GameSettingsPage {
  std::string en, de;
  std::vector<GameSetting> settings;
};

struct GameSettings {
  std::filesystem::path config_file;
  std::vector<GameSettingsPage> pages;
};

// Loads a game's settings pages on a background thread (about 2-3 s: the game starts far enough
// to know them). `state` is 0 while loading, 1 done, -1 failed.
class SettingsLoader {
 public:
  void Start(const Found& game, const std::string& key);
  // Pages that are known already (the launcher's own settings, hub_settings.h): ready at once.
  void SetReady(std::shared_ptr<GameSettings> settings);
  int state() const { return shared_ ? shared_->state.load() : -1; }
  bool started() const { return bool(shared_); }
  std::shared_ptr<GameSettings> settings() const;

 private:
  // Shared with the loading thread, which may outlive a Start of its own (a game found again).
  struct Shared {
    std::atomic<int> state{0};
    std::mutex mutex;
    std::shared_ptr<GameSettings> settings;
  };
  std::shared_ptr<Shared> shared_;
};

// Writes one setting into a TOML settings file without touching the rest of it: replaces the line
// of `name`, or adds it under its [category] header, or adds the header at the end (as the games'
// own menu does, skylanders-portal settings_file.cpp). `type` decides the quoting.
bool WriteGameSetting(const std::filesystem::path& file, const std::string& category,
                      const std::string& name, const std::string& type, const std::string& value);

}  // namespace coreoflight
