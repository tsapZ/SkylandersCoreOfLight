#include "ds3_ui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <thread>

#include "game_settings.h"
#include "games.h"
#include "hints.h"
#include "hub_settings.h"
#include "imgui.h"
#include "nxe_art.h"
#include "platform.h"
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
const ImU32 kRed = Rgba(240, 110, 110);

float TextWidth(ImFont* font, float size, const std::string& text) {
  return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
}

void Text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const std::string& text, float wrap = 0.0f) {
  dl->AddText(font, size, pos, col, text.c_str(), nullptr, wrap);
}

bool Continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

// Shortens `text` at its end with "..." to fit `width`, a whole UTF-8 character at a time.
std::string Fit(ImFont* font, float size, std::string text, float width) {
  if (TextWidth(font, size, text) <= width) return text;
  while (!text.empty() && TextWidth(font, size, text + "...") > width) {
    while (!text.empty() && Continuation(text.back())) text.pop_back();
    if (!text.empty()) text.pop_back();
  }
  return text + "...";
}

// The same from the front, for a path: its end (the folder's own name) stays.
std::string FitStart(ImFont* font, float size, std::string text, float width) {
  if (TextWidth(font, size, text) <= width) return text;
  while (!text.empty() && TextWidth(font, size, "..." + text) > width) {
    text.erase(0, 1);
    while (!text.empty() && Continuation(text.front())) text.erase(0, 1);
  }
  return "..." + text;
}

// A hint: the controller button or the keyboard key (hints.h), and what it does; a click on it
// presses the button.
float Hint(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const char* button, ImU32 col, const char* label,
           const char* key = nullptr) {
  const float x = pos.x + HintGlyph(dl, font, size, pos, button, col, key) + size * 0.4f;
  Text(dl, font, size, {x, pos.y}, kWhite, label);
  HintClickArea({pos.x - size * 0.2f, pos.y - size * 0.25f},
                {x + TextWidth(font, size, label) + size * 0.3f, pos.y + size * 1.3f}, button);
  return x + TextWidth(font, size, label) + size * 1.2f;
}

bool Over(ImVec2 a, ImVec2 b) {
  const ImVec2 m = ImGui::GetIO().MousePos;
  return m.x >= a.x && m.x < b.x && m.y >= a.y && m.y < b.y;
}

}  // namespace

void Ds3Screen::Init(ImFont* regular, ImFont* bold, bool german) {
  regular_ = regular;
  bold_ = bold;
  german_ = german;
  browser_.Init(regular, bold, german, true);
  browser_.SetFor3ds(true);
}

void Ds3Screen::Open() {
  open_ = true;
  const std::string chosen = LoadHubSettings().ds3_folder;
  folder_ = chosen.empty() ? UserDownloadsDir() : FolderFromStored(chosen);
  RefreshInstalled();
  StartScan();
  Ds3LookForUpdates();
}

void Ds3Screen::RefreshInstalled() {
  installed_.clear();
  set_up_.clear();
  for (const Ds3Game& g : Ds3Games()) {
    installed_.push_back(InstalledDs3Version(g));
    set_up_.push_back(!installed_.back().empty() && Ds3SetUp(g));
  }
}

Ds3Screen::Need Ds3Screen::NeedOf(const Ds3Dump& d) const {
  if (installed_.size() <= d.game || installed_[d.game].empty()) return d.supported() ? Need::kPackage : Need::kOtherRegion;
  if (set_up_[d.game]) return Need::kNothing;
  return d.supported() ? Need::kSetup : Need::kOtherRegion;
}

void Ds3Screen::StartScan() {
  // In the background: a big Downloads folder, a slow disk or a network share mustn't hold the
  // window still. A search still running for an older folder just ends unseen.
  auto scan = std::make_shared<Scan>();
  scan->folder = folder_;
  scan_ = scan;
  std::thread([scan] {
    scan->dumps = FindDs3Dumps(scan->folder);
    scan->done = true;
  }).detach();
}

void Ds3Screen::TakeFolder(const std::filesystem::path& folder) {
  folder_ = folder;
  WriteGameSetting(HubSettingsFile(), "Hub", "ds3_folder", "string", StoredFolder(folder_));
  report_count_ = true;
  StartScan();
}

void Ds3Screen::Say(const std::string& text, bool bad) {
  message_ = text;
  message_bad_ = bad;
  message_until_ = ImGui::GetTime() + 8.0;
}

void* Ds3Screen::TakeStartedProcess() {
  void* p = started_;
  started_ = nullptr;
  return p;
}

