#include "achievements_ui.h"
#include "hints.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "games.h"
#include "imgui.h"
#include "nxe_art.h"
#include "profiles.h"
#include "textures.h"

namespace coreoflight {
namespace {

ImU32 Rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

const ImU32 kGold = Rgba(255, 204, 64);
const ImU32 kWhite = Rgba(255, 255, 255);
const ImU32 kDim = Rgba(180, 190, 215);
const ImU32 kPanel = Rgba(24, 30, 70, 235);
const ImU32 kEdge = Rgba(90, 100, 150);
const ImU32 kGreen = Rgba(120, 200, 90);
constexpr float kPi = 3.14159265f;

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
  HintClickArea({pos.x - size * 0.2f, pos.y - size * 0.25f}, {x + TextWidth(font, size, label) + size * 0.3f, pos.y + size * 1.3f},
                button);  // a click on it presses the button (hints.h)
  return x + TextWidth(font, size, label) + size * 1.2f;
}

}  // namespace

void DrawPlatinumTrophy(ImDrawList* dl, float x, float y, float size, bool earned) {
  const ImU32 body = earned ? Rgba(222, 232, 248) : Rgba(62, 70, 104);
  const ImU32 shade = earned ? Rgba(150, 168, 200) : Rgba(48, 54, 86);
  const ImU32 edge = earned ? Rgba(110, 126, 166) : Rgba(96, 106, 146);
  const float h = size, top = y - h * 0.5f;
  const float r = h * 0.3f;  // the bowl: straight sides, a round bottom
  const float line = std::max(1.0f, h * 0.05f);
  // The handles behind the bowl.
  dl->PathArcTo({x - r, top + h * 0.17f}, h * 0.11f, kPi * 0.5f, kPi * 1.5f);
  dl->PathStroke(edge, 0, line);
  dl->PathArcTo({x + r, top + h * 0.17f}, h * 0.11f, -kPi * 0.5f, kPi * 0.5f);
  dl->PathStroke(edge, 0, line);
  dl->PathLineTo({x - r, top});
  dl->PathLineTo({x + r, top});
  dl->PathArcTo({x, top + h * 0.12f}, r, 0.0f, kPi);
  dl->PathFillConvex(body);
  dl->PathLineTo({x - r, top});
  dl->PathLineTo({x + r, top});
  dl->PathArcTo({x, top + h * 0.12f}, r, 0.0f, kPi);
  dl->PathStroke(edge, ImDrawFlags_Closed, line * 0.6f);
  // Stem and base.
  dl->AddRectFilled({x - h * 0.05f, top + h * 0.42f}, {x + h * 0.05f, top + h * 0.72f}, shade);
  dl->AddRectFilled({x - h * 0.2f, top + h * 0.7f}, {x + h * 0.2f, top + h * 0.82f}, body, h * 0.03f);
  dl->AddRectFilled({x - h * 0.27f, top + h * 0.82f}, {x + h * 0.27f, top + h}, shade, h * 0.04f);
  // A star on the bowl and a shine on its left.
  const ImVec2 c{x, top + h * 0.2f};
  ImVec2 star[10];
  for (int i = 0; i < 10; ++i) {
    const float a = -kPi * 0.5f + i * kPi / 5.0f, rad = (i % 2 ? 0.045f : 0.11f) * h;
    star[i] = {c.x + std::cos(a) * rad, c.y + std::sin(a) * rad};
  }
  dl->AddConcavePolyFilled(star, 10, earned ? Rgba(170, 190, 225) : shade);
  if (earned) {
    dl->AddRectFilled({x - r * 0.72f, top + h * 0.05f}, {x - r * 0.5f, top + h * 0.3f}, Rgba(255, 255, 255, 170),
                      h * 0.04f);
  }
}

void AchievementsScreen::Init(ImFont* regular, ImFont* bold, bool german) {
  regular_ = regular;
  bold_ = bold;
  german_ = german;
}

void AchievementsScreen::Open(size_t game) {
  game_ = std::min(game, AllGames().size() - 1);
  open_ = true;
  Load();
}

void AchievementsScreen::Load() {
  list_ = LoadAchievements(AllGames()[game_], ActiveProfile());
  selected_ = 0;
  scroll_ = 0;
  info_ = false;
}

