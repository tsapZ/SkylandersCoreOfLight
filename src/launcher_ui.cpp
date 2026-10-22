#include "launcher_ui.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "textures.h"

namespace coreoflight {
namespace {

ImU32 Rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

const ImU32 kGold = Rgba(255, 204, 64);
const ImU32 kGoldDark = Rgba(170, 120, 20);
const ImU32 kWhite = Rgba(255, 255, 255);
const ImU32 kDim = Rgba(180, 190, 215);

// Each game's colour on its card (a plain accent, no artwork).
const ImU32 kAccents[] = {Rgba(80, 170, 70), Rgba(230, 130, 40), Rgba(150, 90, 220),
                          Rgba(40, 150, 220), Rgba(220, 60, 60), Rgba(240, 180, 40)};

std::string Utf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string out(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), out.data(), n, nullptr, nullptr);
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return out;
}

void Text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const char* text,
          float wrap = 0.0f) {
  dl->AddText(font, size, pos, col, text, nullptr, wrap);
}

float TextWidth(ImFont* font, float size, const char* text) {
  return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
}

// A round button glyph like the games' hints: (A) green, (B) red.
float Hint(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const char* button, ImU32 col,
           const char* label) {
  const float r = size * 0.62f;
  const ImVec2 c{pos.x + r, pos.y + size * 0.55f};
  dl->AddCircleFilled(c, r, col);
  const float bw = TextWidth(font, size * 0.8f, button);
  Text(dl, font, size * 0.8f, {c.x - bw * 0.5f, c.y - size * 0.42f}, kWhite, button);
  const float x = pos.x + 2 * r + size * 0.4f;
  Text(dl, font, size, {x, pos.y}, kWhite, label);
  return x + TextWidth(font, size, label) + size * 1.2f;
}

}  // namespace

void LauncherUi::Init(const std::filesystem::path& launcher_dir, ImFont* regular, ImFont* bold) {
  launcher_dir_ = launcher_dir;
  regular_ = regular;
  bold_ = bold;
  german_ = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN;
  settings_.Init(regular, bold, german_);
  figures_screen_.Init(regular, bold, german_);
  profiles_screen_.Init(regular, bold, german_);
  achievements_screen_.Init(regular, bold, german_);
  Refresh();
}

void LauncherUi::Refresh() {
  FindGames(found_, launcher_dir_);
  LoadProfiles(german_);
  figures_ = CountFigures();
  CountAchievements();
  // A game may have been rebuilt or set up meanwhile: its settings are read again when needed.
  loaders_.assign(found_.size(), SettingsLoader());
  // Start on the newest installed game.
  for (size_t i = found_.size(); i-- > 0;) {
    if (!found_[i].exe.empty()) {
      selected_ = i;
      break;
    }
  }
}

void LauncherUi::CountAchievements() {
  achievements_.clear();
  last_saved_.clear();
  for (size_t i = 0; i < AllGames().size(); ++i) {
    achievements_.push_back(LoadAchievements(AllGames()[i], ActiveProfile()));
    last_saved_.push_back(i < found_.size() && !found_[i].exe.empty() ? LastSaved(ActiveProfile(), found_[i].exe)
                                                                        : 0);
  }
}

void LauncherUi::AddDisc(const std::filesystem::path& iso) {
  if (!import_.running()) import_.Start(iso, found_, german_);
}

void* LauncherUi::TakeStartedProcess() {
  void* p = started_;
  started_ = nullptr;
  return p;
}

void LauncherUi::Start(size_t index) {
  if (index >= found_.size() || found_[index].exe.empty()) return;
  void* process = nullptr;
  std::wstring error;
  if (StartGame(found_[index], error, &process)) {
    started_ = process;
  } else {
    error_ = error;
    error_until_ = ImGui::GetTime() + 6.0;
  }
}

void LauncherUi::OpenSettings(size_t index) {
  if (index >= found_.size() || found_[index].exe.empty()) return;
  if (!loaders_[index].started() || loaders_[index].state() < 0) {
    loaders_[index].Start(found_[index], AllGames()[index].key);
  }
  settings_game_ = index;
  const Game& game = AllGames()[index];
  settings_.Open(german_ ? game.name_de : game.name_en, &loaders_[index]);
}

