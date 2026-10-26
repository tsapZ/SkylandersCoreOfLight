#include "profiles_ui.h"
#include "hints.h"

#include <cmath>
#include <cctype>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "achievements.h"
#include "achievements_ui.h"  // DrawPlatinumTrophy
#include "imgui.h"
#include "imgui_internal.h"  // ImTextCharToUtf8
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
// A profile without a picture gets its initial on one of these.
const ImU32 kColors[] = {Rgba(80, 170, 70), Rgba(230, 130, 40), Rgba(150, 90, 220),
                         Rgba(40, 150, 220), Rgba(220, 60, 60), Rgba(240, 180, 40)};

float TextWidth(ImFont* font, float size, const std::string& text) {
  return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
}

void Text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const std::string& text,
          float wrap = 0.0f) {
  dl->AddText(font, size, pos, col, text.c_str(), nullptr, wrap);
}

void Centered(ImDrawList* dl, ImFont* font, float size, float x, float w, float y, ImU32 col,
              const std::string& text) {
  Text(dl, font, size, {x + (w - TextWidth(font, size, text)) * 0.5f, y}, col, text);
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

// The figure files in a profile's folder, as the games count them (not their backup copies).
std::vector<fs::path> FigureFiles(const Profile& p) {
  std::vector<fs::path> files;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(ProfileFiguresDir(p), ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    const auto ext = it->path().extension();
    if (!it->is_regular_file(ec) || (ext != ".dump" && ext != ".sky" && ext != ".bin")) continue;
    if (it->path().wstring().find(L"\\real\\") != std::wstring::npos) continue;
    files.push_back(it->path());
  }
  return files;
}

// The profile's picture, or its initial on a colour.
void DrawPicture(ImDrawList* dl, ImFont* bold, const Profile& p, size_t index, ImVec2 center, float radius) {
  if (p.has_picture) {
    const fs::path picture = FigurePicture(p.picture_id, p.picture_variant);
    if (const ImTextureID texture = picture.empty() ? ImTextureID{} : LoadTexture(picture)) {
      dl->AddCircleFilled(center, radius, Rgba(40, 50, 100));
      dl->AddImageRounded(texture, {center.x - radius, center.y - radius}, {center.x + radius, center.y + radius},
                          {0, 0}, {1, 1}, kWhite, radius);
      return;
    }
  }
  dl->AddCircleFilled(center, radius, kColors[index % std::size(kColors)]);
  std::string initial = p.name.empty() ? "?" : p.name.substr(0, 1);
  // The whole first character when it is more than one byte of UTF-8.
  while (initial.size() < p.name.size() && (static_cast<unsigned char>(p.name[initial.size()]) & 0xC0) == 0x80) {
    initial += p.name[initial.size()];
  }
  const float size = radius * 1.1f;
  Text(dl, bold, size, {center.x - TextWidth(bold, size, initial) * 0.5f, center.y - size * 0.55f}, kWhite, initial);
}

}  // namespace

void ProfilesScreen::Init(ImFont* regular, ImFont* bold, bool german) {
  regular_ = regular;
  bold_ = bold;
  german_ = german;
}

void ProfilesScreen::Open(const std::vector<Found>* found) {
  found_ = found;
  open_ = true;
  mode_ = Mode::kList;
  Load();
  selected_ = 0;
  for (size_t i = 0; i < profiles_.size(); ++i) {
    if (profiles_[i].id == ActiveProfile().id) selected_ = i;
  }
}

bool ProfilesScreen::TakeChanged() {
  const bool changed = changed_;
  changed_ = false;
  return changed;
}

void ProfilesScreen::Load() {
  profiles_ = LoadProfiles(german_);
  figure_counts_.clear();
  saves_.clear();
  gamerscores_.clear();
  platinums_.clear();
  for (const Profile& p : profiles_) {
    uint32_t score = 0;
    int platinums = 0;
    for (const Game& game : AllGames()) {
      const GameAchievements a = LoadAchievements(game, p);
      score += a.gamerscore;
      platinums += a.platinum ? 1 : 0;
    }
    gamerscores_.push_back(score);
    platinums_.push_back(platinums);
    figure_counts_.push_back(int(FigureFiles(p).size()));
    std::string saves;
    for (size_t i = 0; found_ && i < found_->size() && i < AllGames().size(); ++i) {
      if ((*found_)[i].exe.empty() || !HasSave(p, (*found_)[i].exe)) continue;
      if (!saves.empty()) saves += ", ";
      saves += german_ ? AllGames()[i].name_de : AllGames()[i].name_en;
    }
    saves_.push_back(saves);
  }
  selected_ = std::min(selected_, profiles_.size());
}