void AchievementsScreen::Draw(float width, float height) {
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("achievements", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float u = std::min(width / 1280.0f, height / 720.0f);
  DrawBackdrop(dl, width, height, GameBackground(game_));
  Text(dl, bold_, 22 * u, {48 * u, 34 * u}, kDim, "SKYLANDERS");
  Text(dl, bold_, 56 * u, {46 * u, 56 * u}, kGold, german_ ? "ERFOLGE" : "ACHIEVEMENTS");
  // Whose they are.
  const std::string whose = (german_ ? "Profil " : "Profile ") + ActiveProfile().name;
  Text(dl, regular_, 20 * u, {width - 48 * u - TextWidth(regular_, 20 * u, whose), 48 * u}, kDim, whose);

  auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
  auto once = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
  const size_t games = AllGames().size();
  if (!info_ && (once(ImGuiKey_GamepadL1) || once(ImGuiKey_PageUp) || once(ImGuiKey_Q))) {
    game_ = (game_ + games - 1) % games;
    Load();
  }
  if (!info_ && (once(ImGuiKey_GamepadR1) || once(ImGuiKey_PageDown) || once(ImGuiKey_E))) {
    game_ = (game_ + 1) % games;
    Load();
  }
  const bool info_before = info_;  // what this frame's keys act on
  if (info_) {
    // The info view on top: B (or A again) closes it, nothing else moves.
    if (once(ImGuiKey_Escape) || once(ImGuiKey_GamepadFaceRight) || once(ImGuiKey_Enter) ||
        once(ImGuiKey_GamepadFaceDown) || ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      info_ = false;
    }
  } else if (once(ImGuiKey_Escape) || once(ImGuiKey_GamepadFaceRight)) {
    open_ = false;
  }

  // The games as tabs.
  float tx = 48 * u;
  const float ty = 128 * u;
  for (size_t i = 0; i < games; ++i) {
    const std::string name = german_ ? AllGames()[i].name_de : AllGames()[i].name_en;
    const float w = TextWidth(bold_, 20 * u, name) + 28 * u;
    const bool on = i == game_;
    dl->AddRectFilled({tx, ty}, {tx + w, ty + 36 * u}, on ? kGold : Rgba(30, 38, 80), 18 * u);
    Text(dl, bold_, 20 * u, {tx + 14 * u, ty + 6 * u}, on ? Rgba(60, 35, 0) : kDim, name);
    if (io.MousePos.x >= tx && io.MousePos.x < tx + w && io.MousePos.y >= ty && io.MousePos.y < ty + 36 * u &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !on) {
      game_ = i;
      Load();
    }
    tx += w + 10 * u;
  }

  const float left = 48 * u, top = 186 * u, bottom = height - 90 * u;
  if (!list_.known) {
    Text(dl, regular_, 22 * u, {left, top + 20 * u}, kDim,
         german_ ? "Noch keine Erfolgsliste: das Spiel legt seine Erfolge mit Bildern ab, wenn es einmal "
                   "etwa eine halbe Minute lief."
                 : "No list yet: the game leaves its achievements with their pictures once it ran for about "
                   "half a minute.",
         width - 2 * left);
  } else {
    char summary[160];
    std::snprintf(summary, sizeof(summary),
                  german_ ? "%d von %zu Erfolgen  \xC2\xB7  %u / %u G" : "%d of %zu achievements  \xC2\xB7  %u / %u G",
                  list_.unlocked, list_.items.size(), list_.gamerscore, list_.gamerscore_total);
    Text(dl, bold_, 24 * u, {left, top}, kWhite, summary);
    // How far, as a bar, and the platinum trophy in front of it: bright with its date once every
    // achievement is unlocked (then in place of the full bar).
    const float bar_w = std::min(420 * u, width - 2 * left);
    const float bar_x = width - left - bar_w;
    DrawPlatinumTrophy(dl, bar_x - 30 * u, top + 14 * u, 40 * u, list_.platinum);
    if (list_.platinum) {
      const std::string date = FileTimeDate(list_.platinum_time, german_);
      const std::string label = (german_ ? "PLATIN-TROPH\xC3\x84" "E" : "PLATINUM TROPHY") +
                                (date.empty() ? std::string() : "  \xC2\xB7  " + date);
      Text(dl, bold_, 24 * u, {bar_x, top}, Rgba(222, 232, 248), label);
    } else {
      const float done = list_.items.empty() ? 0.0f : float(list_.unlocked) / float(list_.items.size());
      dl->AddRectFilled({bar_x, top + 8 * u}, {width - left, top + 22 * u}, Rgba(40, 50, 100), 7 * u);
      dl->AddRectFilled({bar_x, top + 8 * u}, {bar_x + bar_w * done, top + 22 * u}, kGreen, 7 * u);
    }

    const int n = int(list_.items.size());
    // Rows of two columns on a wide window: up / down move a row, left / right a column.
    const int columns = width > 1100 * u ? 2 : 1;
    const bool keys = !info_before;
    if (keys && n > 0 && (once(ImGuiKey_Enter) || once(ImGuiKey_GamepadFaceDown))) info_ = true;
    if (keys && n > 0 && (pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_GamepadDpadUp) || pressed(ImGuiKey_GamepadLStickUp))) {
      if (selected_ >= columns) selected_ -= columns;
    }
    if (keys && n > 0 &&
        (pressed(ImGuiKey_DownArrow) || pressed(ImGuiKey_GamepadDpadDown) || pressed(ImGuiKey_GamepadLStickDown))) {
      selected_ = std::min(n - 1, selected_ + columns);
    }
    if (keys && n > 0 && columns > 1 &&
        (pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_GamepadDpadLeft) || pressed(ImGuiKey_GamepadLStickLeft))) {
      selected_ = std::max(0, selected_ - 1);
    }
    if (keys && n > 0 && columns > 1 &&
        (pressed(ImGuiKey_RightArrow) || pressed(ImGuiKey_GamepadDpadRight) || pressed(ImGuiKey_GamepadLStickRight))) {
      selected_ = std::min(n - 1, selected_ + 1);
    }
    if (keys && io.MouseWheel != 0) selected_ = std::clamp(selected_ - int(io.MouseWheel) * 3, 0, std::max(0, n - 1));
    const float gap = 16 * u;
    const float row_h = 84 * u;
    const float list_top = top + 44 * u;
    const int rows_visible = std::max(1, int((bottom - list_top) / (row_h + 8 * u)));
    const int row = selected_ / columns;
    if (row < scroll_) scroll_ = row;
    if (row >= scroll_ + rows_visible) scroll_ = row - rows_visible + 1;
    const float col_w = (width - 2 * left - gap * (columns - 1)) / columns;
    for (int i = scroll_ * columns; i < n && i < (scroll_ + rows_visible) * columns; ++i) {
      const Achievement& a = list_.items[size_t(i)];
      const float x = left + (i % columns) * (col_w + gap);
      const float y = list_top + (i / columns - scroll_) * (row_h + 8 * u);
      const bool chosen = i == selected_;
      const bool got = a.unlocked != 0;
      if (io.MousePos.x >= x && io.MousePos.x < x + col_w && io.MousePos.y >= y && io.MousePos.y < y + row_h &&
          (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) && !info_) {
        selected_ = i;
      }
      if (!info_before && io.MousePos.x >= x && io.MousePos.x < x + col_w && io.MousePos.y >= y &&
          io.MousePos.y < y + row_h && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        selected_ = i;
        info_ = true;
      }
      dl->AddRectFilled({x, y}, {x + col_w, y + row_h}, got ? kPanel : Rgba(16, 20, 46, 220), 14 * u);
      dl->AddRect({x, y}, {x + col_w, y + row_h}, chosen ? kGold : got ? Rgba(90, 140, 90) : kEdge, 14 * u, 0,
                  (chosen ? 3.0f : 1.5f) * u);
      const float icon = 64 * u;
      const ImVec2 c{x + 12 * u + icon * 0.5f, y + row_h * 0.5f};
      dl->AddCircleFilled(c, icon * 0.5f, Rgba(18, 18, 22));
      if (const ImTextureID texture = a.picture.empty() ? ImTextureID{} : LoadTexture(a.picture)) {
        dl->AddImageRounded(texture, {c.x - icon * 0.5f, c.y - icon * 0.5f}, {c.x + icon * 0.5f, c.y + icon * 0.5f},
                            {0, 0}, {1, 1}, got ? kWhite : Rgba(255, 255, 255, 70), icon * 0.5f);
      }
      if (got) dl->AddCircle(c, icon * 0.5f, kGreen, 48, 2.5f * u);
      const float tx0 = x + 24 * u + icon;
      char score[16];
      std::snprintf(score, sizeof(score), "%uG", a.gamerscore);
      const float score_w = TextWidth(bold_, 20 * u, score);
      Text(dl, bold_, 20 * u, {x + col_w - 14 * u - score_w, y + 10 * u}, got ? kGold : kDim, score);
      Text(dl, bold_, 21 * u, {tx0, y + 9 * u}, got ? kWhite : kDim,
           Fit(bold_, 21 * u, a.name, x + col_w - tx0 - score_w - 24 * u));
      const std::string& about = got || a.locked_description.empty() ? a.description : a.locked_description;
      Text(dl, regular_, 17 * u, {tx0, y + 36 * u}, got ? kDim : Rgba(140, 150, 180),
           Fit(regular_, 17 * u, about, x + col_w - tx0 - 14 * u));
      if (got) {
        const std::string date = FileTimeDate(a.unlocked, german_);
        if (!date.empty()) {
          const std::string when = (german_ ? "Freigeschaltet am " : "Unlocked on ") + date;
          Text(dl, regular_, 15 * u, {tx0, y + 58 * u}, kGreen, when);
        }
      }
    }
  }

  if (info_ && list_.known && selected_ >= 0 && size_t(selected_) < list_.items.size()) DrawInfo(width, height, u);
  float hx = 48 * u;
  const float hy = height - 52 * u;
  if (!info_ && list_.known && !list_.items.empty()) {
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 160, 70), "Info");
  }
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "LB", Rgba(80, 90, 140), german_ ? "Spiel davor" : "Previous game");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "RB", Rgba(80, 90, 140), german_ ? "N\xC3\xA4" "chstes Spiel" : "Next game");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
  ImGui::End();
}

