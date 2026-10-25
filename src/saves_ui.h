#pragma once

// The saves screen (saves_ui.cpp, RT / S on a card): the chosen game's saves of the active profile on
// the left ("Deine Spielstände", when each was last saved and how many older versions it has; the
// game's options file apart below), the older versions of the one chosen on the right, each saying
// where it comes from (the game's own previous version, the launcher's automatic copies, the
// player's, the one kept before a restore). X backs the save up, A on a version brings it back
// (after asking; the save as it is now is kept first), Y opens the backups' folder. Reworded and
// laid out again for Core of Light #33 (the owner found the first version confusing).

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "saves.h"

struct ImFont;

namespace coreoflight {

class SavesScreen {
 public:
  void Init(ImFont* regular, ImFont* bold, bool german);
  void Open(size_t game, const std::filesystem::path& exe, std::filesystem::path background);
  bool is_open() const { return open_; }
  void Draw(float width, float height);

 private:
  void Load();
  void Message(const std::string& text);

  ImFont* regular_ = nullptr;
  ImFont* bold_ = nullptr;
  bool german_ = false;
  bool open_ = false;
  size_t game_ = 0;
  std::filesystem::path exe_;
  std::filesystem::path background_;
  std::vector<SaveSlot> slots_;
  std::vector<SaveCopy> copies_;  // of the chosen save
  std::vector<size_t> copy_counts_;  // how many older versions each save has
  int slot_ = 0, copy_ = 0;
  int column_ = 0;        // 0 the saves, 1 the copies
  bool confirm_ = false;  // "replace the save with this copy?"
  std::string message_;
  double message_until_ = 0;
};

}  // namespace coreoflight
