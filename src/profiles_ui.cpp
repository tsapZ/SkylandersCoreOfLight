#include "profiles_ui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "achievements.h"
#include "imgui.h"
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
  for (const Profile& p : profiles_) {
    uint32_t score = 0;
    for (const Game& game : AllGames()) score += LoadAchievements(game, p).gamerscore;
    gamerscores_.push_back(score);
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
  char figures[64];
  std::snprintf(figures, sizeof(figures), german_ ? "%d Figuren  \xC2\xB7  %u G" : "%d figures  \xC2\xB7  %u G",
                figure_counts_[index], gamerscores_[index]);
  Text(dl, regular_, 20 * s, {x + 22 * s, y + 280 * s}, kDim, figures);
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
  const float u = std::min(width / 1280.0f, height / 720.0f);
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
        mode_ = Mode::kDelete;
      } else if (back) {
        open_ = false;
      }
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 170, 70),
                on_profile ? (german_ ? "Mit diesem Profil spielen" : "Play with this profile")
                           : (german_ ? "Anlegen" : "Make it"));
      if (on_profile) {
        hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Y", Rgba(220, 180, 40), german_ ? "Bild" : "Picture");
        hx = Hint(dl, bold_, 22 * u, {hx, hy}, "X", Rgba(50, 120, 220), german_ ? "Name" : "Name");
        if (selected_ > 0) {
          hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Back", Rgba(80, 90, 140), german_ ? "L\xC3\xB6schen" : "Delete");
        }
      }
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
      break;
    }
    case Mode::kNewFigures: {
      const Profile& from = ActiveProfile();
      draw_panel(250 * u);
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
      lx = Hint(dl, bold_, 22 * u, {x, y}, "B", Rgba(200, 60, 60), german_ ? "Ohne Figuren" : "No figures");
      (void)lx;
      const bool fresh = confirm, copies = once(ImGuiKey_GamepadFaceLeft) || once(ImGuiKey_X);
      if (fresh || copies || back) {
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
      draw_panel(190 * u);
      const float x = panel.x + 28 * u, w = panel_w - 56 * u;
      Text(dl, bold_, 28 * u, {x, panel.y + 22 * u}, kGold, german_ ? "Name des Profils" : "The profile's name");
      ImGui::SetNextWindowPos({x, panel.y + 70 * u});
      ImGui::SetNextWindowSize({w, 60 * u});
      ImGui::SetNextWindowFocus();
      ImGui::Begin("profile_name", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoBackground);
      ImGui::PushFont(bold_);
      ImGui::SetNextItemWidth(w - 16 * u);
      if (focus_name_) {
        ImGui::SetKeyboardFocusHere();
        focus_name_ = false;
      }
      const bool entered = ImGui::InputText("##name", name_, sizeof(name_), ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::PopFont();
      ImGui::End();
      Text(dl, regular_, 18 * u, {x, panel.y + 140 * u}, kDim,
           german_ ? "Mit der Tastatur eingeben, Enter beh\xC3\xA4lt ihn." : "Type it, Enter keeps it.");
      if (entered || once(ImGuiKey_GamepadFaceDown)) {
        std::string name = name_;
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
      } else if (back) {
        mode_ = Mode::kList;
      }
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Enter", Rgba(70, 170, 70), german_ ? "Behalten" : "Keep");
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Esc", Rgba(200, 60, 60), german_ ? "Abbrechen" : "Cancel");
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
      draw_panel(200 * u);
      const float x = panel.x + 28 * u, w = panel_w - 56 * u;
      const Profile& p = profiles_[selected_];
      Text(dl, bold_, 28 * u, {x, panel.y + 22 * u}, kGold,
           Fit(bold_, 28 * u, (german_ ? "Profil l\xC3\xB6schen: " : "Delete the profile: ") + p.name, w));
      Text(dl, regular_, 20 * u, {x, panel.y + 70 * u}, kWhite,
           german_ ? "Seine Spielst\xC3\xA4nde, Erfolge und Figuren kommen in den Papierkorb (von dort lassen "
                     "sie sich wiederherstellen)."
                   : "Its saves, achievements and figures go to the recycle bin (they can be restored from there).",
           w);
      if (once(ImGuiKey_GamepadFaceDown) || once(ImGuiKey_Enter)) {
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
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(200, 60, 60), german_ ? "L\xC3\xB6schen" : "Delete");
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
