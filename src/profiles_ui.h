#pragma once

// The profile screen (profiles_ui.cpp, #4): a card per profile with its picture, its figures and
// the games it has saves of; choose the one to play with, make a new one (with fresh copies of the
// figures, copies as they are, or none), rename it, give it a Skylander's picture, delete it.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "games.h"
#include "profiles.h"

struct ImFont;

namespace coreoflight {

class ProfilesScreen {
 public:
  void Init(ImFont* regular, ImFont* bold, bool german);
  // Reads the profiles again and opens the screen; `found` names the games for the saves shown.
  void Open(const std::vector<Found>* found);
  bool is_open() const { return open_; }
  void Draw(float width, float height);
  // True once after the active profile (or its name or picture) changed.
  bool TakeChanged();

 private:
  enum class Mode { kList, kName, kPicture, kDelete, kNewFigures };
  void Load();
  void DrawCard(size_t index, float x, float y, float w, float h, bool selected);
  void StartPictureChoice();
  void Message(const std::string& text);

  ImFont* regular_ = nullptr;
  ImFont* bold_ = nullptr;
  bool german_ = false;
  bool open_ = false;
  bool changed_ = false;
  const std::vector<Found>* found_ = nullptr;
  std::vector<Profile> profiles_;
  // Per profile: its figure count and the games it has saves of.
  std::vector<int> figure_counts_;
  std::vector<std::string> saves_;
  std::vector<uint32_t> gamerscores_;  // over every game whose list is known
  size_t selected_ = 0;  // profiles_.size() is the "new profile" card
  Mode mode_ = Mode::kList;
  char name_[64] = {};
  bool focus_name_ = false;
  std::vector<std::pair<uint16_t, uint16_t>> pictures_;  // the choice for kPicture
  size_t picture_ = 0;
  std::string message_;
  double message_until_ = 0;
};

}  // namespace coreoflight
