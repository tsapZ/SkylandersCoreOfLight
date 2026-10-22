#pragma once

// The achievements screen (achievements_ui.cpp): a game's achievements with its own pictures, the
// active profile's unlocked ones bright with their date, how many and the gamerscore; LB / RB
// switch the game.

#include <cstddef>

#include "achievements.h"

struct ImFont;

namespace coreoflight {

class AchievementsScreen {
 public:
  void Init(ImFont* regular, ImFont* bold, bool german);
  // Opens on a game (its index in AllGames), read again from the files.
  void Open(size_t game);
  bool is_open() const { return open_; }
  void Draw(float width, float height);

 private:
  void Load();

  ImFont* regular_ = nullptr;
  ImFont* bold_ = nullptr;
  bool german_ = false;
  bool open_ = false;
  size_t game_ = 0;
  GameAchievements list_;
  int selected_ = 0, scroll_ = 0;
};

}  // namespace coreoflight
