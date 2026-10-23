#include "figures_ui.h"
#include "hints.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

#include "games.h"
#include "profiles.h"
#include "imgui.h"
#include "nxe_art.h"
#include "portal/figure_catalog.h"
#include "portal/figure_file.h"
#include "textures.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;
namespace sp = skylanders::portal;

ImU32 Rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

const ImU32 kGold = Rgba(255, 204, 64);
const ImU32 kWhite = Rgba(255, 255, 255);
const ImU32 kDim = Rgba(180, 190, 215);
const ImU32 kPanel = Rgba(24, 30, 70, 235);
const ImU32 kEdge = Rgba(90, 100, 150);

constexpr std::string_view kGames[] = {"Spyro's Adventure", "Giants", "Swap Force", "Trap Team", "SuperChargers",
                                       "Imaginators"};

ImU32 ElementColor(sp::Element e) {
  switch (e) {
    case sp::Element::kMagic: return Rgba(160, 80, 220);
    case sp::Element::kEarth: return Rgba(150, 100, 50);
    case sp::Element::kWater: return Rgba(40, 110, 220);
    case sp::Element::kFire: return Rgba(220, 70, 40);
    case sp::Element::kTech: return Rgba(240, 150, 30);
    case sp::Element::kUndead: return Rgba(110, 120, 130);
    case sp::Element::kLife: return Rgba(70, 170, 60);
    case sp::Element::kAir: return Rgba(120, 200, 240);
    case sp::Element::kLight: return Rgba(250, 220, 90);
    case sp::Element::kDark: return Rgba(80, 50, 110);
    default: return Rgba(90, 95, 120);
  }
}

const char* ElementDe(sp::Element e) {
  switch (e) {
    case sp::Element::kMagic: return "Magie";
    case sp::Element::kEarth: return "Erde";
    case sp::Element::kWater: return "Wasser";
    case sp::Element::kFire: return "Feuer";
    case sp::Element::kTech: return "Technik";
    case sp::Element::kUndead: return "Untot";
    case sp::Element::kLife: return "Leben";
    case sp::Element::kAir: return "Luft";
    case sp::Element::kLight: return "Licht";
    case sp::Element::kDark: return "Dunkelheit";
    default: return "";
  }
}

float TextWidth(ImFont* font, float size, const std::string& text) {
  return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
}

void Text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const std::string& text,
          float wrap = 0.0f) {
  dl->AddText(font, size, pos, col, text.c_str(), nullptr, wrap);
}

// Shortens `text` with "..." to fit `width`.
std::string Fit(ImFont* font, float size, std::string text, float width) {
  if (TextWidth(font, size, text) <= width) return text;
  while (!text.empty() && TextWidth(font, size, text + "...") > width) {
    text.pop_back();
    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();
  }
  return text + "...";
}

// A hint: the controller button or the keyboard key (hints.h), and what it does.
float Hint(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const char* button, ImU32 col,
           const char* label, const char* key = nullptr) {
  const float x = pos.x + HintGlyph(dl, font, size, pos, button, col, key) + size * 0.4f;
  Text(dl, font, size, {x, pos.y}, kWhite, label);
  return x + TextWidth(font, size, label) + size * 1.2f;
}

std::string Category(std::string_view c, bool german) {
  if (!german) return std::string(c);
  static const std::pair<std::string_view, std::string_view> kDe[] = {
      {"Cores", "Kern-Skylander"},     {"Variants", "Varianten"},     {"Chase Variants", "Seltene Varianten"},
      {"Items", "Magische Objekte"},   {"Sidekicks", "Sidekicks"},    {"LightCores", "LightCore"},
      {"Giants", "Giants"},            {"Swappers", "Swapper"},       {"Trap Masters", "Fallen-Meister"},
      {"Traps", "Fallen"},             {"Minis", "Minis"},            {"Eon's Elite", "Eons Elite"},
      {"Vehicles", "Fahrzeuge"},       {"Vehicle Variants", "Fahrzeug-Varianten"},
      {"SuperChargers", "SuperChargers"}, {"Senseis", "Sensei"},      {"Sensei Variants", "Sensei-Varianten"},
      {"Villain Senseis", "Schurken-Sensei"}, {"Villain Variants", "Schurken-Varianten"},
      {"Creation Crystals", "Kreationskristalle"},
  };
  for (const auto& [en, de] : kDe) {
    if (c == en) return std::string(de);
  }
  return std::string(c);
}

// The version badge in the menu's language ("Series 2" -> "Serie 2"), as the games' menus have it.
std::string Badge(const std::string& badge, bool german) {
  if (!german) return badge;
  if (badge.starts_with("Series ")) return "Serie " + badge.substr(7);
  static const std::pair<std::string_view, std::string_view> kDe[] = {
      {"Variant", "Variante"}, {"Chase", "Seltene Variante"}, {"Vehicle", "Fahrzeug"}, {"Trap", "Falle"},
      {"Trap Master", "Fallen-Meister"},
      {"Mini", "Mini-Skylander"}, {"Creation Crystal", "Kreationskristall"}, {"Villain Sensei", "Schurken-Sensei"},
  };
  for (const auto& [en, de] : kDe) {
    if (badge == en) return std::string(de);
  }
  return badge;
}

std::string PlayTime(uint32_t seconds, bool german) {
  char text[48];
  if (seconds >= 3600) {
    std::snprintf(text, sizeof(text), german ? "%u Std. %u Min." : "%u h %u min", seconds / 3600, seconds / 60 % 60);
  } else {
    std::snprintf(text, sizeof(text), german ? "%u Min." : "%u min", seconds / 60);
  }
  return text;
}

}  // namespace

void FiguresScreen::Init(ImFont* regular, ImFont* bold, bool german) {
  regular_ = regular;
  bold_ = bold;
  german_ = german;
}

void FiguresScreen::Configure(bool only_mine, bool marks) {
  all_ = !only_mine;
  marks_enabled_ = marks;
}