void LauncherUi::DrawCard(size_t index, float x, float y, float w, float h, bool selected) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const Game& game = AllGames()[index];
  const Found& f = found_[index];
  const bool installed = !f.exe.empty();
  const float s = h / 420.0f;  // the card's own scale
  const float r = 22 * s;
  if (selected) {
    // The light of the core behind the chosen card.
    for (int k = 6; k > 0; --k) {
      const float grow = k * 7 * s;
      dl->AddRectFilled({x - grow, y - grow}, {x + w + grow, y + h + grow}, Rgba(255, 210, 90, 10), r + grow);
    }
  }
  dl->AddRectFilled({x, y}, {x + w, y + h}, Rgba(24, 30, 70, 235), r);
  dl->AddRect({x, y}, {x + w, y + h}, selected ? kGold : Rgba(90, 100, 150), r, 0, selected ? 5 * s : 2 * s);
  // The game's colour as a band at the top.
  const ImU32 accent = kAccents[index % 6];
  dl->AddRectFilled({x + 6 * s, y + 6 * s}, {x + w - 6 * s, y + 70 * s}, installed ? accent : Rgba(70, 75, 100),
                    r - 4 * s, ImDrawFlags_RoundCornersTop);
  char year[8];
  std::snprintf(year, sizeof(year), "%d", game.year);
  const float year_size = 40 * s;
  Text(dl, bold_, year_size, {x + (w - TextWidth(bold_, year_size, year)) * 0.5f, y + 14 * s}, kWhite, year);
  // The game's hero, once a game has left its picture (figures_ui.h), in the band's corners.
  if (hero_pictures_.size() <= index) hero_pictures_.resize(AllGames().size());
  if (hero_pictures_[index].empty() || ImGui::GetTime() > hero_checked_ + 10.0) {
    if (ImGui::GetTime() > hero_checked_ + 10.0) {
      hero_checked_ = ImGui::GetTime();
      for (size_t i = 0; i < AllGames().size(); ++i) {
        hero_pictures_[i] = FigurePicture(AllGames()[i].hero_id, AllGames()[i].hero_variant);
      }
    }
  }
  if (!hero_pictures_[index].empty()) {
    if (const ImTextureID texture = LoadTexture(hero_pictures_[index])) {
      const float side = 72 * s;
      const ImVec2 a{x + (w - side) * 0.5f, y + 144 * s};
      dl->AddImageRounded(texture, a, {a.x + side, a.y + side}, {0, 0}, {1, 1},
                          installed ? kWhite : Rgba(255, 255, 255, 90), 14 * s);
    }
  }
  const char* name = german_ ? game.name_de : game.name_en;
  float name_size = 34 * s;
  while (TextWidth(bold_, name_size, name) > w - 28 * s && name_size > 12) name_size -= 1;
  Text(dl, bold_, name_size, {x + (w - TextWidth(bold_, name_size, name)) * 0.5f, y + 110 * s},
       installed ? kWhite : kDim, name);
  // A few of the game's own settings (its settings file next to its exe), as its F4 menu calls them.
  if (installed) {
    std::vector<std::string> lines;
    auto get = [&](const char* key) {
      auto it = f.settings.find(key);
      return it == f.settings.end() ? std::string() : it->second;
    };
    if (const std::string scale = get("resolution_scale"); !scale.empty()) {
      lines.push_back((german_ ? "Aufl\xC3\xB6" "sung " : "Resolution ") + scale + "x");
    }
    if (const std::string fps = get("frame_rate_limit"); !fps.empty() && fps != "0") {
      lines.push_back(fps + " FPS");
    }
    if (!get("scene_size").empty()) lines.push_back(german_ ? "Natives 3D-Bild" : "Native 3D picture");
    // The active profile's achievements, once the game left its list.
    if (index < achievements_.size() && achievements_[index].known) {
      char text[64];
      std::snprintf(text, sizeof(text), german_ ? "Erfolge %d / %zu" : "Achievements %d / %zu",
                    achievements_[index].unlocked, achievements_[index].items.size());
      lines.insert(lines.begin(), text);
    }
    const std::string language = get("language");
    if (language == "german") lines.push_back("Deutsch");
    else if (language == "english") lines.push_back("English");
    else if (!language.empty() && language != "auto") lines.push_back(language);
    // Below the hero's picture there is room for three.
    if (!hero_pictures_[index].empty() && lines.size() > 3) lines.resize(3);
    float ly = y + (hero_pictures_[index].empty() ? 170 : 226) * s;
    for (const std::string& line : lines) {
      Text(dl, regular_, 21 * s, {x + 22 * s, ly}, kDim, line.c_str(), w - 44 * s);
      ly += 24 * s;
    }
  }
  // The disc's state.
  const char* state = nullptr;
  ImU32 state_col = kDim;
  if (!installed) {
    state = german_ ? "Nicht installiert" : "Not installed";
  } else {
    switch (f.disc.load()) {
      case DiscState::kMatches:
        state = german_ ? "Bereit" : "Ready";
        state_col = Rgba(110, 220, 110);
        break;
      case DiscState::kOther:
        state = german_ ? "Andere Disc-Version" : "Another disc version";
        state_col = Rgba(240, 170, 60);
        break;
      case DiscState::kMissing:
        state = german_ ? "Disc fehlt (rom\\default.xex)" : "Disc missing (rom\\default.xex)";
        state_col = Rgba(240, 90, 90);
        break;
      default:
        state = german_ ? "Disc wird gepr\xC3\xBC" "ft ..." : "Checking the disc ...";
        break;
    }
  }
  const float state_size = 20 * s;
  Text(dl, regular_, state_size, {x + 18 * s, y + h - 64 * s}, state_col, state, w - 36 * s);
  // When the active profile last saved it.
  if (installed && index < last_saved_.size() && last_saved_[index]) {
    const std::string when =
        (german_ ? "Zuletzt gespeichert " : "Last saved ") + FileTimeDate(last_saved_[index], german_);
    Text(dl, regular_, 16 * s, {x + 18 * s, y + h - 38 * s}, kDim, when.c_str(), w - 36 * s);
  }
  if (installed && selected) {
    const char* go = german_ ? "Starten" : "Play";
    const float go_size = 24 * s;
    const float gw = TextWidth(bold_, go_size, go) + 60 * s;
    const ImVec2 a{x + (w - gw) * 0.5f, y + h - 112 * s};
    dl->AddRectFilled(a, {a.x + gw, a.y + 44 * s}, kGoldDark, 22 * s);
    dl->AddRectFilled({a.x + 3 * s, a.y + 3 * s}, {a.x + gw - 3 * s, a.y + 41 * s}, kGold, 19 * s);
    Text(dl, bold_, go_size, {a.x + 30 * s, a.y + 8 * s}, Rgba(60, 35, 0), go);
  }
}