void Ds3Screen::StartSelected() {
  if (selected_ < 0 || size_t(selected_) >= dumps_.size() || busy_ || started_) return;
  if (install_.running()) {
    Say(german_ ? "Einen Moment, erst wird das Spiel fertig eingerichtet." : "One moment, the game is being set up first.", true);
    return;
  }
  const Ds3Dump& d = dumps_[size_t(selected_)];
  switch (NeedOf(d)) {
    case Need::kOtherRegion:
      Say(german_ ? "Bisher l\xC3\xA4uft nur die europ\xC3\xA4ische Fassung dieses Spiels."
                  : "Only the European edition of this game runs so far.",
          true);
      return;
    case Need::kPackage:
    case Need::kSetup:
      // Everything it needs, then the start (Draw, once the install is done).
      install_dump_ = d;
      start_after_ = true;
      install_.Start(d.game, &d, NeedOf(d) == Need::kPackage, german_);
      return;
    case Need::kNothing:
      break;
  }
  std::string error;
  if (void* p = StartDs3Game(d, german_, error)) {
    started_ = p;
    Say(german_ ? "Das Spiel startet ..." : "The game is starting ...", false);
  } else {
    Say(error, true);
  }
}

void Ds3Screen::UpdateSelected() {
  if (selected_ < 0 || size_t(selected_) >= dumps_.size() || busy_ || started_ || install_.running()) return;
  const Ds3Dump& d = dumps_[size_t(selected_)];
  if (NeedOf(d) == Need::kOtherRegion) {
    Say(german_ ? "Bisher l\xC3\xA4uft nur die europ\xC3\xA4ische Fassung dieses Spiels."
                : "Only the European edition of this game runs so far.",
        true);
    return;
  }
  install_dump_ = d;
  start_after_ = false;
  install_.Start(d.game, &d, true, german_);
}