void FiguresScreen::Open(const fs::path& figures_dir) {
  open_ = true;
  copying_ = false;
  figures_dir_ = figures_dir;
  selected_ = 0;
  // Each game leaves the pictures it reads from its disc there (skylanders-portal
  // StartFigurePictureCache): looked for again on every open.
  pictures_dirs_ = {figures_dir.parent_path() / "Pictures"};
  if (MainFiguresDir().parent_path() / "Pictures" != pictures_dirs_[0]) {
    pictures_dirs_.push_back(MainFiguresDir().parent_path() / "Pictures");
  }
  pictures_.clear();
  trees_.clear();
  tree_ = false;
  // The hats: each game's hats.txt, read oldest game first so the newest one's names win (and
  // the profile's after the main profile's).
  hats_.clear();
  for (const std::string_view game : kGames) {
    for (size_t root = pictures_dirs_.size(); root-- > 0;) {
      const fs::path dir = pictures_dirs_[root] / fs::path(std::u8string(game.begin(), game.end()));
      std::ifstream in(dir / "hats.txt", std::ios::binary);
      std::string line;
      while (std::getline(in, line)) {
        const size_t tab1 = line.find('\t'), tab2 = line.find('\t', tab1 + 1);
        if (tab1 == std::string::npos || tab2 == std::string::npos) continue;
        const int number = std::atoi(line.substr(0, tab1).c_str());
        if (number <= 0 || number > 0xFFFF) continue;
        Hat hat{line.substr(tab1 + 1, tab2 - tab1 - 1), line.substr(tab2 + 1), {}};
        const fs::path picture = dir / ("hat-" + std::to_string(number) + ".png");
        std::error_code exists_ec;
        Hat& known = hats_[uint16_t(number)];
        // A newer game's name, but an older game's picture when the newer one left none:
        // Imaginators names 266 hats and leaves 3 of their pictures, Trap Team 35 of 245, while
        // Swap Force and SuperChargers leave all of theirs.
        hat.picture = fs::exists(picture, exists_ec) ? picture : known.picture;
        known = std::move(hat);
      }
    }
  }
  owned_.clear();
  catalog_.clear();
  std::set<std::pair<uint16_t, uint16_t>> have;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(figures_dir, ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file(ec)) continue;
    // The games' own copies of a figure before writing to it are not figures of their own.
    if (it->path().string().find("\\real\\") != std::string::npos) continue;
    const auto data = sp::LoadFigureFile(it->path());
    if (!data) continue;
    Figure f;
    f.id = sp::ReadFigureId(*data);
    f.variant = sp::ReadFigureVariant(*data);
    f.file = it->path();
    f.owned = true;
    f.stats = sp::ParseFigureStats(*data);
    f.trap = sp::ReadTrapVillain(*data);
    if (const sp::SkylanderInfo* info = sp::FindSkylander(f.id, f.variant)) {
      const std::string shown = sp::FigureDisplayName(*data);
      f.name = sp::DisplayName(shown.empty() ? info->name : shown);
      f.game = std::string(info->game);
      f.category = std::string(info->category);
      f.badge = sp::VersionBadge(*info);
      f.element = sp::FigureElement(*info);
    } else {
      f.name = it->path().stem().string();
      f.category = german_ ? "Unbekannt" : "Unknown";
    }
    have.insert({f.id, f.variant});
    owned_.push_back(std::move(f));
  }
  // The figures the owner marked as owned without a file of them.
  marked_.clear();
  if (marks_enabled_) {
    std::ifstream in(MarksFile(), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
      const size_t tab = line.find('\t');
      if (line.empty() || line[0] == '#' || tab == std::string::npos) continue;
      const long id = std::strtol(line.c_str(), nullptr, 10), variant = std::strtol(line.c_str() + tab + 1, nullptr, 10);
      if (id < 0 || id > 0xFFFF || variant < 0 || variant > 0xFFFF) continue;
      marked_.insert({uint16_t(id), uint16_t(variant)});
    }
  }
  for (const auto& [id, variant] : marked_) {
    const sp::SkylanderInfo* info = sp::FindSkylander(id, variant);
    if (!info || have.contains({id, variant})) continue;
    Figure f;
    f.id = id;
    f.variant = variant;
    f.owned = f.marked = true;
    f.name = sp::DisplayName(info->name);
    f.game = std::string(info->game);
    f.category = std::string(info->category);
    f.badge = sp::VersionBadge(*info);
    f.element = sp::FigureElement(*info);
    owned_.push_back(std::move(f));
  }
  // In release order, then by kind of figure as the catalog orders them, then by name.
  auto game_index = [](const std::string& g) {
    for (size_t i = 0; i < std::size(kGames); ++i) {
      if (g == kGames[i]) return int(i);
    }
    return 99;
  };
  std::stable_sort(owned_.begin(), owned_.end(), [&](const Figure& a, const Figure& b) {
    return std::make_pair(game_index(a.game), a.name) < std::make_pair(game_index(b.game), b.name);
  });
  for (const sp::SkylanderInfo& info : sp::AllSkylanders()) {
    if (info.category == "Unreleased") continue;
    Figure f;
    f.id = info.id;
    f.variant = info.variant;
    f.name = sp::DisplayName(info.name);
    f.game = std::string(info.game);
    f.category = std::string(info.category);
    f.badge = sp::VersionBadge(info);
    f.element = sp::FigureElement(info);
    f.owned = have.contains({info.id, info.variant});
    f.marked = !f.owned && marked_.contains({info.id, info.variant});
    f.owned = f.owned || f.marked;
    // The owner's figure of it, the one played furthest if there are several.
    for (const Figure& mine : owned_) {
      if (mine.id != f.id || mine.variant != f.variant) continue;
      if (!f.stats || (mine.stats && mine.stats->xp > f.stats->xp)) {
        f.stats = mine.stats;
        f.file = mine.file;
      }
    }
    catalog_.push_back(std::move(f));
  }
}

fs::path FiguresScreen::MarksFile() const { return figures_dir_.parent_path() / "owned.txt"; }

void FiguresScreen::ToggleMark(const Figure& f) {
  const auto key = std::make_pair(f.id, f.variant);
  if (!marked_.erase(key)) marked_.insert(key);
  std::string text = "# Skylanders: Core of Light, the figures marked \"I own it\" (id, variant)\n";
  for (const auto& [id, variant] : marked_) text += std::to_string(id) + "\t" + std::to_string(variant) + "\n";
  std::error_code ec;
  fs::create_directories(MarksFile().parent_path(), ec);
  const fs::path temp = fs::path(MarksFile()) += ".new";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << text;
    if (!out) return;
  }
  fs::rename(temp, MarksFile(), ec);
  // The lists again (f is one of them: only `key` is used from here), the same figure chosen.
  const int tab = tab_;
  const bool all = all_;
  Open(figures_dir_);
  tab_ = tab;
  all_ = all;
  const std::vector<const Figure*> shown = Shown();
  for (size_t i = 0; i < shown.size(); ++i) {
    if (shown[i]->id == key.first && shown[i]->variant == key.second) selected_ = int(i);
  }
}

const fs::path* FiguresScreen::Picture(const Figure& f) {
  const auto key = std::make_pair(f.id, f.variant);
  auto it = pictures_.find(key);
  if (it == pictures_.end()) {
    fs::path found;
    char name[32];
    std::snprintf(name, sizeof(name), "%u-%u.png", unsigned(f.id), unsigned(f.variant));
    std::error_code ec;
    // The figure's own game first (the owner's wish, 2026-10-22: each figure as its own game shows
    // it, as games.cpp FigurePicture does for the cards), then the newest game, which knows the
    // most characters.
    std::vector<std::string_view> games;
    if (const auto* info = skylanders::portal::FindSkylander(f.id, f.variant)) games.push_back(info->game);
    for (size_t i = std::size(kGames); i-- > 0;) games.push_back(kGames[i]);
    for (const std::string_view game : games) {
      for (const fs::path& root : pictures_dirs_) {
        if (!found.empty()) break;
        const fs::path p = root / fs::path(std::u8string(game.begin(), game.end())) / name;
        if (fs::exists(p, ec)) found = p;
      }
    }
    it = pictures_.emplace(key, found).first;
  }
  return it->second.empty() ? nullptr : &it->second;
}

const FiguresScreen::UpgradeTree* FiguresScreen::TreeOf(const Figure& f) {
  const auto key = std::make_pair(f.id, f.variant);
  auto it = trees_.find(key);
  if (it != trees_.end()) return it->second.get();
  std::shared_ptr<UpgradeTree> tree;
  char name[48];
  std::snprintf(name, sizeof(name), "upgrades-%u-%u.txt", unsigned(f.id), unsigned(f.variant));
  for (size_t at = 0; at < pictures_dirs_.size() * std::size(kGames) && !tree; ++at) {
    const size_t i = std::size(kGames) - 1 - at % std::size(kGames);
    const fs::path dir =
        pictures_dirs_[at / std::size(kGames)] / fs::path(std::u8string(kGames[i].begin(), kGames[i].end()));
    std::ifstream in(dir / name, std::ios::binary);
    if (!in) continue;
    auto read = std::make_shared<UpgradeTree>();
    std::string line;
    while (std::getline(in, line)) {
      std::vector<std::string> fields;
      for (size_t at = 0;;) {
        const size_t tab = line.find('\t', at);
        fields.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
        if (tab == std::string::npos) break;
        at = tab + 1;
      }
      if (fields[0] == "P" && fields.size() >= 5) {
        read->paths.push_back({std::atoi(fields[1].c_str()), std::atoi(fields[2].c_str()), fields[3], fields[4], {}});
      } else if (fields[0] == "U" && fields.size() >= 7 && !read->paths.empty()) {
        Upgrade up{fields[1], fields[6], uint32_t(std::strtoul(fields[2].c_str(), nullptr, 10)),
                   std::atoi(fields[3].c_str()), std::atoi(fields[4].c_str()), {}};
        if (!fields[5].empty()) up.picture = dir / fields[5];
        read->paths.back().upgrades.push_back(std::move(up));
      }
    }
    if (!read->paths.empty()) tree = read;
  }
  return (trees_[key] = tree).get();
}