void ProfilesScreen::Message(const std::string& text) {
  message_ = text;
  message_until_ = ImGui::GetTime() + 6.0;
}

void ProfilesScreen::StartPictureChoice() {
  // The profile's own Skylanders first, then each game's hero: those a game has left a picture of.
  const Profile& p = profiles_[selected_];
  std::vector<std::pair<uint16_t, uint16_t>> candidates;
  for (const fs::path& file : FigureFiles(p)) {
    if (const auto data = sp::LoadFigureFile(file)) {
      candidates.emplace_back(sp::ReadFigureId(*data), sp::ReadFigureVariant(*data));
    }
  }
  for (const Game& game : AllGames()) candidates.emplace_back(game.hero_id, game.hero_variant);
  pictures_.clear();
  std::set<std::pair<uint16_t, uint16_t>> seen;
  for (const auto& c : candidates) {
    if (!seen.insert(c).second || FigurePicture(c.first, c.second).empty()) continue;
    pictures_.push_back(c);
  }
  if (pictures_.empty()) {
    Message(german_ ? "Noch keine Bilder: jedes Spiel legt die Bilder seiner Figuren ab, wenn es einmal "
                      "ein paar Minuten lief."
                    : "No pictures yet: each game leaves its figures' pictures once it ran for a few minutes.");
    return;
  }
  picture_ = 0;
  for (size_t i = 0; i < pictures_.size(); ++i) {
    if (p.has_picture && pictures_[i] == std::make_pair(p.picture_id, p.picture_variant)) picture_ = i;
  }
  mode_ = Mode::kPicture;
}

void ProfilesScreen::DrawCard(size_t index, float x, float y, float w, float h, bool selected) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float s = h / 420.0f;
  const float r = 22 * s;
  if (selected) {
    for (int k = 6; k > 0; --k) {
      const float grow = k * 7 * s;
      dl->AddRectFilled({x - grow, y - grow}, {x + w + grow, y + h + grow}, Rgba(255, 210, 90, 10), r + grow);
    }
  }
  dl->AddRectFilled({x, y}, {x + w, y + h}, kPanel, r);
  dl->AddRect({x, y}, {x + w, y + h}, selected ? kGold : kEdge, r, 0, selected ? 5 * s : 2 * s);
  if (index == profiles_.size()) {  // the "new profile" card
    const ImVec2 c{x + w * 0.5f, y + 150 * s};
    dl->AddCircle(c, 60 * s, kDim, 48, 4 * s);
    dl->AddLine({c.x - 30 * s, c.y}, {c.x + 30 * s, c.y}, kDim, 6 * s);
    dl->AddLine({c.x, c.y - 30 * s}, {c.x, c.y + 30 * s}, kDim, 6 * s);
    Centered(dl, bold_, 28 * s, x, w, y + 240 * s, kWhite, german_ ? "Neues Profil" : "New profile");
    Text(dl, regular_, 19 * s, {x + 20 * s, y + 290 * s}, kDim,
         german_ ? "Eigene Spielst\xC3\xA4nde und Figuren, deine bisherigen bleiben, wie sie sind."
                 : "Its own saves and figures; your current ones stay as they are.",
         w - 40 * s);
    return;
  }
  const Profile& p = profiles_[index];
  const bool active = p.id == ActiveProfile().id;
  DrawPicture(dl, bold_, p, index, {x + w * 0.5f, y + 120 * s}, 70 * s);
  Centered(dl, bold_, 30 * s, x, w, y + 206 * s, kWhite, Fit(bold_, 30 * s, p.name, w - 30 * s));
  if (IsMainProfile(p)) {
    Centered(dl, regular_, 17 * s, x, w, y + 244 * s, kDim,
             german_ ? "Deine Daten wie die Spiele sie kennen" : "Your data as the games keep it");
  }
  char figures[96];
  std::snprintf(figures, sizeof(figures), german_ ? "%d Figuren  \xC2\xB7  %u G" : "%d figures  \xC2\xB7  %u G",
                figure_counts_[index], gamerscores_[index]);
  Text(dl, regular_, 20 * s, {x + 22 * s, y + 280 * s}, kDim, figures);
  // Its platinum trophies, as cups behind the gamerscore.
  for (int t = 0; t < platinums_[index]; ++t) {
    DrawPlatinumTrophy(dl, x + 22 * s + TextWidth(regular_, 20 * s, figures) + (18 + t * 22) * s, y + 291 * s,
                       20 * s, true);
  }
  const std::string saves =
      saves_[index].empty() ? (german_ ? "Noch keine Spielst\xC3\xA4nde" : "No saves yet")
                            : (german_ ? "Spielst\xC3\xA4nde: " : "Saves: ") + saves_[index];
  Text(dl, regular_, 18 * s, {x + 22 * s, y + 308 * s}, kDim, saves, w - 44 * s);
  if (active) {
    const std::string tag = german_ ? "Aktiv" : "Active";
    const float tw = TextWidth(bold_, 20 * s, tag) + 28 * s;
    const ImVec2 a{x + (w - tw) * 0.5f, y + h - 54 * s};
    dl->AddRectFilled(a, {a.x + tw, a.y + 34 * s}, kGold, 17 * s);
    Text(dl, bold_, 20 * s, {a.x + 14 * s, a.y + 5 * s}, Rgba(60, 35, 0), tag);
  }
}

