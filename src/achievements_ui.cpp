#include "achievements_ui.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "games.h"
#include "imgui.h"
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

}  // namespace

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
  dl->AddRectFilledMultiColor({0, 0}, {width, height}, Rgba(10, 22, 60), Rgba(10, 22, 60), Rgba(3, 7, 22),
                              Rgba(3, 7, 22));
  Text(dl, bold_, 22 * u, {48 * u, 34 * u}, kDim, "SKYLANDERS");
  Text(dl, bold_, 56 * u, {46 * u, 56 * u}, kGold, german_ ? "ERFOLGE" : "ACHIEVEMENTS");
  // Whose they are.
  const std::string whose = (german_ ? "Profil " : "Profile ") + ActiveProfile().name;
  Text(dl, regular_, 20 * u, {width - 48 * u - TextWidth(regular_, 20 * u, whose), 48 * u}, kDim, whose);

  auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
  auto once = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
  const size_t games = AllGames().size();
  if (once(ImGuiKey_GamepadL1) || once(ImGuiKey_PageUp) || once(ImGuiKey_Q)) {
    game_ = (game_ + games - 1) % games;
    Load();
  }
  if (once(ImGuiKey_GamepadR1) || once(ImGuiKey_PageDown) || once(ImGuiKey_E)) {
    game_ = (game_ + 1) % games;
    Load();
  }
  if (once(ImGuiKey_Escape) || once(ImGuiKey_GamepadFaceRight)) open_ = false;

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
    // How far, as a bar.
    const float bar_w = std::min(420 * u, width - 2 * left);
    const float done = list_.items.empty() ? 0.0f : float(list_.unlocked) / float(list_.items.size());
    dl->AddRectFilled({width - left - bar_w, top + 8 * u}, {width - left, top + 22 * u}, Rgba(40, 50, 100), 7 * u);
    dl->AddRectFilled({width - left - bar_w, top + 8 * u}, {width - left - bar_w + bar_w * done, top + 22 * u},
                      kGreen, 7 * u);

    const int n = int(list_.items.size());
    if (pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_GamepadDpadUp) || pressed(ImGuiKey_GamepadLStickUp)) {
      selected_ = std::max(0, selected_ - 1);
    }
    if (pressed(ImGuiKey_DownArrow) || pressed(ImGuiKey_GamepadDpadDown) || pressed(ImGuiKey_GamepadLStickDown)) {
      selected_ = std::min(n - 1, selected_ + 1);
    }
    if (io.MouseWheel != 0) selected_ = std::clamp(selected_ - int(io.MouseWheel) * 3, 0, std::max(0, n - 1));
    // Rows of two columns on a wide window.
    const int columns = width > 1100 * u ? 2 : 1;
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
          (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
        selected_ = i;
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

  float hx = 48 * u;
  const float hy = height - 52 * u;
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "LB", Rgba(80, 90, 140), german_ ? "Spiel davor" : "Previous game");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "RB", Rgba(80, 90, 140), german_ ? "N\xC3\xA4" "chstes Spiel" : "Next game");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
  ImGui::End();
}

}  // namespace coreoflight
