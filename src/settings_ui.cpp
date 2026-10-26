#include "settings_ui.h"
#include "hints.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"  // GetKeyData: how long a key is held (the slider speeds up)
#include "nxe_art.h"

namespace coreoflight {
namespace {

ImU32 Rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

const ImU32 kGold = Rgba(255, 204, 64);
const ImU32 kWhite = Rgba(255, 255, 255);
const ImU32 kDim = Rgba(180, 190, 215);
const ImU32 kPanel = Rgba(24, 30, 70, 235);
const ImU32 kEdge = Rgba(90, 100, 150);

float TextWidth(ImFont* font, float size, const char* text) {
  return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
}

void Text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const std::string& text,
          float wrap = 0.0f) {
  dl->AddText(font, size, pos, col, text.c_str(), nullptr, wrap);
}

// `text` cut at its start ("...") to fit `width`: a long folder keeps its end, where its name is.
std::string FitStart(ImFont* font, float size, std::string text, float width) {
  if (TextWidth(font, size, text.c_str()) <= width) return text;
  while (text.size() > 4 && TextWidth(font, size, ("..." + text).c_str()) > width) {
    size_t cut = 1;
    while (cut < text.size() && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) ++cut;
    text.erase(0, cut);
  }
  return "..." + text;
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

bool IsOn(const std::string& v) { return v == "true" || v == "1"; }

// An option's value and the setting's: the same text, or the same number (a game writes its
// double settings as "0.000000", the options say "0").
bool SameValue(const std::string& option, const std::string& value) {
  if (option == value) return true;
  char* end_a = nullptr;
  char* end_b = nullptr;
  const double a = std::strtod(option.c_str(), &end_a);
  const double b = std::strtod(value.c_str(), &end_b);
  return !option.empty() && !value.empty() && *end_a == '\0' && *end_b == '\0' && std::fabs(a - b) < 1e-6;
}

}  // namespace

void SettingsScreen::Init(ImFont* regular, ImFont* bold, bool german) {
  regular_ = regular;
  bold_ = bold;
  german_ = german;
}

void SettingsScreen::Open(const std::string& title, SettingsLoader* loader, std::filesystem::path background,
                          int page) {
  open_ = true;
  title_ = title;
  background_ = std::move(background);
  loader_ = loader;
  edit_.reset();
  page_ = page;  // Draw keeps it within the pages
  row_ = scroll_ = 0;
  wheel_ = 0.0f;
  changes_.clear();
  confirm_discard_ = false;
  message_.clear();
}

std::map<std::string, std::string> SettingsScreen::TakeSaved() { return std::exchange(saved_, {}); }

std::string SettingsScreen::TakeAction() { return std::exchange(action_, {}); }

void SettingsScreen::ShowMessage(const std::string& text) {
  message_ = text;
  message_until_ = ImGui::GetTime() + 6.0;
}

bool SettingsScreen::SetActionLabel(const std::string& cvar, const std::string& en, const std::string& de) {
  if (!edit_) return false;
  for (GameSettingsPage& page : edit_->pages) {
    for (GameSetting& s : page.settings) {
      if (s.cvar != cvar || s.type != "action" || s.options.empty()) continue;
      s.options[0].en = en;
      s.options[0].de = de;
    }
  }
  return true;
}

std::string SettingsScreen::ValueLabel(const GameSetting& s) const {
  for (const SettingChoice& o : s.options) {
    if (SameValue(o.value, s.value)) return german_ ? o.de : o.en;
  }
  if (s.options.empty() && s.type == "bool") return IsOn(s.value) ? (german_ ? "An" : "On") : (german_ ? "Aus" : "Off");
  if (s.slider_max > s.slider_min) return s.value + s.slider_unit;
  if (s.value.empty()) return german_ ? "Eigene Einstellung" : "Custom";
  return s.value;
}

bool SettingsScreen::Enabled(const GameSetting& s, const GameSetting** needs) const {
  if (s.enabled_by.empty() || !edit_) return true;
  for (const GameSettingsPage& page : edit_->pages) {
    for (const GameSetting& other : page.settings) {
      if (other.cvar != s.enabled_by) continue;
      if (needs) *needs = &other;
      return IsOn(other.value);
    }
  }
  return true;  // the game doesn't show that setting: nothing to depend on
}

void SettingsScreen::Step(int step) {
  if (!edit_ || page_ >= int(edit_->pages.size())) return;
  auto& settings = edit_->pages[page_].settings;
  if (row_ >= int(settings.size())) return;
  GameSetting& s = settings[row_];
  if (!Enabled(s)) return;  // greyed: its enabled_by setting is off
  // A button row (type "action", the launcher's "Problem melden"): A runs it, left / right nothing.
  if (s.type == "action") {
    if (step > 0) action_ = s.cvar;
    return;
  }
  if (s.slider_max > s.slider_min) {
    SetSlider(s, std::atoi(s.value.c_str()) + step);
    return;
  }
  std::string next;
  if (!s.options.empty()) {
    int at = -1;
    for (int i = 0; i < int(s.options.size()); ++i) {
      if (SameValue(s.options[i].value, s.value)) at = i;
    }
    const int n = int(s.options.size());
    at = at < 0 ? (step > 0 ? 0 : n - 1) : (at + step + n) % n;
    next = s.options[at].value;
  } else if (s.type == "bool") {
    next = IsOn(s.value) ? "false" : "true";
  } else {
    return;  // a free value: the game's own menu (F10) has it
  }
  s.value = next;
  auto set = [&](const std::string& cvar, const std::string& value, const std::string& category,
                 const std::string& type) {
    changes_[cvar] = {category, type, value};
    // The same setting elsewhere (a preset's half on its own row) shows the new value too.
    for (auto& page : edit_->pages) {
      for (auto& other : page.settings) {
        if (other.cvar == cvar) other.value = value;
      }
    }
  };
  if (s.cvar.empty()) {
    const auto it = s.presets.find(next);
    if (it != s.presets.end()) {
      for (const auto& a : it->second) set(a.cvar, a.value, a.category, a.type);
    }
  } else {
    set(s.cvar, next, s.category, s.type);
  }
  // A preset shows which of its options the settings now match, or none.
  for (auto& page : edit_->pages) {
    for (auto& preset : page.settings) {
      if (!preset.cvar.empty() || preset.presets.empty()) continue;
      std::string match;
      for (const auto& [option, assignments] : preset.presets) {
        bool all = !assignments.empty();
        for (const auto& a : assignments) {
          std::string now;
          for (const auto& p : edit_->pages) {
            for (const auto& o : p.settings) {
              if (o.cvar == a.cvar) now = o.value;
            }
          }
          if (const auto c = changes_.find(a.cvar); c != changes_.end()) now = c->second.value;
          if (now != a.value) all = false;
        }
        if (all) match = option;
      }
      preset.value = match;
    }
  }
  confirm_discard_ = false;
}

void SettingsScreen::SetSlider(GameSetting& s, int value) {
  value = std::clamp(value, s.slider_min, s.slider_max);
  if (std::to_string(value) == s.value) return;
  s.value = std::to_string(value);
  changes_[s.cvar] = {s.category, s.type, s.value};
  confirm_discard_ = false;
}

std::string SettingsScreen::EditedValue(const std::string& cvar) const {
  if (const auto c = changes_.find(cvar); c != changes_.end()) return c->second.value;
  if (!edit_) return {};
  for (const GameSettingsPage& page : edit_->pages) {
    for (const GameSetting& s : page.settings) {
      if (s.cvar == cvar) return s.value;
    }
  }
  return {};
}

void SettingsScreen::Save() {
  if (!edit_) return;
  bool ok = true;
  for (const auto& [cvar, c] : changes_) {
    ok = WriteGameSetting(edit_->config_file, c.category, cvar, c.type, c.value) && ok;
    saved_[cvar] = c.value;
  }
  const size_t n = changes_.size();
  changes_.clear();
  confirm_discard_ = false;
  char text[160];
  if (!ok) {
    std::snprintf(text, sizeof(text), "%s", german_ ? "Konnte die Einstellungsdatei nicht schreiben"
                                                    : "Could not write the settings file");
  } else if (n) {
    std::snprintf(text, sizeof(text), german_ ? "%zu Einstellung(en) gespeichert" : "%zu setting(s) saved", n);
  } else {
    std::snprintf(text, sizeof(text), "%s", german_ ? "Nichts ge\xC3\xA4ndert" : "Nothing changed");
  }
  message_ = text;
  message_until_ = ImGui::GetTime() + 3.0;
}

void SettingsScreen::Draw(float width, float height) {
  if (!edit_ && loader_ && loader_->state() == 1) {
    if (auto loaded = loader_->settings()) edit_ = std::make_shared<GameSettings>(*loaded);
  }
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("settings", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float u = std::min(width / 1280.0f, height / 720.0f);
  DrawBackdrop(dl, width, height, background_);
  Text(dl, bold_, 22 * u, {48 * u, 30 * u}, kDim, german_ ? "EINSTELLUNGEN" : "SETTINGS");
  Text(dl, bold_, 44 * u, {46 * u, 52 * u}, kGold, title_);

  // While the "save the changes?" question is up (confirm_discard_), only it takes input.
  const bool asking = confirm_discard_;
  auto pressed = [asking](ImGuiKey k) { return !asking && ImGui::IsKeyPressed(k, true); };
  const bool back = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
  const float left = 48 * u, top = 120 * u, bottom = height - 80 * u;
  if (!edit_) {
    const int state = loader_ ? loader_->state() : -1;
    const char* text = state == 0 ? (german_ ? "Die Einstellungen des Spiels werden gelesen ..."
                                             : "Reading the game's settings ...")
                                  : (german_ ? "Das Spiel hat seine Einstellungen nicht genannt (\xC3\xA4ltere Version? "
                                               "Neu bauen hilft)."
                                             : "The game didn't tell its settings (an older build? Rebuilding helps).");
    Text(dl, regular_, 24 * u, {left, top + 20 * u}, kDim, text, width - 2 * left);
    Hint(dl, bold_, 22 * u, {left, height - 52 * u}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
    if (back) open_ = false;
    ImGui::End();
    return;
  }

  auto& pages = edit_->pages;
  page_ = std::clamp(page_, 0, int(pages.size()) - 1);
  // Page tabs: LB / RB, Q / E, or a click.
  if (pressed(ImGuiKey_GamepadL1) || pressed(ImGuiKey_Q) || pressed(ImGuiKey_PageUp)) {
    page_ = (page_ + int(pages.size()) - 1) % int(pages.size());
    row_ = 0;
  }
  if (pressed(ImGuiKey_GamepadR1) || pressed(ImGuiKey_E) || pressed(ImGuiKey_PageDown)) {
    page_ = (page_ + 1) % int(pages.size());
    row_ = 0;
  }
  float tx = left;
  const float tab_size = 22 * u;
  for (int i = 0; i < int(pages.size()); ++i) {
    const std::string& name = german_ ? pages[i].de : pages[i].en;
    const float w = TextWidth(bold_, tab_size, name.c_str()) + 36 * u;
    const ImVec2 a{tx, top}, b{tx + w, top + 44 * u};
    const bool hovered = !asking && io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y &&
                         io.MousePos.y < b.y;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      page_ = i;
      row_ = 0;
    }
    dl->AddRectFilled(a, b, i == page_ ? kGold : Rgba(40, 50, 100, 230), 22 * u);
    Text(dl, bold_, tab_size, {tx + 18 * u, top + 9 * u}, i == page_ ? Rgba(60, 35, 0) : kWhite, name);
    tx += w + 12 * u;
  }

  auto& settings = pages[page_].settings;
  const int n = int(settings.size());
  // The view follows the chosen row when the pad or the keys move it; the mouse wheel only scrolls
  // (the owner, #23: with the wheel every row in between got chosen).
  bool follow = false;
  if (pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_GamepadDpadUp) || pressed(ImGuiKey_GamepadLStickUp)) {
    row_ = (row_ + n - 1) % n;
    follow = true;
  }
  if (pressed(ImGuiKey_DownArrow) || pressed(ImGuiKey_GamepadDpadDown) || pressed(ImGuiKey_GamepadLStickDown)) {
    row_ = (row_ + 1) % n;
    follow = true;
  }
  row_ = std::clamp(row_, 0, n - 1);
  // A slider moves faster the longer left / right is held (1, then 2, then 5 per step; #24).
  const bool on_slider = settings[row_].slider_max > settings[row_].slider_min;
  auto held_step = [&](std::initializer_list<ImGuiKey> keys) {
    float held = 0.0f;
    for (ImGuiKey k : keys) held = std::max(held, ImGui::GetKeyData(k)->DownDuration);
    return held > 1.6f ? 5 : held > 0.8f ? 2 : 1;
  };
  if (pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_GamepadDpadLeft) || pressed(ImGuiKey_GamepadLStickLeft)) {
    Step(on_slider ? -held_step({ImGuiKey_LeftArrow, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft}) : -1);
  }
  if (pressed(ImGuiKey_RightArrow) || pressed(ImGuiKey_GamepadDpadRight) || pressed(ImGuiKey_GamepadLStickRight)) {
    Step(on_slider ? held_step({ImGuiKey_RightArrow, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight}) : 1);
  } else if (!asking && !on_slider &&
             (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false))) {
    Step(1);
  }
  // Y as the hint says; S as before.
  if (!asking && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false) || ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
                  ImGui::IsKeyPressed(ImGuiKey_S, false))) {
    Save();
  }

  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging_ = -1;
  // The rows on the left, the chosen one's explanation on the right.
  const float list_top = top + 66 * u;
  const float list_w = std::min(width * 0.58f, 760 * u);
  const float row_h = 50 * u;
  const int visible = std::max(1, int((bottom - list_top - 20 * u) / row_h));
  // The mouse wheel scrolls the view a row per notch and leaves the choice where it is.
  if (!asking) {
    wheel_ += io.MouseWheel;
    const int steps = int(wheel_);
    wheel_ -= float(steps);
    scroll_ -= steps;
  }
  if (follow) {
    if (row_ < scroll_) scroll_ = row_;
    if (row_ >= scroll_ + visible) scroll_ = row_ - visible + 1;
  }
  scroll_ = std::clamp(scroll_, 0, std::max(0, n - visible));
  const int first = scroll_;
  for (int i = first; i < std::min(n, first + visible); ++i) {
    const GameSetting& s = settings[i];
    const float y = list_top + (i - first) * row_h;
    const ImVec2 a{left, y}, b{left + list_w, y + row_h - 8 * u};
    const bool hovered = !asking && io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y &&
                         io.MousePos.y < b.y;
    if (hovered && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) row_ = i;
    const bool slider = s.slider_max > s.slider_min;
    // A slider's bar on the right of its row: a click or a drag on it sets the value.
    const float bar_w = 220 * u, bar_x1 = b.x - 40 * u, bar_x0 = bar_x1 - bar_w;
    if (slider && Enabled(s) && !asking) {
      const bool on_bar = io.MousePos.x >= bar_x0 - 10 * u && io.MousePos.x <= bar_x1 + 10 * u && io.MousePos.y >= a.y &&
                          io.MousePos.y < b.y;
      if (on_bar && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) dragging_ = i;
      if (dragging_ == i && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        row_ = i;
        const float f = std::clamp((io.MousePos.x - bar_x0) / bar_w, 0.0f, 1.0f);
        SetSlider(settings[i], s.slider_min + int(std::lround(f * float(s.slider_max - s.slider_min))));
      }
    }
    if (hovered && !slider && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      row_ = i;
      Step(1);
    }
    if (hovered && !slider && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      row_ = i;
      Step(-1);
    }
    const bool chosen = i == row_;
    const bool enabled = Enabled(s);
    dl->AddRectFilled(a, b, chosen ? Rgba(60, 70, 140, 240) : kPanel, 16 * u);
    if (chosen) dl->AddRect(a, b, enabled ? kGold : kEdge, 16 * u, 0, 3 * u);
    const bool changed = !s.cvar.empty() && changes_.count(s.cvar);
    const std::string& label = german_ ? s.de : s.en;
    const ImU32 greyed = Rgba(110, 118, 150);
    Text(dl, regular_, 22 * u, {a.x + 18 * u, y + 9 * u}, !enabled ? greyed : changed ? kGold : kWhite, label);
    const float vs = 21 * u;
    if (slider) {
      const float f = float(std::atoi(s.value.c_str()) - s.slider_min) / float(s.slider_max - s.slider_min);
      const float cy = y + row_h * 0.5f - 4 * u;
      dl->AddRectFilled({bar_x0, cy - 4 * u}, {bar_x1, cy + 4 * u}, Rgba(30, 38, 80), 4 * u);
      dl->AddRectFilled({bar_x0, cy - 4 * u}, {bar_x0 + bar_w * f, cy + 4 * u}, enabled ? kGold : greyed, 4 * u);
      dl->AddCircleFilled({bar_x0 + bar_w * f, cy}, (chosen ? 10 : 8) * u, enabled ? kWhite : greyed);
      const std::string value = ValueLabel(s);
      Text(dl, bold_, vs, {bar_x0 - 18 * u - TextWidth(bold_, vs, value.c_str()), y + 10 * u}, enabled ? kGold : greyed,
           value);
      continue;
    }
    // The value keeps clear of the label (a folder's path is long).
    const float room = list_w - TextWidth(regular_, 22 * u, label.c_str()) - 120 * u;
    const std::string value = FitStart(bold_, vs, ValueLabel(s), std::max(room, 80 * u));
    const float vw = TextWidth(bold_, vs, value.c_str());
    const float vx = b.x - 40 * u - vw;
    Text(dl, bold_, vs, {vx, y + 10 * u}, enabled ? kGold : greyed, value);
    if (chosen && enabled && s.type != "action" && (!s.options.empty() || s.type == "bool")) {
      Text(dl, bold_, vs, {vx - 28 * u, y + 10 * u}, kDim, "<");
      Text(dl, bold_, vs, {b.x - 28 * u, y + 10 * u}, kDim, ">");
    }
  }
  // The explanation.
  const float px = left + list_w + 24 * u, pw = width - px - left;
  if (pw > 160 * u && row_ < n) {
    const GameSetting& s = settings[row_];
    dl->AddRectFilled({px, list_top}, {px + pw, bottom - 10 * u}, kPanel, 18 * u);
    dl->AddRect({px, list_top}, {px + pw, bottom - 10 * u}, kEdge, 18 * u, 0, 2 * u);
    Text(dl, bold_, 24 * u, {px + 20 * u, list_top + 16 * u}, kWhite, german_ ? s.de : s.en, pw - 40 * u);
    std::string help = german_ ? s.help_de : s.help_en;
    if (const GameSetting* needs = nullptr; !Enabled(s, &needs) && needs) {
      help = (german_ ? "Nur mit \xE2\x80\x9E" + needs->de + "\xE2\x80\x9C an. " : "Only with \"" + needs->en + "\" on. ") + help;
    }
    Text(dl, regular_, 20 * u, {px + 20 * u, list_top + 58 * u}, kDim, help, pw - 40 * u);
  }

  // Hints, a message, and B: back. With unsaved changes B asks: A saves and closes, X throws the
  // changes away and closes, B goes on editing (a double B used to lose them).
  float hx = left;
  const float hy = height - 52 * u;
  if (!asking) {
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "LB/RB", Rgba(80, 90, 140), german_ ? "Seite" : "Page");
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 170, 70), german_ ? "\xC3\x84ndern" : "Change");
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Y", Rgba(220, 180, 40), german_ ? "Speichern" : "Save");
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
    if (back) {
      if (!changes_.empty()) {
        confirm_discard_ = true;
      } else {
        open_ = false;
      }
    }
  } else {
    const float pw = std::min(620 * u, width - 96 * u), ph = 200 * u;
    const ImVec2 a{(width - pw) * 0.5f, (height - ph) * 0.5f}, b{a.x + pw, a.y + ph};
    dl->AddRectFilled({0, 0}, {width, height}, Rgba(0, 0, 0, 140));
    dl->AddRectFilled(a, b, Rgba(20, 26, 64, 250), 18 * u);
    dl->AddRect(a, b, kGold, 18 * u, 0, 3 * u);
    Text(dl, bold_, 28 * u, {a.x + 28 * u, a.y + 22 * u}, kGold,
         german_ ? "\xC3\x84nderungen speichern?" : "Save the changes?");
    char text[160];
    std::snprintf(text, sizeof(text),
                  german_ ? "%zu Einstellung(en) ge\xC3\xA4ndert, noch nicht gespeichert." : "%zu setting(s) changed, not saved yet.",
                  changes_.size());
    Text(dl, regular_, 20 * u, {a.x + 28 * u, a.y + 70 * u}, kWhite, text, pw - 56 * u);
    float bx = a.x + 28 * u;
    const float by = b.y - 52 * u;
    bx = Hint(dl, bold_, 22 * u, {bx, by}, "A", Rgba(70, 170, 70), german_ ? "Speichern" : "Save");
    bx = Hint(dl, bold_, 22 * u, {bx, by}, "X", Rgba(50, 120, 220), german_ ? "Verwerfen" : "Throw away");
    bx = Hint(dl, bold_, 22 * u, {bx, by}, "B", Rgba(200, 60, 60), german_ ? "Weiter bearbeiten" : "Keep editing");
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
      Save();
      open_ = false;
    } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false) || ImGui::IsKeyPressed(ImGuiKey_X, false)) {
      changes_.clear();
      confirm_discard_ = false;
      open_ = false;
    } else if (back) {
      confirm_discard_ = false;
    }
  }
  if (!message_.empty() && ImGui::GetTime() < message_until_) {
    Text(dl, regular_, 20 * u, {hx + 10 * u, hy}, Rgba(110, 220, 110), message_);
  }
  const std::u8string file = edit_->config_file.u8string();
  Text(dl, regular_, 16 * u, {left, height - 82 * u}, Rgba(120, 130, 160),
       std::string(file.begin(), file.end()));
  ImGui::End();
}

}  // namespace coreoflight