void ProfilesScreen::Draw(float width, float height) {
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("profiles", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float u = UiScale(width, height);
  dl->AddRectFilledMultiColor({0, 0}, {width, height}, Rgba(10, 22, 60), Rgba(10, 22, 60), Rgba(3, 7, 22),
                              Rgba(3, 7, 22));
  Text(dl, bold_, 22 * u, {48 * u, 34 * u}, kDim, "SKYLANDERS");
  Text(dl, bold_, 56 * u, {46 * u, 56 * u}, kGold, german_ ? "PROFILE" : "PROFILES");

  auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
  auto once = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
  const bool confirm = once(ImGuiKey_Enter) || once(ImGuiKey_KeypadEnter) || once(ImGuiKey_GamepadFaceDown);
  const bool back = once(ImGuiKey_Escape) || once(ImGuiKey_GamepadFaceRight);
  const bool left = pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_GamepadDpadLeft) ||
                    pressed(ImGuiKey_GamepadLStickLeft);
  const bool right = pressed(ImGuiKey_RightArrow) || pressed(ImGuiKey_GamepadDpadRight) ||
                     pressed(ImGuiKey_GamepadLStickRight);
  const size_t n = profiles_.size() + 1;

  // The cards in a row, moved along when there are more than fit.
  const float gap = 24 * u;
  const float card_w = std::min(270 * u, (width - 96 * u - gap * 3) / 4);
  const float card_h = card_w * 1.6f;
  const size_t fit = std::max<size_t>(1, size_t((width - 96 * u + gap) / (card_w + gap)));
  const size_t first = selected_ < fit ? 0 : selected_ - fit + 1;
  const size_t shown = std::min(fit, n - first);
  const float x0 = (width - (card_w * shown + gap * (shown - 1))) * 0.5f;
  const float y0 = 150 * u + (height - 240 * u - 150 * u - card_h) * 0.5f;
  bool clicked = false;
  float chosen_x = -1;  // the chosen card's left edge, if it is on screen
  for (size_t k = 0; k < shown; ++k) {
    const size_t i = first + k;
    const float x = x0 + k * (card_w + gap);
    if (i == selected_) chosen_x = x;
    const bool hovered = io.MousePos.x >= x && io.MousePos.x < x + card_w && io.MousePos.y >= y0 &&
                         io.MousePos.y < y0 + card_h;
    if (mode_ == Mode::kList && hovered && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) selected_ = i;
    if (mode_ == Mode::kList && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      clicked = selected_ == i;
      selected_ = i;
    }
    DrawCard(i, x, y0, card_w, card_h, i == selected_);
  }

  const float panel_w = std::min(720 * u, width - 96 * u);
  const ImVec2 panel{(width - panel_w) * 0.5f, y0 + card_h * 0.25f};
  auto draw_panel = [&](float h) {
    dl->AddRectFilled({0, 0}, {width, height}, Rgba(0, 0, 0, 140));
    dl->AddRectFilled(panel, {panel.x + panel_w, panel.y + h}, kPanel, 18 * u);
    dl->AddRect(panel, {panel.x + panel_w, panel.y + h}, kGold, 18 * u, 0, 3 * u);
  };
  float hx = 48 * u;
  const float hy = height - 52 * u;
  const bool on_profile = selected_ < profiles_.size();

  // Buttons under the chosen card: what it can do, for the mouse, each with its pad button.
  bool act_create = false, act_picture = false, act_name = false, act_delete = false;
  if (mode_ == Mode::kList && chosen_x >= 0) {
    struct Action {
      const char* pad;
      std::string label;
      ImU32 col;
      bool* hit;
    };
    std::vector<Action> actions;
    if (on_profile) {
      actions.push_back({"Y", german_ ? "Bild" : "Picture", Rgba(220, 180, 40), &act_picture});
      actions.push_back({"X", german_ ? "Name" : "Name", Rgba(50, 120, 220), &act_name});
      if (selected_ > 0) {
        actions.push_back({"Back", german_ ? "L\xC3\xB6schen" : "Delete", Rgba(200, 60, 60), &act_delete});
      }
    } else {
      actions.push_back({"A", german_ ? "Erstellen" : "Create", Rgba(70, 170, 70), &act_create});
    }
    const float bh = 40 * u, bgap = 8 * u;
    const float bw = (card_w - bgap * float(actions.size() - 1)) / float(actions.size());
    const float by = y0 + card_h + 16 * u;
    for (size_t k = 0; k < actions.size(); ++k) {
      const ImVec2 a{chosen_x + float(k) * (bw + bgap), by}, b{a.x + bw, by + bh};
      const bool over = io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y;
      dl->AddRectFilled(a, b, over ? Rgba(70, 80, 150, 245) : Rgba(36, 44, 96, 235), 20 * u);
      dl->AddRect(a, b, actions[k].col, 20 * u, 0, 2 * u);
      const float ts = 17 * u;
      const std::string text = std::string(actions[k].pad) + "  " + actions[k].label;
      Text(dl, bold_, ts, {a.x + (bw - TextWidth(bold_, ts, text)) * 0.5f, a.y + (bh - ts) * 0.5f - 1 * u}, kWhite,
           text);
      if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) *actions[k].hit = true;
    }
  }

  switch (mode_) {
    case Mode::kList: {
      if (left) selected_ = (selected_ + n - 1) % n;
      if (right) selected_ = (selected_ + 1) % n;
      if (confirm || clicked || act_create) {
        if (!on_profile) {
          char name[32];
          std::snprintf(name, sizeof(name), german_ ? "Profil %zu" : "Profile %zu", profiles_.size() + 1);
          const std::string id = CreateProfile(name);
          if (id.empty()) {
            Message(german_ ? "Das Profil lie\xC3\x9F sich nicht anlegen (Dokumente\\Skylanders\\Profiles)."
                            : "Could not make the profile (Documents\\Skylanders\\Profiles).");
          } else {
            Load();
            for (size_t i = 0; i < profiles_.size(); ++i) {
              if (profiles_[i].id == id) selected_ = i;
            }
            mode_ = Mode::kNewFigures;
          }
        } else {
          SetActiveProfile(profiles_[selected_]);
          changed_ = true;
          open_ = false;
        }
      } else if (on_profile && (act_picture || once(ImGuiKey_GamepadFaceUp) || once(ImGuiKey_Tab))) {
        StartPictureChoice();
      } else if (on_profile && (act_name || once(ImGuiKey_GamepadFaceLeft) || once(ImGuiKey_F2))) {
        std::snprintf(name_, sizeof(name_), "%s", profiles_[selected_].name.c_str());
        focus_name_ = true;
        mode_ = Mode::kName;
      } else if (on_profile && selected_ > 0 &&
                 (act_delete || once(ImGuiKey_GamepadBack) || once(ImGuiKey_Delete))) {
        delete_chosen_ = false;
        mode_ = Mode::kDelete;
      } else if (back) {
        open_ = false;
      }
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 170, 70),
                on_profile ? (german_ ? "Mit diesem Profil spielen" : "Play with this profile")
                           : (german_ ? "Anlegen" : "Make it"));
      if (on_profile) {
        hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Y", Rgba(220, 180, 40), german_ ? "Bild" : "Picture", "Tab");
        hx = Hint(dl, bold_, 22 * u, {hx, hy}, "X", Rgba(50, 120, 220), german_ ? "Name" : "Name", "F2");
        if (selected_ > 0) {
          hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Back", Rgba(80, 90, 140), german_ ? "L\xC3\xB6schen" : "Delete", "Del");
        }
      }
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
      break;
    }
    case Mode::kNewFigures: {
      const Profile& from = ActiveProfile();
      draw_panel(290 * u);
      float y = panel.y + 22 * u;
      const float x = panel.x + 28 * u, w = panel_w - 56 * u;
      Text(dl, bold_, 28 * u, {x, y}, kGold,
           Fit(bold_, 28 * u, (german_ ? "Figuren f\xC3\xBCr " : "Figures for ") + profiles_[selected_].name, w));
      y += 46 * u;
      const std::string source = (german_ ? "Aus dem Profil " : "From the profile ") + from.name + ":";
      Text(dl, regular_, 20 * u, {x, y}, kDim, source, w);
      y += 40 * u;
      float lx = x;
      lx = Hint(dl, bold_, 22 * u, {lx, y}, "A", Rgba(70, 170, 70),
                german_ ? "Neu anfangen (wie frisch gekauft)" : "Start over (as if just bought)");
      y += 40 * u;
      lx = Hint(dl, bold_, 22 * u, {x, y}, "X", Rgba(50, 120, 220),
                german_ ? "Kopien, wie sie sind" : "Copies as they are");
      y += 40 * u;
      lx = Hint(dl, bold_, 22 * u, {x, y}, "Y", Rgba(220, 180, 40), german_ ? "Ohne Figuren" : "No figures");
      y += 40 * u;
      lx = Hint(dl, bold_, 22 * u, {x, y}, "B", Rgba(200, 60, 60),
                german_ ? "Abbrechen (kein neues Profil)" : "Cancel (no new profile)");
      (void)lx;
      const bool fresh = confirm, copies = once(ImGuiKey_GamepadFaceLeft) || once(ImGuiKey_X);
      const bool none = once(ImGuiKey_GamepadFaceUp) || once(ImGuiKey_Y);
      if (back) {
        // The profile was made a moment ago and is still empty: it goes again, for good.
        DeleteProfile(profiles_[selected_], false);
        Load();
        selected_ = profiles_.size();  // back on the "new profile" card
        mode_ = Mode::kList;
      } else if (fresh || copies || none) {
        if (fresh || copies) {
          const int copied = CopyFigures(from, profiles_[selected_], fresh);
          char text[160];
          std::snprintf(text, sizeof(text),
                        german_ ? (fresh ? "%d Figuren neu angelegt" : "%d Figuren kopiert")
                                : (fresh ? "%d figures made new" : "%d figures copied"),
                        copied);
          Message(text);
        }
        Load();
        std::snprintf(name_, sizeof(name_), "%s", profiles_[selected_].name.c_str());
        focus_name_ = true;
        mode_ = Mode::kName;
      }
      break;
    }
    case Mode::kName: {
      // The name with an on-screen keyboard, for the pad (the owner: everything with the controller
      // first; it needed a physical keyboard): the pad moves over the keys, A types the key, X
      // deletes, Y types a space, LB switches capitals, Start keeps the name, B cancels. A keyboard
      // types straight into it (Enter keeps, Esc cancels). At most 15 letters, as the games pass it
      // on as the player's name.
      if (focus_name_) {
        focus_name_ = false;
        osk_row_ = 1;
        osk_col_ = 0;
        osk_caps_ = name_[0] == '\0';
      }
      static const std::vector<std::vector<std::string>> kRowsDe = {
          {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
          {"Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "\xC3\x9C"},
          {"A", "S", "D", "F", "G", "H", "J", "K", "L", "\xC3\x96", "\xC3\x84"},
          {"Y", "X", "C", "V", "B", "N", "M", "\xC3\x9F", "-", "."}};
      static const std::vector<std::vector<std::string>> kRowsEn = {
          {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
          {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"},
          {"A", "S", "D", "F", "G", "H", "J", "K", "L", "'"},
          {"Z", "X", "C", "V", "B", "N", "M", "-", "."}};
      const auto& rows = german_ ? kRowsDe : kRowsEn;
      const int row_count = int(rows.size()) + 1;  // the last: caps, space, delete, done
      auto row_len = [&](int r) { return r < int(rows.size()) ? int(rows[size_t(r)].size()) : 4; };
      // The name's letters (UTF-8), for the limit and for deleting the last one.
      auto letters = [](const std::string& s) {
        int n = 0;
        for (unsigned char c : s) n += (c & 0xC0) != 0x80;
        return n;
      };
      std::string name = name_;
      auto type = [&](const std::string& text) {
        if (letters(name) + letters(text) > 15) return;
        name += text;
      };
      auto erase_last = [&] {
        while (!name.empty() && (static_cast<unsigned char>(name.back()) & 0xC0) == 0x80) name.pop_back();
        if (!name.empty()) name.pop_back();
      };
      // A key in the case the capitals switch says (the umlauts too; ß has no capital here).
      auto key_text = [&](const std::string& key) {
        if (osk_caps_) return key;
        if (key == "\xC3\x9C") return std::string("\xC3\xBC");
        if (key == "\xC3\x96") return std::string("\xC3\xB6");
        if (key == "\xC3\x84") return std::string("\xC3\xA4");
        std::string lower = key;
        for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
        return lower;
      };
      bool done = false, cancel = false;
      // What a key does; letters turn the capitals off after the first one.
      auto press = [&](int r, int c) {
        if (r < int(rows.size())) {
          const std::string& key = rows[size_t(r)][size_t(c)];
          type(key_text(key));
          if (std::isalpha(static_cast<unsigned char>(key[0])) || static_cast<unsigned char>(key[0]) >= 0x80) {
            osk_caps_ = false;
          }
        } else if (c == 0) {
          osk_caps_ = !osk_caps_;
        } else if (c == 1) {
          type(" ");
        } else if (c == 2) {
          erase_last();
        } else {
          done = true;
        }
      };
      // The pad on the keys.
      const bool up = pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_GamepadDpadUp) || pressed(ImGuiKey_GamepadLStickUp);
      const bool down =
          pressed(ImGuiKey_DownArrow) || pressed(ImGuiKey_GamepadDpadDown) || pressed(ImGuiKey_GamepadLStickDown);
      auto move_row = [&](int d) {
        const float at = (float(osk_col_) + 0.5f) / float(row_len(osk_row_));
        osk_row_ = (osk_row_ + d + row_count) % row_count;
        osk_col_ = std::clamp(int(at * float(row_len(osk_row_))), 0, row_len(osk_row_) - 1);
      };
      if (up) move_row(-1);
      if (down) move_row(+1);
      if (left) osk_col_ = (osk_col_ + row_len(osk_row_) - 1) % row_len(osk_row_);
      if (right) osk_col_ = (osk_col_ + 1) % row_len(osk_row_);
      if (once(ImGuiKey_GamepadFaceDown)) press(osk_row_, osk_col_);
      if (pressed(ImGuiKey_GamepadFaceLeft)) erase_last();  // held: repeats, to clear a name quickly
      if (once(ImGuiKey_GamepadFaceUp)) type(" ");
      if (once(ImGuiKey_GamepadL1) || once(ImGuiKey_GamepadL3)) osk_caps_ = !osk_caps_;
      if (once(ImGuiKey_GamepadStart) || once(ImGuiKey_Enter) || once(ImGuiKey_KeypadEnter)) done = true;
      if (back) cancel = true;
      // The keyboard types itself (the arrows still move over the keys).
      for (const ImWchar ch : io.InputQueueCharacters) {
        if (ch < 0x20 || ch == 0x7F) continue;
        char utf8[5] = {};
        ImTextCharToUtf8(utf8, ch);
        type(utf8);
      }
      if (pressed(ImGuiKey_Backspace)) erase_last();

      // The panel, centred: the title, the name, the keys.
      const float ph = 500 * u;
      const ImVec2 p0{(width - panel_w) * 0.5f, std::max(20 * u, (height - ph) * 0.5f - 30 * u)};
      dl->AddRectFilled({0, 0}, {width, height}, Rgba(0, 0, 0, 140));
      dl->AddRectFilled(p0, {p0.x + panel_w, p0.y + ph}, kPanel, 18 * u);
      dl->AddRect(p0, {p0.x + panel_w, p0.y + ph}, kGold, 18 * u, 0, 3 * u);
      const float x = p0.x + 28 * u, w = panel_w - 56 * u;
      Text(dl, bold_, 28 * u, {x, p0.y + 22 * u}, kGold, german_ ? "Name des Profils" : "The profile's name");
      const ImVec2 f0{x, p0.y + 68 * u}, f1{x + w, p0.y + 124 * u};
      dl->AddRectFilled(f0, f1, Rgba(8, 14, 40, 235), 12 * u);
      dl->AddRect(f0, f1, Rgba(120, 140, 200), 12 * u, 0, 2 * u);
      Text(dl, bold_, 30 * u, {f0.x + 16 * u, f0.y + 10 * u}, kWhite, name);
      if (std::fmod(ImGui::GetTime(), 1.0) < 0.6) {  // the caret
        const float cx = f0.x + 18 * u + TextWidth(bold_, 30 * u, name.c_str());
        dl->AddRectFilled({cx, f0.y + 12 * u}, {cx + 3 * u, f1.y - 12 * u}, kGold);
      }
      char count[16];
      std::snprintf(count, sizeof(count), "%d / 15", letters(name));
      Text(dl, regular_, 18 * u, {f1.x - TextWidth(regular_, 18 * u, count) - 14 * u, f0.y + 17 * u}, kDim, count);
      // The keys: the letter rows, then caps, space, delete, done (wider).
      const float key_gap = 8 * u, key_h = 50 * u;
      float ky = p0.y + 146 * u;
      for (int r = 0; r < row_count; ++r) {
        const int len = row_len(r);
        const bool special = r == int(rows.size());
        const float widths[4] = {0.18f, 0.42f, 0.18f, 0.22f};
        float kx = x;
        for (int c = 0; c < len; ++c) {
          const float kw = special ? (w - key_gap * 3) * widths[c] : (w - key_gap * 10) / 11.0f;
          const ImVec2 k0{kx, ky}, k1{kx + kw, ky + key_h};
          const bool hover = io.MousePos.x >= k0.x && io.MousePos.x < k1.x && io.MousePos.y >= k0.y && io.MousePos.y < k1.y;
          if (hover && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
            osk_row_ = r;
            osk_col_ = c;
          }
          if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            osk_row_ = r;
            osk_col_ = c;
            press(r, c);
          }
          const bool on = r == osk_row_ && c == osk_col_;
          const bool lit = special && c == 0 && osk_caps_;
          dl->AddRectFilled(k0, k1, on ? kGold : lit ? Rgba(70, 90, 160) : Rgba(30, 40, 90, 240), 10 * u);
          std::string label;
          if (!special) {
            label = key_text(rows[size_t(r)][size_t(c)]);
          } else {
            const char* labels_de[4] = {"Gro\xC3\x9F", "Leerzeichen", "L\xC3\xB6schen", "Fertig"};
            const char* labels_en[4] = {"Caps", "Space", "Delete", "Done"};
            label = german_ ? labels_de[c] : labels_en[c];
          }
          const float ls = special ? 22 * u : 26 * u;
          Text(dl, bold_, ls, {k0.x + (kw - TextWidth(bold_, ls, label.c_str())) * 0.5f, k0.y + (key_h - ls) * 0.5f - 2 * u},
               on ? Rgba(60, 35, 0) : kWhite, label);
          kx += kw + key_gap;
        }
        ky += key_h + key_gap;
      }
      if (done) {
        name.erase(0, name.find_first_not_of(" \t"));
        name.erase(name.find_last_not_of(" \t") + 1);
        if (!name.empty() && selected_ < profiles_.size()) {
          Profile p = profiles_[selected_];
          p.name = name;
          SaveProfile(p);
          if (p.id == ActiveProfile().id) changed_ = true;
          Load();
        }
        mode_ = Mode::kList;
      } else if (cancel) {
        mode_ = Mode::kList;
      }
      std::snprintf(name_, sizeof(name_), "%s", name.c_str());
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 170, 70), german_ ? "Tippen" : "Type");
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "X", Rgba(50, 120, 220), german_ ? "L\xC3\xB6schen" : "Delete", "");
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Y", Rgba(220, 180, 40), german_ ? "Leerzeichen" : "Space", "");
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "LB", Rgba(80, 90, 140), german_ ? "Gro\xC3\x9F / klein" : "Caps", "");
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Start", Rgba(80, 90, 140), german_ ? "Fertig" : "Done", "Enter");
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Abbrechen" : "Cancel");
      break;
    }
    case Mode::kPicture: {
      if (pictures_.empty()) {
        mode_ = Mode::kList;
        break;
      }
      if (left) picture_ = (picture_ + pictures_.size() - 1) % pictures_.size();
      if (right) picture_ = (picture_ + 1) % pictures_.size();
      draw_panel(330 * u);
      const float x = panel.x + 28 * u, w = panel_w - 56 * u;
      Text(dl, bold_, 28 * u, {x, panel.y + 22 * u}, kGold, german_ ? "Bild des Profils" : "The profile's picture");
      // The chosen picture big, its neighbours small at the sides.
      const float cy = panel.y + 180 * u;
      const int m = int(pictures_.size());
      for (int d = -2; d <= 2; ++d) {
        // Each picture once: with few, not on both sides.
        if (d != 0 && (d < 0 ? -2 * d >= m : 2 * d > m)) continue;
        const size_t i = size_t(((int(picture_) + d) % m + m) % m);
        const float radius = d == 0 ? 90 * u : 46 * u;
        const ImVec2 c{panel.x + panel_w * 0.5f + d * 150 * u, cy};
        const fs::path file = FigurePicture(pictures_[i].first, pictures_[i].second);
        dl->AddCircleFilled(c, radius, Rgba(40, 50, 100));
        if (const ImTextureID texture = LoadTexture(file)) {
          dl->AddImageRounded(texture, {c.x - radius, c.y - radius}, {c.x + radius, c.y + radius}, {0, 0}, {1, 1},
                              d == 0 ? kWhite : Rgba(255, 255, 255, 120), radius);
        }
        if (d == 0) dl->AddCircle(c, radius, kGold, 64, 4 * u);
      }
      char count[32];
      std::snprintf(count, sizeof(count), "%zu / %zu", picture_ + 1, pictures_.size());
      Centered(dl, regular_, 18 * u, panel.x, panel_w, panel.y + 290 * u, kDim, count);
      (void)w;
      if (confirm || once(ImGuiKey_GamepadFaceLeft) || once(ImGuiKey_X)) {
        Profile p = profiles_[selected_];
        if (confirm) {
          p.has_picture = true;
          p.picture_id = pictures_[picture_].first;
          p.picture_variant = pictures_[picture_].second;
        } else {
          p.has_picture = false;
        }
        SaveProfile(p);
        if (p.id == ActiveProfile().id) changed_ = true;
        Load();
        mode_ = Mode::kList;
      } else if (back) {
        mode_ = Mode::kList;
      }
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 170, 70), german_ ? "Nehmen" : "Take it");
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "X", Rgba(50, 120, 220), german_ ? "Kein Bild" : "No picture");
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
      break;
    }
    case Mode::kDelete: {
      draw_panel(270 * u);
      const float x = panel.x + 28 * u, w = panel_w - 56 * u;
      const Profile& p = profiles_[selected_];
      Text(dl, bold_, 28 * u, {x, panel.y + 22 * u}, kGold,
           Fit(bold_, 28 * u, (german_ ? "Profil l\xC3\xB6schen: " : "Delete the profile: ") + p.name, w));
      Text(dl, regular_, 20 * u, {x, panel.y + 70 * u}, kWhite,
           german_ ? "Seine Spielst\xC3\xA4nde, Erfolge und Figuren kommen in den Papierkorb (von dort lassen "
                     "sie sich wiederherstellen)."
                   : "Its saves, achievements and figures go to the recycle bin (they can be restored from there).",
           w);
      // Two buttons, "Behalten" chosen first (as the games' own yes/no questions): A on a reflex
      // keeps the profile.
      if (once(ImGuiKey_GamepadDpadLeft) || once(ImGuiKey_GamepadDpadRight) || once(ImGuiKey_GamepadLStickLeft) ||
          once(ImGuiKey_GamepadLStickRight) || once(ImGuiKey_LeftArrow) || once(ImGuiKey_RightArrow)) {
        delete_chosen_ = !delete_chosen_;
      }
      {
        const float bw = 220 * u, bh = 52 * u, by = panel.y + 270 * u - bh - 22 * u;
        for (int k = 0; k < 2; ++k) {
          const bool is_delete = k == 1, on = is_delete == delete_chosen_;
          const ImVec2 a{x + k * (bw + 20 * u), by}, b{a.x + bw, a.y + bh};
          dl->AddRectFilled(a, b, on ? (is_delete ? Rgba(150, 50, 50) : Rgba(60, 80, 160)) : Rgba(30, 34, 60), 12 * u);
          dl->AddRect(a, b, on ? kGold : kEdge, 12 * u, 0, (on ? 3.0f : 1.5f) * u);
          const char* label = is_delete ? (german_ ? "L\xC3\xB6schen" : "Delete") : (german_ ? "Behalten" : "Keep it");
          Text(dl, bold_, 22 * u, {a.x + (bw - TextWidth(bold_, 22 * u, label)) * 0.5f, a.y + 13 * u}, kWhite, label);
        }
      }
      if ((once(ImGuiKey_GamepadFaceDown) || once(ImGuiKey_Enter)) && !delete_chosen_) {
        mode_ = Mode::kList;
      } else if (once(ImGuiKey_GamepadFaceDown) || once(ImGuiKey_Enter)) {
        const bool was_active = p.id == ActiveProfile().id;
        if (DeleteProfile(p)) {
          Message(german_ ? "Profil gel\xC3\xB6scht (im Papierkorb)" : "Profile deleted (in the recycle bin)");
          if (was_active) changed_ = true;
          selected_ = 0;
        } else {
          Message(german_ ? "Das Profil lie\xC3\x9F sich nicht l\xC3\xB6schen (l\xC3\xA4uft ein Spiel damit?)"
                          : "Could not delete the profile (is a game running with it?)");
        }
        Load();
        mode_ = Mode::kList;
      } else if (back) {
        mode_ = Mode::kList;
      }
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 170, 70), german_ ? "W\xC3\xA4hlen" : "Choose");
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(80, 90, 140), german_ ? "Behalten" : "Keep it");
      break;
    }
  }
  if (!message_.empty() && ImGui::GetTime() < message_until_) {
    Text(dl, regular_, 20 * u, {48 * u, height - 92 * u}, Rgba(110, 220, 110), message_, width - 96 * u);
  }
  ImGui::End();
}

}  // namespace coreoflight
