#pragma once

// The figure library (figures_ui.cpp, #8): the owner's Skylanders from the shared figure folder,
// with their level, gold and play time, by game, and every Skylander of a game with the ones the
// owner has marked, without a game running. The figure files are read with skylanders-portal.

#include <cstdint>
#include <filesystem>
#include <map>
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
  // The figure's picture from the games' picture folders (the newest game that has one), or none.
  const std::filesystem::path* Picture(const Figure& f);

  ImFont* regular_ = nullptr;
  ImFont* bold_ = nullptr;
  bool german_ = false;
  bool open_ = false;
  std::vector<Figure> owned_;    // one per figure file
  std::vector<Figure> catalog_;  // every released Skylander, marked if owned
  int tab_ = 0;                  // 0 = every game, then the games in release order
  bool all_ = false;             // every Skylander of the game, not only the owner's
  int selected_ = 0;
  std::filesystem::path pictures_dir_;  // Documents\Skylanders\Pictures, one folder per game
  std::map<std::pair<uint16_t, uint16_t>, std::filesystem::path> pictures_;  // empty path: none
  // The hats the games left (Pictures\<game>\hats.txt and hat-<n>.png), the newest game's.
  struct Hat {
    std::string name, bonuses;
    std::filesystem::path picture;
  };
  std::map<uint16_t, Hat> hats_;
};

}  // namespace coreoflight
