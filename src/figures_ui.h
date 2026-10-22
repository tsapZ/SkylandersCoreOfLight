#pragma once

// The figure library (figures_ui.cpp, #8): the owner's Skylanders from the shared figure folder,
// with their level, gold and play time, by game, and every Skylander of a game with the ones the
// owner has marked, without a game running. The figure files are read with skylanders-portal.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "portal/figure_info.h"
#include "portal/figure_stats.h"

struct ImFont;

namespace coreoflight {

class FiguresScreen {
 public:
  void Init(ImFont* regular, ImFont* bold, bool german);
  // Reads the figure folder again and opens the screen.
  void Open(const std::filesystem::path& figures_dir);
  bool is_open() const { return open_; }
  void Draw(float width, float height);

 private:
  struct Figure {
    std::string name, game, category, badge;
    skylanders::portal::Element element = skylanders::portal::Element::kNone;
    uint16_t id = 0, variant = 0;
    bool owned = false;  // a figure file of it is in the folder
    std::optional<skylanders::portal::FigureStats> stats;
    std::filesystem::path file;
  };
  std::vector<const Figure*> Shown() const;

  ImFont* regular_ = nullptr;
  ImFont* bold_ = nullptr;
  bool german_ = false;
  bool open_ = false;
  std::vector<Figure> owned_;    // one per figure file
  std::vector<Figure> catalog_;  // every released Skylander, marked if owned
  int tab_ = 0;                  // 0 = every game, then the games in release order
  bool all_ = false;             // every Skylander of the game, not only the owner's
  int selected_ = 0;
};

}  // namespace coreoflight