// As the games' details view (skylanders-portal UpgradeOwned): the two paths share their bits,
// which count for the chosen path; the other one's are its alternate bits.
bool FiguresScreen::Owned(const Figure& f, const UpgradePath& path, const Upgrade& up) const {
  if (up.bit < 0) return true;
  if (!f.stats) return false;
  if (path.kind != 2) return (f.stats->upgrades >> up.bit) & 1;
  const int chosen = sp::UpgradePath(*f.stats);
  if (chosen == 0) return false;
  const int bit = chosen == path.number + 1 ? up.bit : up.alternate_bit;
  return bit >= 0 && ((f.stats->upgrades >> bit) & 1);
}

void FiguresScreen::DrawTree(float width, float height, float u) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImGuiIO& io = ImGui::GetIO();
  const UpgradeTree* tree = TreeOf(tree_figure_);
  if (!tree || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
    tree_ = false;
    return;
  }
  // Every upgrade in order, for the choice.
  std::vector<std::pair<const UpgradePath*, const Upgrade*>> all;
  for (const auto& path : tree->paths) {
    for (const auto& up : path.upgrades) all.emplace_back(&path, &up);
  }
  if (all.empty()) {
    tree_ = false;
    return;
  }
  // Where each drawn upgrade is, for moving through the tree as it looks (below).
  struct Cell {
    int index;
    float x, y;
  };
  std::vector<Cell> cells;
  const float left = 48 * u, top = 124 * u, bottom = height - 80 * u;
  const float panel_w = std::min(380 * u, width * 0.3f);
  const float area_w = width - 2 * left - panel_w - 24 * u;
  const float col_w = area_w * 0.5f;
  const float icon = 48 * u, step = std::min(col_w / 4.0f, 110 * u), row_h = 104 * u;
  // The two paths side by side on the right, the rest on the left, as the games draw the tree.
  float y_left = top, y_right = top;
  int index = 0;
  for (const auto& path : tree->paths) {
    const bool right = path.kind == 2;
    const float x0 = left + (right ? col_w : 0.0f);
    float& y = right ? y_right : y_left;
    if (y + row_h > bottom) {  // no room: skip the rest of this column
      index += int(path.upgrades.size());
      continue;
    }
    std::string label = path.name;
    if (label.empty()) {
      switch (path.kind) {
        case 0: label = german_ ? "Angriffe" : "Attacks"; break;
        case 1: label = german_ ? "Grund-Upgrades" : "Basic upgrades"; break;
        case 3: label = german_ ? "Seelenstein" : "Soul Gem"; break;
        case 4: label = "Wow Pow"; break;
        default: label = german_ ? "Pfad" : "Path"; break;
      }
    }
    Text(dl, bold_, 18 * u, {x0, y}, kDim, Fit(bold_, 18 * u, label, col_w - 16 * u));
    for (size_t k = 0; k < path.upgrades.size(); ++k, ++index) {
      const Upgrade& up = path.upgrades[k];
      const float ix = x0 + float(k) * step;
      const ImVec2 a{ix, y + 26 * u}, b{ix + icon, y + 26 * u + icon};
      const bool owned = Owned(tree_figure_, path, up);
      const bool chosen = index == tree_index_;
      cells.push_back({index, a.x, a.y});
      if (io.MousePos.x >= a.x && io.MousePos.x < a.x + step && io.MousePos.y >= a.y && io.MousePos.y < b.y + 30 * u &&
          (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
        tree_index_ = index;
      }
      dl->AddRectFilled(a, b, owned ? Rgba(60, 70, 140) : Rgba(30, 34, 60), 10 * u);
      const ImTextureID texture = up.picture.empty() ? ImTextureID{} : LoadTexture(up.picture);
      if (texture) {
        dl->AddImageRounded(texture, a, b, {0, 0}, {1, 1}, owned ? kWhite : Rgba(255, 255, 255, 80), 10 * u);
      }
      dl->AddRect(a, b, chosen ? kGold : owned ? Rgba(110, 220, 110) : kEdge, 10 * u, 0, (chosen ? 3.0f : 1.5f) * u);
      Text(dl, regular_, 13 * u, {ix, b.y + 4 * u}, owned ? kWhite : Rgba(150, 160, 190),
           Fit(regular_, 13 * u, up.name, step - 8 * u));
    }
    y += row_h;
  }
  // The d-pad moves to the nearest drawn upgrade in its direction: along a path, between the rows
  // and across to the two paths on the right. An upgrade without room on screen isn't chosen.
  if (!cells.empty()) {
    auto at = [&](int index) -> const Cell* {
      for (const Cell& c : cells) {
        if (c.index == index) return &c;
      }
      return nullptr;
    };
    if (!at(tree_index_)) tree_index_ = cells.front().index;
    // The nearest one on the same row (or column) first, so right goes along a path and on to
    // the path beside it; else the nearest in that direction at all.
    auto move = [&](float dx, float dy) {
      const Cell* from = at(tree_index_);
      const Cell* best = nullptr;
      float best_score = 0;
      for (int pass = 0; pass < 2 && !best; ++pass) {
        for (const Cell& c : cells) {
          const float ox = c.x - from->x, oy = c.y - from->y;
          const float along = ox * dx + oy * dy, across = std::abs(ox * dy - oy * dx);
          if (along < 1.0f || (pass == 0 && across > 8 * u)) continue;
          const float score = along + 2.0f * across;
          if (!best || score < best_score) {
            best = &c;
            best_score = score;
          }
        }
      }
      if (best) tree_index_ = best->index;
    };
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    if (pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_GamepadDpadLeft) || pressed(ImGuiKey_GamepadLStickLeft)) {
      move(-1, 0);
    }
    if (pressed(ImGuiKey_RightArrow) || pressed(ImGuiKey_GamepadDpadRight) || pressed(ImGuiKey_GamepadLStickRight)) {
      move(1, 0);
    }
    if (pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_GamepadDpadUp) || pressed(ImGuiKey_GamepadLStickUp)) move(0, -1);
    if (pressed(ImGuiKey_DownArrow) || pressed(ImGuiKey_GamepadDpadDown) || pressed(ImGuiKey_GamepadLStickDown)) {
      move(0, 1);
    }
  }
  // The chosen upgrade: its picture, name, price, whether it is bought, what it does.
  const auto& [path, up] = all[size_t(std::clamp(tree_index_, 0, int(all.size()) - 1))];
  const float px = width - left - panel_w, x = px + 20 * u, w = panel_w - 40 * u;
  dl->AddRectFilled({px, top}, {px + panel_w, bottom}, kPanel, 18 * u);
  dl->AddRect({px, top}, {px + panel_w, bottom}, kEdge, 18 * u, 0, 2 * u);
  float y = top + 18 * u;
  if (!up->picture.empty()) {
    if (const ImTextureID texture = LoadTexture(up->picture)) {
      const float side = 120 * u;
      dl->AddImageRounded(texture, {px + (panel_w - side) * 0.5f, y}, {px + (panel_w + side) * 0.5f, y + side}, {0, 0},
                          {1, 1}, kWhite, 14 * u);
      y += side + 14 * u;
    }
  }
  Text(dl, bold_, 22 * u, {x, y}, kWhite, up->name, w);
  y += 32 * u;
  const bool owned = Owned(tree_figure_, *path, *up);
  std::string status = owned ? (german_ ? "Gekauft" : "Bought") : (german_ ? "Noch nicht gekauft" : "Not bought yet");
  if (up->cost) status += "  \xC2\xB7  " + std::to_string(up->cost) + " Gold";
  Text(dl, regular_, 17 * u, {x, y}, owned ? Rgba(110, 220, 110) : kGold, status, w);
  y += 30 * u;
  Text(dl, regular_, 17 * u, {x, y}, kDim, up->description, w);
  float hx = left;
  const float hy = height - 52 * u;
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
}

std::vector<const FiguresScreen::Figure*> FiguresScreen::Shown() const {
  std::vector<const Figure*> out;
  const auto& list = all_ ? catalog_ : owned_;
  // The last tab: the magic items (and adventure packs) of every game, by game, as the lists are.
  const bool items_tab = tab_ == 1 + int(std::size(kGames));
  for (const Figure& f : list) {
    const bool item = f.category == "Items";
    if (items_tab ? item : !item && (tab_ == 0 || f.game == kGames[tab_ - 1])) out.push_back(&f);
  }
  return out;
}

