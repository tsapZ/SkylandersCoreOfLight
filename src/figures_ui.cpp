#include "figures_ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

#include "imgui.h"
#include "portal/figure_catalog.h"
#include "portal/figure_file.h"

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

float Hint(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const char* button, ImU32 col,
           const char* label) {
  const float bw = std::max(TextWidth(font, size * 0.8f, button) + size * 0.5f, size * 1.24f);
  const ImVec2 a{pos.x, pos.y + size * 0.55f - size * 0.62f};
  dl->AddRectFilled(a, {a.x + bw, a.y + size * 1.24f}, col, size * 0.62f);
  Text(dl, font, size * 0.8f, {a.x + (bw - TextWidth(font, size * 0.8f, button)) * 0.5f, pos.y + size * 0.13f},
       kWhite, button);
  const float x = a.x + bw + size * 0.4f;
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

void FiguresScreen::Open(const fs::path& figures_dir) {
  open_ = true;
  selected_ = 0;
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

std::vector<const FiguresScreen::Figure*> FiguresScreen::Shown() const {
  std::vector<const Figure*> out;
  const auto& list = all_ ? catalog_ : owned_;
  for (const Figure& f : list) {
    if (tab_ == 0 || f.game == kGames[tab_ - 1]) out.push_back(&f);
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
  dl->AddRectFilledMultiColor({0, 0}, {width, height}, Rgba(10, 22, 60), Rgba(10, 22, 60), Rgba(3, 7, 22),
                              Rgba(3, 7, 22));
  Text(dl, bold_, 22 * u, {48 * u, 30 * u}, kDim, german_ ? "FIGUREN" : "FIGURES");
  Text(dl, bold_, 44 * u, {46 * u, 52 * u}, kGold,
       all_ ? (german_ ? "Alle Skylander" : "Every Skylander") : (german_ ? "Meine Skylander" : "My Skylanders"));

  auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
  const int tabs = 1 + int(std::size(kGames));
  if (pressed(ImGuiKey_GamepadL1) || pressed(ImGuiKey_Q) || pressed(ImGuiKey_PageUp)) {
    tab_ = (tab_ + tabs - 1) % tabs;
    selected_ = 0;
  }
  if (pressed(ImGuiKey_GamepadR1) || pressed(ImGuiKey_E) || pressed(ImGuiKey_PageDown)) {
    tab_ = (tab_ + 1) % tabs;
    selected_ = 0;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false) || ImGui::IsKeyPressed(ImGuiKey_A, false)) {
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
    const std::string name = i == 0 ? (german_ ? "Alle Spiele" : "Every game") : std::string(kGames[i - 1]);
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
  const float tile_w = 230 * u, tile_h = 96 * u, gap = 12 * u;
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
    const int alpha = f.owned ? 255 : 110;
    dl->AddRectFilled(a, b, chosen ? Rgba(60, 70, 140, 240) : Rgba(24, 30, 70, f.owned ? 235 : 150), 16 * u);
    if (chosen) dl->AddRect(a, b, kGold, 16 * u, 0, 3 * u);
    // The element as a coloured disc with the name's first letter (no pictures here yet).
    const ImVec2 c{a.x + 34 * u, a.y + tile_h * 0.5f};
    const ImU32 ec = ElementColor(f.element);
    dl->AddCircleFilled(c, 24 * u, (ec & 0x00FFFFFF) | (ImU32(alpha) << 24), 32);
    const std::string initial = f.name.substr(0, 1);
    Text(dl, bold_, 24 * u, {c.x - TextWidth(bold_, 24 * u, initial) * 0.5f, c.y - 15 * u}, Rgba(255, 255, 255, alpha),
         initial);
    const float tx0 = a.x + 68 * u, tw = tile_w - 78 * u;
    Text(dl, bold_, 19 * u, {tx0, a.y + 12 * u}, Rgba(255, 255, 255, alpha), Fit(bold_, 19 * u, f.name, tw));
    Text(dl, regular_, 15 * u, {tx0, a.y + 38 * u}, Rgba(180, 190, 215, alpha),
         Fit(regular_, 15 * u, Category(f.category, german_), tw));
    std::string line;
    if (f.stats) {
      char text[64];
      std::snprintf(text, sizeof(text), german_ ? "Stufe %u  \xC2\xB7  %u Gold" : "Level %u  \xC2\xB7  %u gold",
                    unsigned(f.stats->level), unsigned(f.stats->gold));
      line = text;
    } else if (!f.owned) {
      line = german_ ? "nicht dabei" : "not owned";
    } else {
      line = german_ ? "noch nicht gespielt" : "not played yet";
    }
    Text(dl, regular_, 15 * u, {tx0, a.y + 62 * u}, f.stats ? kGold : Rgba(150, 160, 190, alpha), line);
  }

  // The chosen figure's details.
  const float px = width - left - panel_w;
  dl->AddRectFilled({px, grid_top}, {px + panel_w, bottom}, kPanel, 18 * u);
  dl->AddRect({px, grid_top}, {px + panel_w, bottom}, kEdge, 18 * u, 0, 2 * u);
  if (n) {
    const Figure& f = *shown[selected_];
    float y = grid_top + 18 * u;
    const float x = px + 20 * u, w = panel_w - 40 * u;
    Text(dl, bold_, 26 * u, {x, y}, kWhite, f.name, w);
    y += 38 * u;
    std::string sub = f.game;
    const std::string element = german_ ? std::string(ElementDe(f.element)) : std::string(sp::ElementName(f.element));
    if (!element.empty()) sub += (sub.empty() ? "" : "  \xC2\xB7  ") + element;
    Text(dl, regular_, 18 * u, {x, y}, kDim, sub, w);
    y += 28 * u;
    Text(dl, regular_, 18 * u, {x, y}, kDim, Category(f.category, german_) + (f.badge.empty() ? "" : "  \xC2\xB7  " + Badge(f.badge, german_)),
         w);
    y += 40 * u;
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
      if (!s.nickname.empty()) row(german_ ? "Spitzname" : "Nickname", s.nickname);
      if (s.last_played) {
        std::snprintf(text, sizeof(text), "%02u.%02u.%04u", unsigned(s.last_played->day), unsigned(s.last_played->month),
                      unsigned(s.last_played->year));
        row(german_ ? "Zuletzt gespielt" : "Last played", text);
      }
    } else {
      Text(dl, regular_, 18 * u, {x, y}, kDim,
           f.owned ? (german_ ? "Noch nicht gespielt: die Figur hat noch keinen Spielstand."
                              : "Not played yet: the figure has no progress saved.")
                   : (german_ ? "Nicht in deinem Figurenordner." : "Not in your figure folder."),
           w);
    }
    if (!f.file.empty()) {
      const std::u8string file = f.file.filename().u8string();
      Text(dl, regular_, 14 * u, {x, bottom - 30 * u}, Rgba(120, 130, 160), std::string(file.begin(), file.end()), w);
    }
  }

  // The count and the hints.
  char count[96];
  if (all_) {
    const int have = int(std::count_if(shown.begin(), shown.end(), [](const Figure* f) { return f->owned; }));
    std::snprintf(count, sizeof(count), german_ ? "%d von %d dabei" : "%d of %d owned", have, n);
  } else {
    std::snprintf(count, sizeof(count), german_ ? "%d Figuren" : "%d figures", n);
  }
  Text(dl, regular_, 18 * u, {left, height - 82 * u}, kDim, count);
  float hx = left;
  const float hy = height - 52 * u;
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "LB/RB", Rgba(80, 90, 140), german_ ? "Spiel" : "Game");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "X", Rgba(50, 120, 220),
            all_ ? (german_ ? "Nur meine" : "Only mine") : (german_ ? "Alle Skylander" : "Every Skylander"));
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
  ImGui::End();
}

}  // namespace coreoflight
