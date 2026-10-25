#pragma once

// The figure library (figures_ui.cpp, #8): the owner's Skylanders from the shared figure folder,
// with their level, gold and play time, by game, and every Skylander of a game with the ones the
// owner has marked, without a game running. The figure files are read with skylanders-portal.

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "portal/figure_info.h"
#include "portal/figure_stats.h"
#include "portal/trap_villain.h"
#include "profiles.h"

struct ImFont;

namespace coreoflight {

class FiguresScreen {
 public:
  void Init(ImFont* regular, ImFont* bold, bool german);
  // The launcher's settings (hub_settings.h): open on the owner's figures or on all, and whether Y
  // marks a figure without a file as owned. Before Open.
  void Configure(bool only_mine, bool marks);
  // Reads the figure folder again and opens the screen.
  void Open(const std::filesystem::path& figures_dir);
  bool is_open() const { return open_; }
  void Draw(float width, float height);

 private:
  struct Figure {
    std::string name, game, category, badge;
    skylanders::portal::Element element = skylanders::portal::Element::kNone;
    uint16_t id = 0, variant = 0;
    bool owned = false;   // a figure file of it is in the folder, or the owner marked it
    bool marked = false;  // owned only by the owner's mark (no file of it in the folder)
    std::optional<skylanders::portal::FigureStats> stats;
    std::optional<skylanders::portal::TrapContents> trap;  // a Trap Team trap: the villain inside
    std::filesystem::path file;
    // Ever played: its data has play time, XP, gold or a last played date (#15: a figure that
    // never was is shown slightly greyed, real or made with the emulated portal alike).
    bool played() const {
      return stats && (stats->play_seconds || stats->xp || stats->gold || stats->last_played);
    }
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
  int tab_ = 0;                  // 0 = every game, then the games in release order, then items
  bool marks_enabled_ = true;    // "I own it" marks (hub_settings.h)
  bool all_ = false;             // every Skylander of the game, not only the owner's
  int selected_ = 0;
  // The chip under the tabs (the owner's wish, 2026-10-25: a game's figures by element, to get
  // through them faster; traps and vehicles on their own): 0 every figure, a skylanders::portal::
  // Element's number for that element's characters, or one of the kinds below.
  int element_ = 0;
  static constexpr int kKindCrystals = -1, kKindVehicles = -2, kKindTraps = -3;
  // A figure's kind for the chips: one of the kinds above, or 0 for a character.
  static int KindOf(const Figure& f);
  // The chips the current tab has (its figures', whichever chip is chosen).
  std::vector<int> Elements() const;
  // Documents\Skylanders\Pictures, one folder per game: the profile's (next to its figure folder),
  // then the main profile's (the discs' pictures are the same for every profile).
  std::vector<std::filesystem::path> pictures_dirs_;
  std::map<std::pair<uint16_t, uint16_t>, std::filesystem::path> pictures_;  // empty path: none
  // The hats the games left (Pictures\<game>\hats.txt and hat-<n>.png), the newest game's.
  struct Hat {
    std::string name, bonuses;
    std::filesystem::path picture;
  };
  std::map<uint16_t, Hat> hats_;

  // A figure's upgrade tree as a game left it (Pictures\<game>\upgrades-<id>-<variant>.txt and its
  // pictures, skylanders-portal StartFigurePictureCache).
  struct Upgrade {
    std::string name, description;
    uint32_t cost = 0;
    int bit = -1, alternate_bit = -1;
    std::filesystem::path picture;
  };
  struct UpgradePath {
    int kind = 1;     // 0 start, 1 basic, 2 path, 3 soul gem, 4 wow pow (FigureUpgradePath::Kind)
    int number = -1;  // which of the two paths
    std::string name, description;
    std::vector<Upgrade> upgrades;
  };
  struct UpgradeTree {
    std::vector<UpgradePath> paths;
  };
  // nullptr if no game has left one for the figure.
  const UpgradeTree* TreeOf(const Figure& f);
  bool Owned(const Figure& f, const UpgradePath& path, const Upgrade& u) const;
  void DrawTree(float width, float height, float u);
  std::map<std::pair<uint16_t, uint16_t>, std::shared_ptr<UpgradeTree>> trees_;
  bool tree_ = false;  // the tree of tree_figure_ is open
  Figure tree_figure_;
  int tree_index_ = 0;  // the chosen upgrade, counted over all paths

  // Y on one of the owner's figures: its file copied, as it is, into another profile's figure
  // folder (profiles.h), the same sub-folder, a new name if one is there already.
  void DrawCopy(float width, float height, float u);
  // The Rangliste (#13): the profile's figures ranked, one list at a time (Back / R).
  void DrawBoard(float width, float height, float u);
  bool board_ = false;
  int board_list_ = 0;
  int board_scroll_ = 0;
  int grid_scroll_ = 0;  // the grid's first row shown (follows the chosen tile only when it leaves the view)
  bool copying_ = false;
  std::vector<Profile> copy_targets_;
  int copy_target_ = 0;
  std::filesystem::path figures_dir_;  // the folder the library shows
  Figure copy_figure_;

  // Y on a figure without a file: "I own it" (#8), for figures the owner has on the shelf but
  // not as a file. Kept per profile next to its figure folder, Skylanders\owned.txt, one
  // "<id>\t<variant>" a line; the games never read it.
  std::filesystem::path MarksFile() const;
  void ToggleMark(const Figure& f);
  std::set<std::pair<uint16_t, uint16_t>> marked_;
  std::string message_;
  double message_until_ = 0;
};

}  // namespace coreoflight