void Ds3Screen::Draw(float width, float height) {
  // X: the launcher's folder browser on top (pad and keyboard); what it chose becomes the folder.
  if (browser_.is_open()) {
    browser_.Draw(width, height);
    if (const std::filesystem::path chosen = browser_.TakeChosen(); !chosen.empty()) TakeFolder(chosen);
    return;
  }
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  // "3ds", as LauncherUi::Draw names the screen: its slide-in finds the window by that name.
  ImGui::Begin("3ds", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImGuiIO& io = ImGui::GetIO();
  const float u = UiScale(width, height);
  DrawBackdrop(dl, width, height, {});
  Text(dl, bold_, 22 * u, {48 * u, 34 * u}, kDim, "SKYLANDERS");
  Text(dl, bold_, 56 * u, {46 * u, 56 * u}, kGold, "3DS");

  // The search, when it's done (for the folder still chosen).
  if (scan_ && scan_->done.load()) {
    if (scan_->folder == folder_) {
      dumps_ = std::move(scan_->dumps);
      selected_ = std::clamp(selected_, 0, std::max(0, int(dumps_.size()) - 1));
      if (report_count_) {
        char line[96];
        std::snprintf(line, sizeof(line), german_ ? "%d Spiel(e) gefunden." : "%d game(s) found.", int(dumps_.size()));
        Say(line, dumps_.empty());
      }
    }
    scan_.reset();
    report_count_ = false;
  }
  const bool searching = scan_ != nullptr;
  // The install, when it's done: its message, what is there now, and the start it was for.
  {
    std::string text;
    bool ok = false;
    if (install_.TakeResult(text, ok)) {
      RefreshInstalled();
      const bool start = ok && start_after_ && !busy_ && !started_;
      start_after_ = false;
      if (start) {
        std::string error;
        if (void* p = StartDs3Game(install_dump_, german_, error)) {
          started_ = p;
          Say(german_ ? "Das Spiel startet ..." : "The game is starting ...", false);
        } else {
          Say(error, true);
        }
      } else {
        Say(text, !ok);
      }
    }
  }
  // The chosen card's game at the top right: its package version, or that it's missing; a newer one on GitHub.
  const int n_dumps = int(dumps_.size());
  const size_t game = n_dumps > 0 ? dumps_[size_t(std::clamp(selected_, 0, n_dumps - 1))].game : Ds3Games().size();
  const std::string installed = game < installed_.size() ? installed_[game] : std::string();
  const std::string newest = Ds3NewestVersion();
  const bool newer = !installed.empty() && installed != "?" && !newest.empty() && VersionLess(installed, newest);
  if (game < Ds3Games().size()) {
    const std::string line = installed.empty() ? (german_ ? "Noch nicht installiert" : "Not installed yet")
                                               : (german_ ? "Version " : "Version ") + installed;
    Text(dl, bold_, 20 * u, {width - 48 * u - TextWidth(bold_, 20 * u, line), 44 * u}, installed.empty() ? kGold : kDim, line);
    if (newer) {
      const std::string note = german_ ? newest + " ist da (Y)" : newest + " is out (Y)";
      Text(dl, regular_, 18 * u, {width - 48 * u - TextWidth(regular_, 18 * u, note), 70 * u}, kGreen, note);
    }
  }
  // Where the dumps are looked for (cut at the front: the folder's own name stays).
  const float left = 48 * u;
  {
    const std::string label = german_ ? "Ordner: " : "Folder: ";
    Text(dl, regular_, 19 * u, {left, 132 * u}, kDim,
         label + FitStart(regular_, 19 * u, PathUtf8(folder_), width - 2 * left - TextWidth(regular_, 19 * u, label)));
  }

  auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
  auto once = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
  const int n = int(dumps_.size());
  if (once(ImGuiKey_Escape) || once(ImGuiKey_GamepadFaceRight)) open_ = false;
  if (n > 0 && (pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_GamepadDpadLeft) || pressed(ImGuiKey_GamepadLStickLeft))) {
    selected_ = std::max(0, selected_ - 1);
  }
  if (n > 0 && (pressed(ImGuiKey_RightArrow) || pressed(ImGuiKey_GamepadDpadRight) || pressed(ImGuiKey_GamepadLStickRight))) {
    selected_ = std::min(n - 1, selected_ + 1);
  }
  if (n > 0 && (once(ImGuiKey_Enter) || once(ImGuiKey_GamepadFaceDown))) StartSelected();
  if (n > 0 && (once(ImGuiKey_Y) || once(ImGuiKey_GamepadFaceUp))) UpdateSelected();
  if (once(ImGuiKey_X) || once(ImGuiKey_GamepadFaceLeft)) browser_.Open(folder_);

  // The cards: one per dump, its own icon, the game, the region, whether it can run.
  const float top = 178 * u, bar_top = height - 96 * u;
  if (n == 0) {
    const float bw = std::min(820 * u, width - 2 * left);
    const ImVec2 a{(width - bw) * 0.5f, top + 40 * u}, b{a.x + bw, a.y + 150 * u};
    dl->AddRectFilled(a, b, kPanel, 18 * u);
    dl->AddRect(a, b, kEdge, 18 * u, 0, 1.5f * u);
    if (searching) {
      Text(dl, bold_, 24 * u, {a.x + 28 * u, a.y + 22 * u}, kWhite, german_ ? "Suche die 3DS-Spiele ..." : "Looking for the 3DS games ...");
    } else {
      Text(dl, bold_, 24 * u, {a.x + 28 * u, a.y + 22 * u}, kWhite,
           german_ ? "Hier sind noch keine 3DS-Spiele." : "No 3DS games here yet.");
      Text(dl, regular_, 19 * u, {a.x + 28 * u, a.y + 60 * u}, kDim,
           german_ ? "Lege Dumps deiner eigenen Module (.cci oder .3ds, die europ\xC3\xA4ische Fassung) in diesen Ordner, "
                     "oder w\xC3\xA4hle mit X einen anderen. Gesucht wird auch zwei Ordner tief."
                   : "Put dumps of your own cartridges (.cci or .3ds, the European edition) in this folder, or pick "
                     "another one with X. Folders two levels down count too.",
           bw - 56 * u);
    }
  } else {
    const float gap = 22 * u;
    const float card_w = std::min(230 * u, (width - 2 * left - gap * (n - 1)) / n);
    const float card_h = std::min(330 * u, bar_top - top - 40 * u);
    const float row_w = card_w * n + gap * (n - 1);
    float x = (width - row_w) * 0.5f;
    const float y = top + 20 * u;
    for (int i = 0; i < n; ++i, x += card_w + gap) {
      const Ds3Dump& d = dumps_[size_t(i)];
      const Ds3Game& g = Ds3Games()[d.game];
      const ImVec2 a{x, y}, b{x + card_w, y + card_h};
      if (Over(a, b) && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) selected_ = i;
      if (Over(a, b) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        selected_ = i;
        StartSelected();
      }
      const bool on = i == selected_;
      const float lift = on ? 10 * u : 0.0f;
      const ImVec2 la{a.x, a.y - lift}, lb{b.x, b.y - lift};
      dl->AddRectFilled(la, lb, kPanel, 18 * u);
      dl->AddRect(la, lb, on ? kGold : kEdge, 18 * u, 0, (on ? 3.0f : 1.5f) * u);
      // The dump's own icon (48x48), drawn large.
      const float icon = std::min(card_w - 40 * u, 150 * u);
      const ImVec2 ia{la.x + (card_w - icon) * 0.5f, la.y + 22 * u}, ib{ia.x + icon, ia.y + icon};
      dl->AddRectFilled(ia, ib, Rgba(12, 14, 30), 14 * u);
      char key[32];
      std::snprintf(key, sizeof(key), "ds3:%zu:%c", d.game, d.region ? d.region : '?');
      if (const ImTextureID t = LoadTextureRgba(key, 48, 48, d.icon)) {
        dl->AddImageRounded(t, ia, ib, {0, 0}, {1, 1}, kWhite, 14 * u);
      } else {
        Text(dl, bold_, 30 * u, {ia.x + (icon - TextWidth(bold_, 30 * u, "3DS")) * 0.5f, ia.y + icon * 0.5f - 18 * u},
             Rgba(80, 90, 140), "3DS");
      }
      dl->AddRect(ia, ib, Rgba(255, 255, 255, 40), 14 * u, 0, 1.0f * u);
      // Its name, region and year.
      const std::string name = Fit(bold_, 22 * u, german_ ? g.name_de : g.name_en, card_w - 24 * u);
      float ty = ib.y + 16 * u;
      Text(dl, bold_, 22 * u, {la.x + (card_w - TextWidth(bold_, 22 * u, name)) * 0.5f, ty}, kWhite, name);
      ty += 30 * u;
      char sub[64];
      std::snprintf(sub, sizeof(sub), "%s  \xC2\xB7  %d", Ds3RegionName(d.region, german_), g.year);
      Text(dl, regular_, 17 * u, {la.x + (card_w - TextWidth(regular_, 17 * u, sub)) * 0.5f, ty}, kDim, sub);
      ty += 28 * u;
      const Need need = NeedOf(d);
      const bool this_one = install_.running() && install_dump_.game == d.game;
      const std::string state =
          this_one                       ? (install_.setting_up() ? (german_ ? "Wird eingerichtet ..." : "Setting up ...")
                                                                  : (german_ ? "Wird geladen ..." : "Downloading ..."))
          : need == Need::kOtherRegion   ? (german_ ? "Nur EU-Fassung" : "Europe only")
          : need == Need::kPackage       ? (german_ ? "Installieren (A)" : "Install (A)")
          : need == Need::kSetup         ? (german_ ? "Einrichten (A)" : "Set up (A)")
                                         : (german_ ? "Bereit" : "Ready");
      const ImU32 state_col = this_one ? kWhite : need == Need::kOtherRegion ? kRed : need == Need::kNothing ? kGreen : kGold;
      Text(dl, bold_, 18 * u, {la.x + (card_w - TextWidth(bold_, 18 * u, state)) * 0.5f, ty}, state_col, state);
    }
  }

  // Above the hints: the install's progress, or the last message.
  if (install_.running()) {
    const float bw = std::min(620 * u, width - 2 * left);
    const ImVec2 a{(width - bw) * 0.5f, bar_top - 46 * u};
    const float p = std::clamp(install_.progress(), 0.0f, 1.0f);
    const Ds3Game& g = Ds3Games()[install_dump_.game];
    const std::string name = german_ ? g.name_de : g.name_en;
    char line[160];
    if (install_.setting_up()) {
      std::snprintf(line, sizeof(line),
                    german_ ? "%s: die Spieldateien werden aus deinem Dump gemacht ..." : "%s: making the game files from your dump ...",
                    name.c_str());
    } else {
      std::snprintf(line, sizeof(line), german_ ? "%s wird geladen ... %d %%" : "Getting %s ... %d %%", name.c_str(), int(p * 100));
    }
    Text(dl, bold_, 19 * u, {a.x, a.y - 26 * u}, kWhite, line);
    dl->AddRectFilled(a, {a.x + bw, a.y + 12 * u}, Rgba(40, 50, 100), 6 * u);
    dl->AddRectFilled(a, {a.x + bw * p, a.y + 12 * u}, kGreen, 6 * u);
  } else if (!message_.empty() && ImGui::GetTime() < message_until_) {
    const float w = TextWidth(bold_, 19 * u, message_);
    Text(dl, bold_, 19 * u, {(width - w) * 0.5f, bar_top - 40 * u}, message_bad_ ? kRed : kGreen, message_);
  }

  float hx = left;
  const float hy = height - 52 * u;
  if (n > 0) hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 160, 70), german_ ? "Starten" : "Play");
  if (n > 0 && !installed.empty()) {
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Y", Rgba(220, 180, 40),
              newer ? (german_ ? "Aktualisieren" : "Update") : (german_ ? "Neu installieren" : "Reinstall"), "Y");
  }
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "X", Rgba(50, 120, 220), german_ ? "Ordner w\xC3\xA4hlen" : "Pick folder", "X");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
  ImGui::End();
}

}  // namespace coreoflight
