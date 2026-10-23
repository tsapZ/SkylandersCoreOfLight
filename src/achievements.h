#pragma once

// A game's achievements and a profile's progress in them (achievements.cpp), without the game
// running. The list is the game's own: each game leaves it in its picture folder
// (Pictures\<game>\achievements.txt with the title's pictures, skylanders-portal
// StartAchievementCache). What is unlocked is the profile's: the SDK keeps it in the game's user
// data, achievements\<title id>.toml ("[unlocked.<id>]" with the Windows file time).

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace coreoflight {

struct Game;
struct Profile;

struct Achievement {
  uint32_t id = 0;
  uint32_t gamerscore = 0;
  std::filesystem::path picture;  // empty: none
  std::string name, description, locked_description;
  uint64_t unlocked = 0;  // Windows file time; 0 = still locked
};

struct GameAchievements {
  bool known = false;  // the game left its list
  std::vector<Achievement> items;
  int unlocked = 0;
  uint32_t gamerscore = 0, gamerscore_total = 0;
  // Every one unlocked: the launcher's platinum trophy (achievements_ui.h DrawPlatinumTrophy), from
  // the time of the last one.
  bool platinum = false;
  uint64_t platinum_time = 0;
};

GameAchievements LoadAchievements(const Game& game, const Profile& profile);

// "22.10.2026" (German) or "2026-10-22" for a Windows file time, in local time.
std::string FileTimeDate(uint64_t filetime, bool german);

}  // namespace coreoflight