bool LauncherUi::DrawProfileBadge(float width, float u) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImGuiIO& io = ImGui::GetIO();
  const Profile& p = ActiveProfile();
  const float radius = 30 * u;
  const std::string label = german_ ? "Profil" : "Profile";
  const float name_w = std::max(TextWidth(bold_, 24 * u, p.name.c_str()), TextWidth(regular_, 17 * u, label.c_str()));
  const float right = width - 48 * u;
  const ImVec2 c{right - radius, 70 * u};
  const float left = c.x - radius - 14 * u - name_w;
  const bool hovered = io.MousePos.x >= left && io.MousePos.x < right && io.MousePos.y >= c.y - radius &&
                       io.MousePos.y < c.y + radius;
  if (hovered) {
    dl->AddRectFilled({left - 14 * u, c.y - radius - 8 * u}, {right + 8 * u, c.y + radius + 8 * u},
                      Rgba(255, 210, 90, 30), radius);
  }
  Text(dl, regular_, 17 * u, {c.x - radius - 14 * u - TextWidth(regular_, 17 * u, label.c_str()), c.y - 28 * u}, kDim,
       label.c_str());
  Text(dl, bold_, 24 * u, {c.x - radius - 14 * u - TextWidth(bold_, 24 * u, p.name.c_str()), c.y - 6 * u}, kWhite,
       p.name.c_str());
  bool drawn = false;
  if (p.has_picture) {
    const std::filesystem::path picture = FigurePicture(p.picture_id, p.picture_variant);
    if (const ImTextureID texture = picture.empty() ? ImTextureID{} : LoadTexture(picture)) {
      dl->AddCircleFilled(c, radius, Rgba(40, 50, 100));
      dl->AddImageRounded(texture, {c.x - radius, c.y - radius}, {c.x + radius, c.y + radius}, {0, 0}, {1, 1},
                          kWhite, radius);
      drawn = true;
    }
  }
  if (!drawn) {
    dl->AddCircleFilled(c, radius, Rgba(60, 70, 140));
    const std::string initial = p.name.empty() ? std::string("?") : p.name.substr(0, 1);
    Text(dl, bold_, 30 * u, {c.x - TextWidth(bold_, 30 * u, initial.c_str()) * 0.5f, c.y - 17 * u}, kWhite,
         initial.c_str());
  }
  dl->AddCircle(c, radius, kGold, 48, 2.5f * u);
  return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