void FiguresScreen::Draw(float width, float height) {
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("figures", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float u = std::min(width / 1280.0f, height / 720.0f);
  // A game's tab over that game's dashboard background (kGames is AllGames' order); every game's
  // over the plain blue.
  DrawBackdrop(dl, width, height,
               tab_ > 0 && tab_ <= int(std::size(kGames)) ? GameBackground(size_t(tab_ - 1)) : fs::path(), true);
  Text(dl, bold_, 22 * u, {48 * u, 30 * u}, kDim,
       tree_ ? (german_ ? "UPGRADES" : "UPGRADES") : board_ ? (german_ ? "FIGUREN" : "FIGURES")
                                                        : (german_ ? "FIGUREN" : "FIGURES"));
  Text(dl, bold_, 44 * u, {46 * u, 52 * u}, kGold,
       tree_ ? tree_figure_.name
       : board_ ? std::string(german_ ? "Rangliste" : "Leaderboard")
       : tab_ == 1 + int(std::size(kGames))
           ? (all_ ? (german_ ? "Alle Magischen Objekte" : "Every magic item")
                   : (german_ ? "Meine Magischen Objekte" : "My magic items"))
       : all_ ? (german_ ? "Alle Skylander" : "Every Skylander") : (german_ ? "Meine Skylander" : "My Skylanders"));
  // Whose figures they are (profiles.h).
  const std::string whose = (german_ ? "Profil " : "Profile ") + ActiveProfile().name;
  Text(dl, regular_, 20 * u, {width - 48 * u - TextWidth(regular_, 20 * u, whose), 44 * u}, kDim, whose);
  if (tree_) {
    DrawTree(width, height, u);
    ImGui::End();
    return;
  }
  if (copying_) {
    DrawCopy(width, height, u);
    ImGui::End();
    return;
  }
  if (board_) {
    DrawBoard(width, height, u);
    ImGui::End();
    return;
  }
  // Back / R: the Rangliste of the profile's figures (#13).
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false) || ImGui::IsKeyPressed(ImGuiKey_R, false)) {
    board_ = true;
    board_scroll_ = 0;
    ImGui::End();
    return;
  }

  auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
  const int tabs = 2 + int(std::size(kGames));  // every game, each game, the magic items
  if (pressed(ImGuiKey_GamepadL1) || pressed(ImGuiKey_Q) || pressed(ImGuiKey_PageUp)) {
    tab_ = (tab_ + tabs - 1) % tabs;
    selected_ = 0;
  }
  if (pressed(ImGuiKey_GamepadR1) || pressed(ImGuiKey_E) || pressed(ImGuiKey_PageDown)) {
    tab_ = (tab_ + 1) % tabs;
    selected_ = 0;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false) || ImGui::IsKeyPressed(ImGuiKey_X, false) ||
      ImGui::IsKeyPressed(ImGuiKey_A, false)) {
    all_ = !all_;
    selected_ = 0;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
    open_ = false;
  }

  // Tabs: every game, then each game.
  const float left = 48 * u, top = 120 * u;
  float tx = left;
  const float tab_size = 19 * u;
  for (int i = 0; i < tabs; ++i) {
    const std::string name = i == 0                          ? (german_ ? "Alle Spiele" : "Every game")
                             : i == 1 + int(std::size(kGames)) ? (german_ ? "Magische Objekte" : "Magic items")
                                                               : std::string(kGames[i - 1]);
    const float w = TextWidth(bold_, tab_size, name) + 30 * u;
    const ImVec2 a{tx, top}, b{tx + w, top + 40 * u};
    if (io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      tab_ = i;
      selected_ = 0;
    }
    dl->AddRectFilled(a, b, i == tab_ ? kGold : Rgba(40, 50, 100, 230), 20 * u);
    Text(dl, bold_, tab_size, {tx + 15 * u, top + 9 * u}, i == tab_ ? Rgba(60, 35, 0) : kWhite, name);
    tx += w + 10 * u;
  }

  const std::vector<const Figure*> shown = Shown();
  const int n = int(shown.size());
  const float grid_top = top + 60 * u, bottom = height - 80 * u;
  const float panel_w = std::min(380 * u, width * 0.3f);
  const float grid_w = width - 2 * left - panel_w - 24 * u;
  const float tile_w = 240 * u, tile_h = 96 * u, gap = 12 * u;
  const int cols = std::max(1, int((grid_w + gap) / (tile_w + gap)));
  const int rows_visible = std::max(1, int((bottom - grid_top + gap) / (tile_h + gap)));
  if (n) {
    if (pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_GamepadDpadLeft) || pressed(ImGuiKey_GamepadLStickLeft)) {
      selected_ = std::max(0, selected_ - 1);
    }
    if (pressed(ImGuiKey_RightArrow) || pressed(ImGuiKey_GamepadDpadRight) || pressed(ImGuiKey_GamepadLStickRight)) {
      selected_ = std::min(n - 1, selected_ + 1);
    }
    if (pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_GamepadDpadUp) || pressed(ImGuiKey_GamepadLStickUp)) {
      selected_ = std::max(0, selected_ - cols);
    }
    if (pressed(ImGuiKey_DownArrow) || pressed(ImGuiKey_GamepadDpadDown) || pressed(ImGuiKey_GamepadLStickDown)) {
      selected_ = std::min(n - 1, selected_ + cols);
    }
    if (io.MouseWheel != 0) selected_ = std::clamp(selected_ - int(io.MouseWheel) * cols, 0, n - 1);
    // LT / RT (Page Up / Down): a page at a time, for the ~600 tiles of "Alle Skylander".
    if (pressed(ImGuiKey_GamepadL2) || pressed(ImGuiKey_PageUp)) {
      selected_ = std::max(0, selected_ - cols * rows_visible);
    }
    if (pressed(ImGuiKey_GamepadR2) || pressed(ImGuiKey_PageDown)) {
      selected_ = std::min(n - 1, selected_ + cols * rows_visible);
    }
  }
  selected_ = std::clamp(selected_, 0, std::max(0, n - 1));
  const int selected_row = selected_ / cols;
  const int first_row = std::max(0, selected_row - rows_visible + 1);
  if (!n) {
    Text(dl, regular_, 22 * u, {left, grid_top + 10 * u}, kDim,
         all_ ? (german_ ? "Keine Skylander." : "No Skylanders.")
              : (german_ ? "Hier sind noch keine Figuren (Ordner Dokumente\\Skylanders\\Figures)."
                         : "No figures here yet (folder Documents\\Skylanders\\Figures)."),
         grid_w);
  }
  for (int i = first_row * cols; i < std::min(n, (first_row + rows_visible) * cols); ++i) {
    const Figure& f = *shown[i];
    const int row = i / cols - first_row, col = i % cols;
    const ImVec2 a{left + col * (tile_w + gap), grid_top + row * (tile_h + gap)};
    const ImVec2 b{a.x + tile_w, a.y + tile_h};
    if (io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y &&
        (io.MouseDelta.x != 0 || io.MouseDelta.y != 0 || ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
      selected_ = i;
    }
    const bool chosen = i == selected_;
    // Not owned: faint; owned but never played: slightly greyed (#15).
    const bool fresh = f.owned && !f.played();
    const int alpha = !f.owned ? 110 : fresh ? 170 : 255;
    dl->AddRectFilled(a, b, chosen ? Rgba(60, 70, 140, 240) : Rgba(24, 30, 70, f.owned ? 235 : 150), 16 * u);
    // The figure's element, lightly: its colour fading in from the card's left edge.
    if (f.element != sp::Element::kNone) {
      const ImU32 ec = ElementColor(f.element);
      const int ea = f.owned ? (chosen ? 120 : 90) : 45;
      const ImU32 strong = (ec & 0x00FFFFFF) | (ImU32(ea) << 24), clear = ec & 0x00FFFFFF;
      dl->AddRectFilledMultiColor({a.x + 8 * u, a.y + 2 * u}, {a.x + tile_w * 0.62f, b.y - 2 * u}, strong, clear, clear,
                                  strong);
      dl->AddRectFilled(a, {a.x + 8 * u, b.y}, (ec & 0x00FFFFFF) | (ImU32(f.owned ? 230 : 110) << 24), 16 * u,
                        ImDrawFlags_RoundCornersLeft);
    }
    if (chosen) dl->AddRect(a, b, kGold, 16 * u, 0, 3 * u);
    // The game's picture of the figure, else the element as a coloured disc with the first letter.
    const ImVec2 c{a.x + 40 * u, a.y + tile_h * 0.5f};
    const fs::path* picture = Picture(f);
    const ImTextureID texture = picture ? LoadTexture(*picture) : ImTextureID{};
    if (texture) {
      const float r = 32 * u;
      const int grey = fresh ? 175 : 255;
      dl->AddImageRounded(texture, {c.x - r, c.y - r}, {c.x + r, c.y + r}, {0, 0}, {1, 1},
                          Rgba(grey, grey, grey, alpha), 10 * u);
    } else {
      const ImU32 ec = ElementColor(f.element);
      dl->AddCircleFilled(c, 26 * u, (ec & 0x00FFFFFF) | (ImU32(alpha) << 24), 32);
      const std::string initial = f.name.substr(0, 1);
      Text(dl, bold_, 24 * u, {c.x - TextWidth(bold_, 24 * u, initial) * 0.5f, c.y - 15 * u},
           Rgba(255, 255, 255, alpha), initial);
    }
    const float tx0 = a.x + 82 * u, tw = tile_w - 90 * u;
    Text(dl, bold_, 19 * u, {tx0, a.y + 12 * u}, Rgba(255, 255, 255, alpha), Fit(bold_, 19 * u, f.name, tw));
    Text(dl, regular_, 15 * u, {tx0, a.y + 38 * u}, Rgba(180, 190, 215, alpha),
         Fit(regular_, 15 * u, Category(f.category, german_), tw));
    std::string line;
    if (fresh && !f.marked) {
      line = german_ ? "noch nie gespielt" : "never played";
    } else if (f.stats) {
      char text[64];
      std::snprintf(text, sizeof(text), german_ ? "Stufe %u  \xC2\xB7  %u Gold" : "Level %u  \xC2\xB7  %u gold",
                    unsigned(f.stats->level), unsigned(f.stats->gold));
      line = text;
    } else if (!f.owned) {
      line = german_ ? "nicht dabei" : "not owned";
    } else if (f.marked) {
      line = german_ ? "hab ich (ohne Datei)" : "I own it (no file)";
    } else {
      line = german_ ? "noch nicht gespielt" : "not played yet";
    }
    Text(dl, regular_, 15 * u, {tx0, a.y + 62 * u}, f.stats && !fresh ? kGold : Rgba(150, 160, 190, alpha), line);
  }

  // Y / C: the chosen figure of the owner's into another profile.
  if (n && !shown[selected_]->file.empty() &&
      (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false) || ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
       ImGui::IsKeyPressed(ImGuiKey_C, false))) {
    copy_targets_.clear();
    for (const Profile& p : LoadProfiles(german_)) {
      if (p.id != ActiveProfile().id) copy_targets_.push_back(p);
    }
    if (copy_targets_.empty()) {
      message_ = german_ ? "Es gibt kein anderes Profil (Profile: Back im Startbildschirm)."
                         : "There is no other profile (Profiles: Back on the start screen).";
      message_until_ = ImGui::GetTime() + 5.0;
    } else {
      copy_figure_ = *shown[selected_];
      copy_target_ = 0;
      copying_ = true;
    }
  }
  // Y / C on a figure without a file: "I own it", or not any more. The lists are read again, so
  // nothing of this frame's is used after it.
  if (n && marks_enabled_ && shown[selected_]->file.empty() &&
      (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false) || ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
       ImGui::IsKeyPressed(ImGuiKey_C, false))) {
    ToggleMark(*shown[selected_]);
    ImGui::End();
    return;
  }
  // A / Enter (or a click on the chosen tile): the figure's upgrade tree, if a game left one.
  if (n && shown[selected_]->stats &&
      (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
       (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)))) {
    if (TreeOf(*shown[selected_])) {
      tree_ = true;
      tree_figure_ = *shown[selected_];
      tree_index_ = 0;
    } else {
      // Say why nothing opens: the games leave a figure's tree once they have shown it.
      message_ = german_ ? "Noch kein Upgrade-Baum: Das Spiel legt ihn ab, wenn die Figur einmal darin war."
                         : "No upgrade tree yet: the game leaves it once the figure has been in it.";
      message_until_ = ImGui::GetTime() + 5.0;
    }
  }
  // The chosen figure's details.
  const float px = width - left - panel_w;
  dl->AddRectFilled({px, grid_top}, {px + panel_w, bottom}, kPanel, 18 * u);
  dl->AddRect({px, grid_top}, {px + panel_w, bottom}, kEdge, 18 * u, 0, 2 * u);
  if (n) {
    const Figure& f = *shown[selected_];
    float y = grid_top + 18 * u;
    const float x = px + 20 * u, w = panel_w - 40 * u;
    if (const fs::path* picture = Picture(f)) {
      if (const ImTextureID texture = LoadTexture(*picture)) {
        const float side = 120 * u;
        dl->AddImageRounded(texture, {px + panel_w - 20 * u - side, y}, {px + panel_w - 20 * u, y + side}, {0, 0},
                            {1, 1}, Rgba(255, 255, 255, f.owned ? 255 : 120), 16 * u);
      }
    }
    Text(dl, bold_, 26 * u, {x, y}, kWhite, f.name, w - 130 * u);
    y += 38 * u;
    std::string sub = f.game;
    const std::string element = german_ ? std::string(ElementDe(f.element)) : std::string(sp::ElementName(f.element));
    if (!element.empty()) sub += (sub.empty() ? "" : "  \xC2\xB7  ") + element;
    Text(dl, regular_, 18 * u, {x, y}, kDim, sub, w);
    y += 28 * u;
    Text(dl, regular_, 18 * u, {x, y}, kDim, Category(f.category, german_) + (f.badge.empty() ? "" : "  \xC2\xB7  " + Badge(f.badge, german_)),
         w);
    y += 40 * u;
    // A trap: the villain caught in it (skylanders-portal trap_villain.h), as Trap Team shows it.
    if (f.trap) {
      const sp::TrapVillain* villain =
          f.trap->villain_id ? sp::FindTrapVillain(f.trap->villain_id, f.trap->variant) : nullptr;
      std::string line;
      if (!villain) {
        line = german_ ? "Leere Falle" : "Empty trap";
      } else {
        line = (german_ ? "Gefangen: " : "Caught: ") + std::string(villain->name);
        if (f.trap->evolved) line += german_ ? " (entwickelt)" : " (evolved)";
      }
      Text(dl, bold_, 20 * u, {x, y}, villain ? Rgba(110, 220, 110) : kDim, line, w);
      y += 36 * u;
    }
    if (f.stats) {
      const sp::FigureStats& s = *f.stats;
      char text[96];
      std::snprintf(text, sizeof(text), german_ ? "Stufe %u" : "Level %u", unsigned(s.level));
      Text(dl, bold_, 24 * u, {x, y}, kGold, text);
      y += 34 * u;
      // The XP bar to the next level.
      const float p = s.next_xp > s.level_xp ? float(s.xp - s.level_xp) / float(s.next_xp - s.level_xp) : 1.0f;
      dl->AddRectFilled({x, y}, {x + w, y + 14 * u}, Rgba(40, 50, 100), 7 * u);
      dl->AddRectFilled({x, y}, {x + w * std::clamp(p, 0.0f, 1.0f), y + 14 * u}, Rgba(110, 200, 255), 7 * u);
      y += 24 * u;
      std::snprintf(text, sizeof(text), s.next_xp ? "%u / %u XP" : "%u XP", unsigned(s.xp), unsigned(s.next_xp));
      Text(dl, regular_, 16 * u, {x, y}, kDim, text);
      y += 34 * u;
      auto row = [&](const char* label, const std::string& value) {
        Text(dl, regular_, 18 * u, {x, y}, kDim, label);
        Text(dl, bold_, 18 * u, {x + w - TextWidth(bold_, 18 * u, value), y}, kWhite, value);
        y += 30 * u;
      };
      row("Gold", std::to_string(s.gold));
      row(german_ ? "Spielzeit" : "Play time", PlayTime(s.play_seconds, german_));
      std::snprintf(text, sizeof(text), "%d", sp::UpgradeCount(s));
      row(german_ ? "Upgrades" : "Upgrades", text);
      if (const int path = sp::UpgradePath(s)) {
        row(german_ ? "Pfad" : "Path", path == 1 ? (german_ ? "erster" : "first") : (german_ ? "zweiter" : "second"));
      }
      if (!s.nickname.empty()) {
        row(f.category == "Creation Crystals" ? (german_ ? "Imaginator" : "Imaginator")
                                              : (german_ ? "Spitzname" : "Nickname"),
            s.nickname);
      }
      // A creation crystal's battle class, chosen when its Imaginator was made (the game's number,
      // skylanders-portal FigureStats::creation_class; the names as Imaginators' German text).
      if (f.category == "Creation Crystals" && s.creation_class) {
        static constexpr const char* kClassNames[12][2] = {
            {"Ritter", "Knight"},       {"S\xC3\xA4" "belrassler", "Swashbuckler"}, {"Boxer", "Brawler"},
            {"W\xC3\xA4" "chter", "Sentinel"}, {nullptr, nullptr}, {"Schnellsch\xC3\xBC" "tze", "Quickshot"},
            {"Bogenk\xC3\xA4" "mpfer", "Bowslinger"}, {"Panzerbrecher", "Bazooker"}, {"Zauberer", "Sorcerer"},
            {nullptr, nullptr},         {"Schmetterer", "Smasher"},                 {"Ninja", "Ninja"}};
        const uint8_t c = *s.creation_class;
        if (c < 12 && kClassNames[c][0]) row(german_ ? "Klasse" : "Class", kClassNames[c][german_ ? 0 : 1]);
      }
      if (s.hat) {
        // The hat, as the games name it, with its picture and bonuses.
        auto hat = hats_.find(s.hat);
        const std::string name = hat != hats_.end() ? hat->second.name
                                                    : (german_ ? "Nr. " : "No. ") + std::to_string(s.hat);
        row(german_ ? "Hut" : "Hat", name);
        if (hat != hats_.end()) {
          float bx = x;
          if (!hat->second.picture.empty()) {
            if (const ImTextureID texture = LoadTexture(hat->second.picture)) {
              dl->AddImageRounded(texture, {x, y}, {x + 44 * u, y + 44 * u}, {0, 0}, {1, 1}, kWhite, 8 * u);
              bx = x + 54 * u;
            }
          }
          if (!hat->second.bonuses.empty()) {
            Text(dl, regular_, 16 * u, {bx, y + 12 * u}, kGold, hat->second.bonuses, x + w - bx);
          }
          y += 52 * u;
        }
      }
      // Hero points (Giants' and Swap Force's heroic challenges, 100 at most) and the consoles the
      // figure has been on, as the figure keeps them.
      if (s.hero_points) {
        std::snprintf(text, sizeof(text), "%u / 100", unsigned(s.hero_points));
        row(german_ ? "Heldenpunkte" : "Hero points", text);
      }
      if (s.platforms) {
        std::string on;
        static constexpr const char* kPlatforms[] = {"Wii", "Xbox 360", "PS3"};
        for (int bit = 0; bit < 3; ++bit) {
          if (s.platforms & (1 << bit)) on += (on.empty() ? "" : ", ") + std::string(kPlatforms[bit]);
        }
        if (!on.empty()) row(german_ ? "War auf" : "Played on", on);
      }
      if (s.last_played) {
        std::snprintf(text, sizeof(text), "%02u.%02u.%04u", unsigned(s.last_played->day), unsigned(s.last_played->month),
                      unsigned(s.last_played->year));
        row(german_ ? "Zuletzt gespielt" : "Last played", text);
      }
    } else if (!f.trap) {
      Text(dl, regular_, 18 * u, {x, y}, kDim,
           f.marked ? (german_ ? "Als \xE2\x80\x9E" "Hab ich\xE2\x80\x9C markiert; im Figurenordner ist keine Datei von ihm. "
                                 "Y nimmt die Markierung zur\xC3\xBC" "ck."
                               : "Marked \"I own it\"; there is no file of it in the figure folder. Y takes the mark "
                                 "back.")
           : f.owned ? (german_ ? "Noch nicht gespielt: die Figur hat noch keinen Spielstand."
                                : "Not played yet: the figure has no progress saved.")
                     : !marks_enabled_ ? (german_ ? "Nicht in deinem Figurenordner." : "Not in your figure folder.")
                     : (german_ ? "Nicht in deinem Figurenordner. Hast du ihn trotzdem? Y markiert ihn als "
                                  "\xE2\x80\x9E" "Hab ich\xE2\x80\x9C."
                                : "Not in your figure folder. Do you have it anyway? Y marks it \"I own it\"."),
           w);
    }
    // The file's name at the bottom, below the rows (left out when they fill the panel).
    if (const float file_y = std::max(y + 6 * u, bottom - 30 * u); !f.file.empty() && file_y + 16 * u <= bottom) {
      const std::u8string file = f.file.filename().u8string();
      Text(dl, regular_, 14 * u, {x, file_y}, Rgba(120, 130, 160), std::string(file.begin(), file.end()), w);
    }
  }

  // The count and the hints.
  char count[96];
  if (all_) {
    const int have = int(std::count_if(shown.begin(), shown.end(), [](const Figure* f) { return f->owned; }));
    std::snprintf(count, sizeof(count), german_ ? "%d von %d dabei" : "%d of %d owned", have, n);
  } else {
    const int played = int(std::count_if(shown.begin(), shown.end(), [](const Figure* f) { return f->played(); }));
    std::snprintf(count, sizeof(count),
                  n == 1 ? (german_ ? "%d Figur  \xC2\xB7  %d gespielt" : "%d figure  \xC2\xB7  %d played")
                         : (german_ ? "%d Figuren  \xC2\xB7  %d gespielt" : "%d figures  \xC2\xB7  %d played"),
                  n, played);
  }
  Text(dl, regular_, 18 * u, {left, height - 82 * u}, kDim, count);
  if (!message_.empty() && ImGui::GetTime() < message_until_) {
    Text(dl, regular_, 18 * u, {left + TextWidth(regular_, 18 * u, count) + 30 * u, height - 82 * u},
         Rgba(110, 220, 110), message_);
  }
  float hx = left;
  const float hy = height - 52 * u;
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "LB/RB", Rgba(80, 90, 140), german_ ? "Spiel" : "Game");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "LT/RT", Rgba(80, 90, 140), german_ ? "Seite" : "Page");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 170, 70), german_ ? "Upgrades" : "Upgrades");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "X", Rgba(50, 120, 220),
            all_ ? (german_ ? "Nur meine" : "Only mine") : (german_ ? "Alle Skylander" : "Every Skylander"));
  if (n && (marks_enabled_ || !shown[selected_]->file.empty())) {
    const Figure& f = *shown[selected_];
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Y", Rgba(220, 180, 40),
              !f.file.empty() ? (german_ ? "In anderes Profil kopieren" : "Copy to another profile")
              : f.marked      ? (german_ ? "Hab ich nicht" : "I don't own it")
                              : (german_ ? "Hab ich" : "I own it"));
  }
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Back", Rgba(80, 90, 140), german_ ? "Rangliste" : "Leaderboard");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
  ImGui::End();
}