void AchievementsScreen::DrawInfo(float width, float height, float u) {
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const Achievement& a = list_.items[size_t(selected_)];
  const bool got = a.unlocked != 0;
  dl->AddRectFilled({0, 0}, {width, height}, Rgba(0, 0, 0, 150));
  const float w = std::min(760 * u, width - 96 * u), h = 360 * u;
  const ImVec2 p0{(width - w) * 0.5f, (height - h) * 0.5f}, p1{p0.x + w, p0.y + h};
  dl->AddRectFilled(p0, p1, Rgba(24, 30, 70, 250), 22 * u);
  dl->AddRect(p0, p1, got ? kGold : kEdge, 22 * u, 0, 3.0f * u);
  // The picture large, round as in the list.
  const float icon = 168 * u;
  const ImVec2 c{p0.x + 40 * u + icon * 0.5f, p0.y + 40 * u + icon * 0.5f};
  dl->AddCircleFilled(c, icon * 0.5f, Rgba(18, 18, 22));
  if (const ImTextureID texture = a.picture.empty() ? ImTextureID{} : LoadTexture(a.picture)) {
    dl->AddImageRounded(texture, {c.x - icon * 0.5f, c.y - icon * 0.5f}, {c.x + icon * 0.5f, c.y + icon * 0.5f},
                        {0, 0}, {1, 1}, got ? kWhite : Rgba(255, 255, 255, 90), icon * 0.5f);
  }
  if (got) dl->AddCircle(c, icon * 0.5f, kGreen, 64, 4.0f * u);
  const float tx = p0.x + 72 * u + icon, tw = p1.x - tx - 32 * u;
  float ty = p0.y + 40 * u;
  char score[16];
  std::snprintf(score, sizeof(score), "%uG", a.gamerscore);
  Text(dl, bold_, 24 * u, {p1.x - 32 * u - TextWidth(bold_, 24 * u, score), ty}, got ? kGold : kDim, score);
  dl->AddText(bold_, 32 * u, {tx, ty}, kWhite, a.name.c_str(), nullptr, tx + tw - TextWidth(bold_, 24 * u, score) - 16 * u);
  ty += 52 * u;
  dl->AddText(regular_, 21 * u, {tx, ty}, kDim, a.description.c_str(), nullptr, tx + tw);
  ty += regular_->CalcTextSizeA(21 * u, FLT_MAX, tw, a.description.c_str()).y + 14 * u;
  if (!a.locked_description.empty() && a.locked_description != a.description) {
    const std::string locked = (german_ ? "Vor dem Freischalten: " : "Before it is unlocked: ") + a.locked_description;
    dl->AddText(regular_, 18 * u, {tx, ty}, Rgba(140, 150, 180), locked.c_str(), nullptr, tx + tw);
    ty += regular_->CalcTextSizeA(18 * u, FLT_MAX, tw, locked.c_str()).y + 14 * u;
  }
  std::string when;
  if (got) {
    const std::string date = FileTimeDate(a.unlocked, german_), clock = FileTimeClock(a.unlocked);
    when = date.empty() ? (german_ ? "Freigeschaltet" : "Unlocked")
                        : (german_ ? "Freigeschaltet am " : "Unlocked on ") + date +
                              (clock.empty() ? std::string() : (german_ ? " um " : " at ") + clock);
  } else {
    when = german_ ? "Noch nicht freigeschaltet" : "Not unlocked yet";
  }
  Text(dl, bold_, 21 * u, {tx, std::max(ty, p0.y + 230 * u)}, got ? kGreen : kDim, when);
  char count[96];
  std::snprintf(count, sizeof(count), german_ ? "%d von %zu Erfolgen dieses Spiels" : "%d of %zu of this game's achievements",
                list_.unlocked, list_.items.size());
  Text(dl, regular_, 18 * u, {p0.x + 40 * u, p1.y - 44 * u}, kDim, count);
  Hint(dl, bold_, 22 * u, {p1.x - 170 * u, p1.y - 48 * u}, "B", Rgba(200, 60, 60), german_ ? "Schlie\xC3\x9F" "en" : "Close");
}

}  // namespace coreoflight