bool LauncherUi::Draw(float width, float height) {
  const ImGuiIO& io = ImGui::GetIO();
  if (profiles_screen_.is_open()) {
    profiles_screen_.Draw(width, height);
    if (profiles_screen_.TakeChanged()) {
      // Another profile: its figures and pictures.
      figures_ = CountFigures();
      CountAchievements();
      hero_checked_ = -100.0;
    }
    return true;
  }
  if (achievements_screen_.is_open()) {
    achievements_screen_.Draw(width, height);
    return true;
  }
  if (figures_screen_.is_open()) {
    figures_screen_.Draw(width, height);
    return true;
  }
  if (settings_.is_open()) {
    settings_.Draw(width, height);
    // What was saved shows on the card at once.
    for (const auto& [cvar, value] : settings_.TakeSaved()) found_[settings_game_].settings[cvar] = value;
    return true;
  }
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("launcher", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float u = std::min(width / 1280.0f, height / 720.0f);

  // Background: deep blue with the Core of Light glowing in the middle.
  dl->AddRectFilledMultiColor({0, 0}, {width, height}, Rgba(10, 22, 60), Rgba(10, 22, 60), Rgba(3, 7, 22),
                              Rgba(3, 7, 22));
  const ImVec2 core{width * 0.5f, height * 0.56f};
  for (int k = 14; k > 0; --k) dl->AddCircleFilled(core, k * 38 * u, Rgba(90, 160, 255, 5), 64);

  // Title, and the profile the games start with.
  Text(dl, bold_, 22 * u, {48 * u, 34 * u}, kDim, "SKYLANDERS");
  Text(dl, bold_, 56 * u, {46 * u, 56 * u}, kGold, "CORE OF LIGHT");
  const bool profile_clicked = DrawProfileBadge(width, u);

  // The cards: six in a row, or three in two rows on a narrow window.
  const size_t n = found_.size();
  const bool two_rows = width / height < 1.45f;
  const size_t per_row = two_rows ? 3 : n;
  const float gap = 22 * u;
  const float area_w = width - 96 * u;
  const float card_w = std::min((area_w - gap * (per_row - 1)) / per_row, 300 * u);
  const float card_h = two_rows ? card_w * 1.25f : card_w * 1.9f;
  const float rows_h = two_rows ? 2 * card_h + gap : card_h;
  const float x0 = (width - (card_w * per_row + gap * (per_row - 1))) * 0.5f;
  const float y0 = 150 * u + (height - 230 * u - 150 * u - rows_h) * 0.5f;

  // Input: d-pad, stick, arrows; A / Enter starts, B / Escape closes.
  auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
  if (n) {
    if (pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_GamepadDpadLeft) || pressed(ImGuiKey_GamepadLStickLeft)) {
      selected_ = (selected_ + n - 1) % n;
    }
    if (pressed(ImGuiKey_RightArrow) || pressed(ImGuiKey_GamepadDpadRight) || pressed(ImGuiKey_GamepadLStickRight)) {
      selected_ = (selected_ + 1) % n;
    }
    if (two_rows && (pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_DownArrow) || pressed(ImGuiKey_GamepadDpadUp) ||
                     pressed(ImGuiKey_GamepadDpadDown))) {
      selected_ = (selected_ + per_row) % n;
    }
  }
  for (size_t i = 0; i < n; ++i) {
    const size_t row = i / per_row, col = i % per_row;
    const float x = x0 + col * (card_w + gap), y = y0 + row * (card_h + gap);
    const bool hovered = io.MousePos.x >= x && io.MousePos.x < x + card_w && io.MousePos.y >= y &&
                         io.MousePos.y < y + card_h;
    if (hovered && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) selected_ = i;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      if (selected_ == i) Start(i);
      selected_ = i;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      selected_ = i;
      OpenSettings(i);
    }
    DrawCard(i, x, y, card_w, card_h, i == selected_);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false) ||
      ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)) {
    Start(selected_);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false) || ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
    OpenSettings(selected_);
  }
  // Start / F: the figure library (figures_ui.h).
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) || ImGui::IsKeyPressed(ImGuiKey_F, false)) {
    figures_screen_.Open(FiguresDir());
  }
  // RB / E: the chosen game's achievements (achievements_ui.h).
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) || ImGui::IsKeyPressed(ImGuiKey_E, false)) {
    achievements_screen_.Open(selected_);
  }
  // Back / P: the profiles (profiles_ui.h).
  if (profile_clicked || ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false) || ImGui::IsKeyPressed(ImGuiKey_P, false)) {
    profiles_screen_.Open(&found_);
  }
  // X / D: the owner's own disc (ISO) for a game, recognised and unpacked into its rom folder.
  if (!import_.running() &&
      (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false) || ImGui::IsKeyPressed(ImGuiKey_D, false))) {
    std::filesystem::path iso;
    if (DiscImport::PickIso(iso, german_)) import_.Start(iso, found_, german_);
  }
  // O: a folder with an unpacked disc instead (dropping one on the window works too).
  if (!import_.running() && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
    std::filesystem::path folder;
    if (DiscImport::PickFolder(folder, german_)) import_.Start(folder, found_, german_);
  }
  // The chosen game's settings are read ahead (the game takes a few seconds to name them).
  if (selected_ < loaders_.size() && !found_[selected_].exe.empty() && !loaders_[selected_].started()) {
    loaders_[selected_].Start(found_[selected_], AllGames()[selected_].key);
  }
  // Not while a disc is being unpacked (it would stop half done).
  const bool quit = !import_.running() && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                                           ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false));

  // The figure folder and the hints at the bottom.
  // The folder as Windows names it when it is the usual one, else its path (coreoflight.ini).
  std::string folder = german_ ? "Dokumente\\Skylanders\\Figures" : "Documents\\Skylanders\\Figures";
  {
    const std::filesystem::path usual = DocumentsDir() / "Skylanders" / "Figures";
    if (FiguresDir() != usual) {
      const std::u8string u8 = FiguresDir().u8string();
      folder = std::string(u8.begin(), u8.end());
    }
  }
  char figures[600];
  if (IsMainProfile(ActiveProfile())) {
    std::snprintf(figures, sizeof(figures), german_ ? "%d Figuren im Ordner %s" : "%d figures in %s", figures_,
                  folder.c_str());
  } else {
    std::snprintf(figures, sizeof(figures), german_ ? "%d Figuren im Profil %s" : "%d figures in the profile %s",
                  figures_, ActiveProfile().name.c_str());
  }
  Text(dl, regular_, 20 * u, {48 * u, height - 92 * u}, kDim, figures);
  float hx = 48 * u;
  const float hy = height - 52 * u;
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 170, 70), german_ ? "Starten" : "Play");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Y", Rgba(220, 180, 40), german_ ? "Einstellungen" : "Settings");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "X", Rgba(50, 120, 220), german_ ? "Disc einlesen" : "Add a disc");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Start", Rgba(80, 90, 140), german_ ? "Figuren" : "Figures");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "RB", Rgba(80, 90, 140), german_ ? "Erfolge" : "Achievements");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Back", Rgba(80, 90, 140), german_ ? "Profile" : "Profiles");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Beenden" : "Quit");
  // The disc being set up: what happens, and how far the unpacking is.
  const DiscImport::Phase phase = import_.phase();
  if (phase != import_seen_) {
    if (phase == DiscImport::Phase::kDone) {
      const int game = import_.game();
      Refresh();  // the disc check again
      if (game >= 0) selected_ = size_t(game);
    }
    if (phase == DiscImport::Phase::kDone || phase == DiscImport::Phase::kFailed) {
      import_shown_until_ = ImGui::GetTime() + 10.0;
    }
    import_seen_ = phase;
  }
  if (import_.running() || (phase != DiscImport::Phase::kIdle && ImGui::GetTime() < import_shown_until_)) {
    const float bx = width * 0.5f, by = height - 100 * u, bw = width * 0.5f - 48 * u;
    const std::string text = import_.message();
    const ImU32 col = phase == DiscImport::Phase::kFailed ? Rgba(240, 110, 110)
                      : phase == DiscImport::Phase::kDone ? Rgba(110, 220, 110)
                                                          : kWhite;
    Text(dl, regular_, 19 * u, {bx, by - 4 * u}, col, text.c_str(), bw);
    if (phase == DiscImport::Phase::kUnpacking) {
      const float p = std::clamp(import_.progress(), 0.0f, 1.0f);
      dl->AddRectFilled({bx, by + 46 * u}, {bx + bw, by + 60 * u}, Rgba(40, 50, 100), 7 * u);
      dl->AddRectFilled({bx, by + 46 * u}, {bx + bw * p, by + 60 * u}, kGold, 7 * u);
      char percent[16];
      std::snprintf(percent, sizeof(percent), "%d %%", int(p * 100));
      Text(dl, bold_, 18 * u, {bx + bw - TextWidth(bold_, 18 * u, percent), by + 64 * u}, kDim, percent);
    }
  }
  if (!error_.empty() && ImGui::GetTime() < error_until_) {
    const std::string text = (german_ ? "Konnte nicht starten: " : "Could not start: ") + Utf8(error_);
    Text(dl, regular_, 20 * u, {hx + 20 * u, hy}, Rgba(240, 110, 110), text.c_str());
  }
  ImGui::End();
  return !quit;
}

}  // namespace coreoflight