void FiguresScreen::DrawBoard(float width, float height, float u) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
      ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false) || ImGui::IsKeyPressed(ImGuiKey_R, false)) {
    board_ = false;
    return;
  }
  // The profile's figures with progress (their files'), the Skylanders only.
  std::vector<const Figure*> mine;
  for (const Figure& f : owned_) {
    if (f.stats && !f.file.empty() && f.element != sp::Element::kNone) mine.push_back(&f);
  }
  struct List {
    const char *de, *en;
  };
  static constexpr List kLists[] = {
      {"Spielzeit", "Play time"},         {"Gold", "Gold"},
      {"Level", "Level"},                 {"Upgrades", "Upgrades"},
      {"Heldenpunkte", "Hero points"},    {"Zuletzt gespielt", "Last played"},
      {"Bester pro Element", "Best of each element"}, {"Titel", "Titles"},
  };
  constexpr int kCount = int(std::size(kLists));
  if (pressed(ImGuiKey_GamepadL1) || pressed(ImGuiKey_Q) || pressed(ImGuiKey_PageUp)) {
    board_list_ = (board_list_ + kCount - 1) % kCount;
    board_scroll_ = 0;
  }
  if (pressed(ImGuiKey_GamepadR1) || pressed(ImGuiKey_E) || pressed(ImGuiKey_PageDown)) {
    board_list_ = (board_list_ + 1) % kCount;
    board_scroll_ = 0;
  }
  // The list: each figure with the value it is ranked by and how that reads.
  struct Entry {
    const Figure* f;
    double value;
    std::string text, label;
  };
  std::vector<Entry> entries;
  char text[96];
  for (const Figure* f : mine) {
    const sp::FigureStats& s = *f->stats;
    switch (board_list_) {
      case 0:
        if (s.play_seconds) entries.push_back({f, double(s.play_seconds), PlayTime(s.play_seconds, german_), {}});
        break;
      case 1:
        std::snprintf(text, sizeof(text), "%u Gold", unsigned(s.gold));
        if (s.gold) entries.push_back({f, double(s.gold), text, {}});
        break;
      case 2:
        std::snprintf(text, sizeof(text), german_ ? "Stufe %u  " "\xC2\xB7" "  %u XP" : "Level %u  " "\xC2\xB7" "  %u XP",
                      unsigned(s.level), unsigned(s.xp));
        if (s.xp) entries.push_back({f, double(s.xp), text, {}});
        break;
      case 3:
        std::snprintf(text, sizeof(text), "%d", sp::UpgradeCount(s));
        if (sp::UpgradeCount(s)) entries.push_back({f, double(sp::UpgradeCount(s)), text, {}});
        break;
      case 4:
        std::snprintf(text, sizeof(text), "%u / 100", unsigned(s.hero_points));
        if (s.hero_points) entries.push_back({f, double(s.hero_points), text, {}});
        break;
      case 5:
        if (s.last_played) {
          std::snprintf(text, sizeof(text), "%02u.%02u.%04u", unsigned(s.last_played->day),
                        unsigned(s.last_played->month), unsigned(s.last_played->year));
          entries.push_back({f, double(s.last_played->year) * 10000 + s.last_played->month * 100 + s.last_played->day,
                             text, {}});
        }
        break;
      default:
        break;
    }
  }
  if (board_list_ == 6) {
    // Each element's figure with the most XP.
    for (int e = int(sp::Element::kMagic); e <= int(sp::Element::kDark); ++e) {
      const Figure* best = nullptr;
      for (const Figure* f : mine) {
        if (int(f->element) == e && f->stats->xp && (!best || f->stats->xp > best->stats->xp)) best = f;
      }
      if (!best) continue;
      std::snprintf(text, sizeof(text), german_ ? "Stufe %u  " "\xC2\xB7" "  %u XP" : "Level %u  " "\xC2\xB7" "  %u XP",
                    unsigned(best->stats->level), unsigned(best->stats->xp));
      const sp::Element el = sp::Element(e);
      entries.push_back({best, double(100 - e), text,
                         german_ ? std::string(ElementDe(el)) : std::string(sp::ElementName(el))});
    }
  }
  if (board_list_ == 7) {
    // A title for the profile's best figure in each thing, in this order; a figure can hold several.
    struct Title {
      const char *de, *en;
      double (*score)(const sp::FigureStats&);  // the higher the better; 0 = not in the running
    };
    static constexpr Title kTitles[] = {
        {"Der Veteran", "The Veteran", [](const sp::FigureStats& s) { return double(s.play_seconds); }},
        {"Der Meister", "The Master", [](const sp::FigureStats& s) { return double(s.xp); }},
        {"Der Schatzj" "\xC3\xA4" "ger", "The Treasure Hunter", [](const sp::FigureStats& s) { return double(s.gold); }},
        {"Der T" "\xC3\xBC" "ftler", "The Tinkerer", [](const sp::FigureStats& s) { return double(sp::UpgradeCount(s)); }},
        {"Der Held", "The Hero", [](const sp::FigureStats& s) { return double(s.hero_points); }},
        // On the most consoles (Wii, Xbox 360, PS3), more than one only.
        {"Der Weltenbummler", "The Globetrotter",
         [](const sp::FigureStats& s) {
           const int n = (s.platforms & 1) + (s.platforms >> 1 & 1) + (s.platforms >> 2 & 1);
           return n > 1 ? double(n) : 0.0;
         }},
        // The played figure with the least XP: the one that waits for its turn.
        {"Der Neuling", "The Rookie", [](const sp::FigureStats& s) { return s.xp ? 1e9 - double(s.xp) : 0.0; }},
    };
    for (size_t t = 0; t < std::size(kTitles); ++t) {
      const Figure* best = nullptr;
      double best_score = 0;
      for (const Figure* f : mine) {
        const double score = kTitles[t].score(*f->stats);
        if (score > best_score) {
          best = f;
          best_score = score;
        }
      }
      if (!best) continue;
      const sp::FigureStats& s = *best->stats;
      switch (t) {
        case 0: std::snprintf(text, sizeof(text), "%s", PlayTime(s.play_seconds, german_).c_str()); break;
        case 1:
        case 6:
          std::snprintf(text, sizeof(text), german_ ? "Stufe %u  " "\xC2\xB7" "  %u XP" : "Level %u  " "\xC2\xB7" "  %u XP",
                        unsigned(s.level), unsigned(s.xp));
          break;
        case 2: std::snprintf(text, sizeof(text), "%u Gold", unsigned(s.gold)); break;
        case 3: std::snprintf(text, sizeof(text), "%d Upgrades", sp::UpgradeCount(s)); break;
        case 4: std::snprintf(text, sizeof(text), german_ ? "%u Heldenpunkte" : "%u hero points", unsigned(s.hero_points)); break;
        default:
          std::snprintf(text, sizeof(text), german_ ? "auf %d Konsolen" : "on %d consoles", int(best_score));
          break;
      }
      entries.push_back({best, double(100 - int(t)), text, german_ ? kTitles[t].de : kTitles[t].en});
    }
  }
  std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.value > b.value; });

  // The profile in one line: time and gold over all its figures, the element played most.
  {
    uint64_t seconds = 0, gold = 0;
    std::map<sp::Element, uint64_t> by_element;
    for (const Figure* f : mine) {
      seconds += f->stats->play_seconds;
      gold += f->stats->gold;
      by_element[f->element] += f->stats->play_seconds;
    }
    std::string favourite;
    uint64_t most = 0;
    for (const auto& [el, s] : by_element) {
      if (s > most) {
        most = s;
        favourite = german_ ? ElementDe(el) : std::string(sp::ElementName(el));
      }
    }
    std::string line = (german_ ? "Gesamt: " : "In all: ") + PlayTime(uint32_t(std::min<uint64_t>(seconds, UINT32_MAX)), german_) +
                       "  " "\xC2\xB7" "  " + std::to_string(gold) + " Gold";
    if (!favourite.empty()) line += std::string("  " "\xC2\xB7" "  ") + (german_ ? "Lieblingselement: " : "Favourite element: ") + favourite;
    Text(dl, regular_, 20 * u, {48 * u, 112 * u}, kDim, line);
  }
  // The lists as tabs.
  const float left = 48 * u, right = width - 48 * u;
  float tx = left;
  const float ty = 146 * u;
  for (int i = 0; i < kCount; ++i) {
    const std::string name = german_ ? kLists[i].de : kLists[i].en;
    const float w = TextWidth(bold_, 18 * u, name) + 26 * u;
    if (tx + w > right) break;
    const bool on = i == board_list_;
    dl->AddRectFilled({tx, ty}, {tx + w, ty + 34 * u}, on ? kGold : Rgba(30, 38, 80), 17 * u);
    Text(dl, bold_, 18 * u, {tx + 13 * u, ty + 6 * u}, on ? Rgba(60, 35, 0) : kDim, name);
    tx += w + 8 * u;
  }
  // The ranks.
  const float top = 196 * u, row_h = 56 * u, bottom = height - 96 * u;
  const int visible = std::max(1, int((bottom - top) / (row_h + 6 * u)));
  const int n = int(entries.size());
  if (pressed(ImGuiKey_DownArrow) || pressed(ImGuiKey_GamepadDpadDown) || pressed(ImGuiKey_GamepadLStickDown)) {
    board_scroll_ = std::min(std::max(0, n - visible), board_scroll_ + 1);
  }
  if (pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_GamepadDpadUp) || pressed(ImGuiKey_GamepadLStickUp)) {
    board_scroll_ = std::max(0, board_scroll_ - 1);
  }
  if (ImGui::GetIO().MouseWheel != 0) {
    board_scroll_ = std::clamp(board_scroll_ - int(ImGui::GetIO().MouseWheel), 0, std::max(0, n - visible));
  }
  if (!n) {
    Text(dl, regular_, 22 * u, {left, top + 10 * u}, kDim,
         german_ ? "Noch keine Figur mit Fortschritt: einmal spielen, dann steht sie hier."
                 : "No figure with progress yet: play one, then it shows here.");
  }
  static const ImU32 kMedals[] = {Rgba(255, 204, 64), Rgba(200, 210, 225), Rgba(205, 127, 50)};
  for (int i = board_scroll_; i < n && i < board_scroll_ + visible; ++i) {
    const Entry& en = entries[size_t(i)];
    const float y = top + (i - board_scroll_) * (row_h + 6 * u);
    dl->AddRectFilled({left, y}, {right, y + row_h}, kPanel, 12 * u);
    if (en.f->element != sp::Element::kNone) {
      dl->AddRectFilled({left, y}, {left + 8 * u, y + row_h}, ElementColor(en.f->element), 12 * u,
                        ImDrawFlags_RoundCornersLeft);
    }
    // The place: a medal for the first three.
    const ImVec2 mc{left + 44 * u, y + row_h * 0.5f};
    char place[8];
    std::snprintf(place, sizeof(place), "%d", i + 1);
    if (i < 3 && board_list_ < 6) {
      dl->AddCircleFilled(mc, 19 * u, kMedals[i]);
      Text(dl, bold_, 20 * u, {mc.x - TextWidth(bold_, 20 * u, place) * 0.5f, mc.y - 12 * u}, Rgba(40, 30, 10), place);
    } else if (board_list_ < 6) {
      Text(dl, bold_, 20 * u, {mc.x - TextWidth(bold_, 20 * u, place) * 0.5f, mc.y - 12 * u}, kDim, place);
    }
    const ImVec2 pc{left + 104 * u, y + row_h * 0.5f};
    const fs::path* picture = Picture(*en.f);
    if (const ImTextureID texture = picture ? LoadTexture(*picture) : ImTextureID{}) {
      const float r = 24 * u;
      dl->AddImageRounded(texture, {pc.x - r, pc.y - r}, {pc.x + r, pc.y + r}, {0, 0}, {1, 1}, kWhite, 8 * u);
    }
    float nx = left + 144 * u;
    if (!en.label.empty()) {
      // Titles in gold, elements in their colour; the column as wide as the widest of the list.
      float label_w = 150 * u;
      for (const Entry& other : entries) label_w = std::max(label_w, TextWidth(bold_, 20 * u, other.label) + 20 * u);
      Text(dl, bold_, 20 * u, {nx, y + 16 * u}, board_list_ == 7 ? kGold : ElementColor(en.f->element), en.label);
      nx += label_w;
    }
    Text(dl, bold_, 21 * u, {nx, y + 7 * u}, kWhite, Fit(bold_, 21 * u, en.f->name, right - nx - 320 * u));
    Text(dl, regular_, 15 * u, {nx, y + 32 * u}, kDim, Fit(regular_, 15 * u, en.f->game, right - nx - 320 * u));
    Text(dl, bold_, 21 * u, {right - 20 * u - TextWidth(bold_, 21 * u, en.text), y + 16 * u}, kGold, en.text);
  }
  float hx = left;
  const float hy = height - 52 * u;
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "LB/RB", Rgba(80, 90, 140), german_ ? "Liste" : "List");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zur" "\xC3\xBC" "ck" : "Back");
}

