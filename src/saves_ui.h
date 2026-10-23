#pragma once

// The saves screen (saves_ui.cpp, RT / S on a card): the chosen game's saves of the active profile on
// the left, the copies of the one chosen on the right (the game's own previous version and the
// launcher's backups). X backs the save up, A on a copy puts it back (after asking; the save as it
// is now is backed up first), Y opens the backups' folder.

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
  int slot_ = 0, copy_ = 0;
  int column_ = 0;        // 0 the saves, 1 the copies
  bool confirm_ = false;  // "replace the save with this copy?"
  std::string message_;
  double message_until_ = 0;
};

}  // namespace coreoflight