void FiguresScreen::DrawCopy(float width, float height, float u) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
  const int n = int(copy_targets_.size());
  if (pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_GamepadDpadUp) || pressed(ImGuiKey_GamepadLStickUp)) {
    copy_target_ = (copy_target_ + n - 1) % n;
  }
  if (pressed(ImGuiKey_DownArrow) || pressed(ImGuiKey_GamepadDpadDown) || pressed(ImGuiKey_GamepadLStickDown)) {
    copy_target_ = (copy_target_ + 1) % n;
  }
  const float pw = std::min(640 * u, width - 96 * u), row = 46 * u;
  const float ph = 120 * u + row * float(n);
  const ImVec2 a{(width - pw) * 0.5f, (height - ph) * 0.5f};
  dl->AddRectFilled({0, 0}, {width, height}, Rgba(0, 0, 0, 140));
  dl->AddRectFilled(a, {a.x + pw, a.y + ph}, kPanel, 18 * u);
  dl->AddRect(a, {a.x + pw, a.y + ph}, kGold, 18 * u, 0, 3 * u);
  Text(dl, bold_, 26 * u, {a.x + 28 * u, a.y + 20 * u}, kGold,
       Fit(bold_, 26 * u, (german_ ? "Kopieren: " : "Copy: ") + copy_figure_.name, pw - 56 * u));
  Text(dl, regular_, 18 * u, {a.x + 28 * u, a.y + 58 * u}, kDim,
       german_ ? "In welches Profil? Die Figur bleibt auch hier, wie sie ist."
               : "Into which profile? The figure stays here as well, as it is.");
  for (int i = 0; i < n; ++i) {
    const float y = a.y + 96 * u + row * float(i);
    if (i == copy_target_) {
      dl->AddRectFilled({a.x + 20 * u, y - 4 * u}, {a.x + pw - 20 * u, y + row - 10 * u}, Rgba(60, 70, 140), 10 * u);
    }
    Text(dl, bold_, 22 * u, {a.x + 36 * u, y + 2 * u}, i == copy_target_ ? kWhite : kDim, copy_targets_[size_t(i)].name);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
    const Profile& target = copy_targets_[size_t(copy_target_)];
    std::error_code ec;
    fs::path rel = fs::relative(copy_figure_.file.parent_path(), figures_dir_, ec);
    if (ec || rel.empty() || rel.native().starts_with(L"..")) rel.clear();
    const fs::path dir = ProfileFiguresDir(target) / rel;
    fs::create_directories(dir, ec);
    const std::u8string stem = copy_figure_.file.stem().u8string();
    fs::path dest = sp::UniqueFigurePath(dir, std::string(stem.begin(), stem.end()));
    // Keep the file's own extension (.sky, .bin) when it isn't .dump.
    if (copy_figure_.file.extension() != ".dump") dest.replace_extension(copy_figure_.file.extension());
    const bool copied = !fs::exists(dest, ec) && fs::copy_file(copy_figure_.file, dest, ec) && !ec;
    message_ = copied ? (german_ ? "Kopiert ins Profil " : "Copied to the profile ") + target.name
                      : (german_ ? "Konnte nicht kopieren" : "Could not copy");
    message_until_ = ImGui::GetTime() + 5.0;
    copying_ = false;
  } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
    copying_ = false;
  }
  float hx = 48 * u;
  const float hy = height - 52 * u;
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 170, 70), german_ ? "Kopieren" : "Copy");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Abbrechen" : "Cancel");
}

}  // namespace coreoflight
