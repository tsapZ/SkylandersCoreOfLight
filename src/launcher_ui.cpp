#include "launcher_ui.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <string_view>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "nxe_art.h"
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
  hub_ = LoadHubSettings();
  ApplyHubSettings();
  Refresh();
}

void LauncherUi::ApplyHubSettings() {
  german_ = HubGerman(hub_);
  settings_.Init(regular_, bold_, german_);
  hub_screen_.Init(regular_, bold_, german_);
  browser_.Init(regular_, bold_, german_);
  figures_screen_.Init(regular_, bold_, german_);
  profiles_screen_.Init(regular_, bold_, german_);
  achievements_screen_.Init(regular_, bold_, german_);
}

void LauncherUi::Refresh() {
  FindGames(found_, launcher_dir_);
  LoadProfiles(german_);
  figures_ = CountFigures();
  CountAchievements();
  // A game may have been rebuilt or set up meanwhile: its settings are read again when needed.
  loaders_.assign(found_.size(), SettingsLoader());
  FindArt();
  // Start on the game the active profile saved last, else on the newest installed one. Later
  // refreshes (a game ended, a disc set up) keep the card the owner is on.
  if (selection_made_ && selected_ < found_.size() && !found_[selected_].exe.empty()) return;
  bool chosen = false;
  uint64_t newest_save = 0;
  for (size_t i = 0; i < found_.size() && i < last_saved_.size(); ++i) {
    if (!found_[i].exe.empty() && last_saved_[i] > newest_save) {
      newest_save = last_saved_[i];
      selected_ = i;
      chosen = true;
    }
  }
  for (size_t i = found_.size(); !chosen && i-- > 0;) {
    if (!found_[i].exe.empty()) {
      selected_ = i;
      chosen = true;
    }
  }
  selection_made_ = chosen;
}

void LauncherUi::FindArt() {
  tile_art_.assign(found_.size(), {});
  background_art_.assign(found_.size(), {});
  for (size_t i = 0; i < found_.size() && i < AllGames().size(); ++i) {
    if (found_[i].exe.empty() || found_[i].rom.empty()) continue;
    // Next to the pictures the game itself leaves (Documents\Skylanders\Pictures\<game>).
    const std::filesystem::path cache = MainFiguresDir().parent_path() / "Pictures" /
                                        std::filesystem::path(reinterpret_cast<const char8_t*>(AllGames()[i].name_en));
    tile_art_[i] = NxeArt(found_[i].rom, cache, "nxeslot.jpg");
    background_art_[i] = NxeArt(found_[i].rom, cache, "nxebg.jpg");
  }
  SetGameBackgrounds(background_art_);
}

void LauncherUi::CountAchievements() {
  achievements_.clear();
  last_saved_.clear();
  play_seconds_.clear();
  for (size_t i = 0; i < AllGames().size(); ++i) {
    achievements_.push_back(LoadAchievements(AllGames()[i], ActiveProfile()));
    play_seconds_.push_back(PlaySeconds(ActiveProfile(), AllGames()[i].key));
    last_saved_.push_back(i < found_.size() && !found_[i].exe.empty() ? LastSaved(ActiveProfile(), found_[i].exe)
                                                                        : 0);
  }
}

void LauncherUi::GameEnded() {
  if (running_key_.empty()) return;
  AddPlaySeconds(running_profile_, running_key_, (GetTickCount64() - running_since_) / 1000);
  running_key_.clear();
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
  music_.Stop();
  if (index >= found_.size() || found_[index].exe.empty()) return;
  void* process = nullptr;
  std::wstring error;
  if (StartGame(found_[index], error, &process)) {
    started_ = process;
    running_key_ = AllGames()[index].key;
    running_profile_ = ActiveProfile();
    running_since_ = GetTickCount64();
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
  settings_.Open(german_ ? game.name_de : game.name_en, &loaders_[index], GameBackground(index));
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
  char year[8];
  std::snprintf(year, sizeof(year), "%d", game.year);
  // The disc's dashboard tile (its logo, 420x320) at the top, once the game is set up (nxe_art.h);
  // else the game's colour as a band with the year, and its name below.
  const ImTextureID tile =
      index < tile_art_.size() && !tile_art_[index].empty() ? LoadTexture(tile_art_[index]) : ImTextureID{};
  float content_y = 0;  // where the hero's picture goes; 0: the layout without a tile
  if (tile) {
    const float tw = w - 12 * s, th = tw * 320.0f / 420.0f;
    dl->AddImageRounded(tile, {x + 6 * s, y + 6 * s}, {x + 6 * s + tw, y + 6 * s + th}, {0, 0}, {1, 1}, kWhite,
                        r - 4 * s, ImDrawFlags_RoundCornersTop);
    // The year small in the tile's corner.
    const float ys = 18 * s, yw = TextWidth(bold_, ys, year);
    dl->AddRectFilled({x + 12 * s, y + 12 * s}, {x + 24 * s + yw, y + 16 * s + ys}, Rgba(10, 14, 40, 190), 8 * s);
    Text(dl, bold_, ys, {x + 18 * s, y + 14 * s}, kWhite, year);
    content_y = y + 6 * s + th + 10 * s;
  } else {
    const ImU32 accent = kAccents[index % 6];
    dl->AddRectFilled({x + 6 * s, y + 6 * s}, {x + w - 6 * s, y + 70 * s}, installed ? accent : Rgba(70, 75, 100),
                      r - 4 * s, ImDrawFlags_RoundCornersTop);
    const float year_size = 40 * s;
    Text(dl, bold_, year_size, {x + (w - TextWidth(bold_, year_size, year)) * 0.5f, y + 14 * s}, kWhite, year);
  }
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
  const float hero_side = 72 * s;
  const float hero_y = tile ? content_y : y + 144 * s;
  if (!hero_pictures_[index].empty()) {
    if (const ImTextureID texture = LoadTexture(hero_pictures_[index])) {
      const ImVec2 a{x + (w - hero_side) * 0.5f, hero_y};
      dl->AddImageRounded(texture, a, {a.x + hero_side, a.y + hero_side}, {0, 0}, {1, 1},
                          installed ? kWhite : Rgba(255, 255, 255, 90), 14 * s);
    }
  }
  if (!tile) {
    // (the tile's logo already names the game)
    const char* name = german_ ? game.name_de : game.name_en;
    float name_size = 34 * s;
    while (TextWidth(bold_, name_size, name) > w - 28 * s && name_size > 12) name_size -= 1;
    Text(dl, bold_, name_size, {x + (w - TextWidth(bold_, name_size, name)) * 0.5f, y + 110 * s},
         installed ? kWhite : kDim, name);
  }
  // The active profile's play time and achievements (the owner's choice, 2026-10-22: these, not the
  // game's settings, which are on Y).
  if (installed) {
    std::vector<std::string> lines;
    // The title music (music.h): a hint while it isn't recorded yet.
    std::error_code music_ec;
    if (hub_.title_music && f.disc.load() == DiscState::kMatches &&
        !std::filesystem::is_regular_file(TitleMusicFile(index), music_ec)) {
      lines.push_back(german_ ? "Titelmusik holen: M / R3" : "Get the title music: M / R3");
    }
    // The active profile's achievements, once the game left its list.
    if (index < achievements_.size() && achievements_[index].known) {
      char text[64];
      std::snprintf(text, sizeof(text), german_ ? "Erfolge %d / %zu" : "Achievements %d / %zu",
                    achievements_[index].unlocked, achievements_[index].items.size());
      lines.insert(lines.begin(), text);
    }
    // The active profile's time in it, as far as the launcher started it.
    if (index >= play_seconds_.size() || !play_seconds_[index]) {
      lines.insert(lines.begin(), german_ ? "Noch nicht gespielt" : "Not played yet");
    } else {
      const uint64_t minutes = play_seconds_[index] / 60, hours = minutes / 60;
      char text[64];
      if (hours) {
        std::snprintf(text, sizeof(text), german_ ? "Gespielt %llu Std. %llu Min." : "Played %llu h %llu min",
                      (unsigned long long)hours, (unsigned long long)(minutes % 60));
      } else if (minutes) {
        std::snprintf(text, sizeof(text), german_ ? "Gespielt %llu Min." : "Played %llu min", (unsigned long long)minutes);
      } else {
        std::snprintf(text, sizeof(text), german_ ? "Gespielt < 1 Min." : "Played < 1 min");
      }
      lines.insert(lines.begin(), text);
    }
    // As many as fit between the hero's picture (or the name) and the disc's state at the bottom,
    // or the chosen card's play button (at h - 112).
    float ly = tile ? hero_y + (hero_pictures_[index].empty() ? 0 : hero_side + 10 * s)
                    : y + (hero_pictures_[index].empty() ? 170 : 226) * s;
    const float lines_end = y + h - (selected ? 116 : 70) * s;
    const size_t fit = size_t(std::max(0.0f, (lines_end - ly) / (24 * s)));
    if (lines.size() > fit) lines.resize(fit);
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
  // The screen this frame will draw, in DrawScreens' order.
  const char* window = capture_process_                ? "capture"
                       : browser_.is_open()             ? "files"
                       : profiles_screen_.is_open()     ? "profiles"
                       : achievements_screen_.is_open() ? "achievements"
                       : figures_screen_.is_open()      ? "figures"
                       : hub_screen_.is_open() || settings_.is_open() ? "settings"
                                                                      : "launcher";
  const double now = ImGui::GetTime();
  if (window != shown_window_) {
    shown_window_ = window;
    shown_since_ = now;
  }
  const bool keep = DrawScreens(width, height);
  // The chosen game's title music, once it recorded it (and while no recording runs).
  {
    std::filesystem::path wanted;
    if (hub_.title_music && !capture_process_ && selected_ < found_.size() && !found_[selected_].exe.empty()) {
      wanted = TitleMusicFile(selected_);
    }
    music_.Play(wanted);
    music_.Update(ImGui::GetIO().DeltaTime);
  }
  // The animation: the frame's vertices of that window moved and faded (the first 0.32 s): a
  // screen slides in from the left, the start screen rises when it comes back.
  const float t = hub_.animations ? std::clamp(float((now - shown_since_) / 0.32), 0.0f, 1.0f) : 1.0f;
  if (t < 1.0f) {
    if (ImGuiWindow* w = ImGui::FindWindowByName(window)) {
      const float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
      const bool start_screen = std::string_view(window) == "launcher";
      const float dx = start_screen ? 0.0f : -(1.0f - ease) * width * 0.18f;
      const float dy = start_screen ? (1.0f - ease) * height * 0.05f : 0.0f;
      const float fade = start_screen ? 0.25f + 0.75f * ease : ease;
      for (ImDrawVert& v : w->DrawList->VtxBuffer) {
        v.pos.x += dx;
        v.pos.y += dy;
        const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 0xFF;
        v.col = (v.col & ~IM_COL32_A_MASK) | (ImU32(float(a) * fade) << IM_COL32_A_SHIFT);
      }
    }
  }
  return keep;
}

void LauncherUi::StartCapture(size_t index) {
  if (capture_process_ || index >= found_.size()) return;
  const Found& f = found_[index];
  if (f.exe.empty() || f.disc.load() != DiscState::kMatches) {
    error_ = german_ ? L"Erst die Disc des Spiels einlesen (X)" : L"Set up the game's disc first (X)";
    error_until_ = ImGui::GetTime() + 4.0;
    return;
  }
  std::wstring error;
  music_.Stop();
  if (!StartTitleMusicCapture(index, f, error, &capture_process_)) {
    error_ = german_ ? L"Dieser Build des Spiels kann seine Titelmusik noch nicht aufnehmen" : error;
    error_until_ = ImGui::GetTime() + 5.0;
    capture_process_ = nullptr;
    return;
  }
  capture_game_ = index;
  capture_since_ = ImGui::GetTime();
}

// While a game records its title music: what happens, how far, and its end.
void LauncherUi::DrawCapture(float width, float height, float u) {
  const DWORD wait = WaitForSingleObject(capture_process_, 0);
  const double elapsed = ImGui::GetTime() - capture_since_;
  if (wait == WAIT_OBJECT_0 || elapsed > 150.0) {
    if (wait != WAIT_OBJECT_0) TerminateProcess(capture_process_, 3);
    CloseHandle(capture_process_);
    capture_process_ = nullptr;
    std::error_code ec;
    const bool ok = std::filesystem::is_regular_file(TitleMusicFile(capture_game_), ec);
    error_ = ok ? (german_ ? L"Titelmusik aufgenommen" : L"Title music recorded")
                : (german_ ? L"Keine Titelmusik aufgenommen (das Spiel war still)" : L"No title music recorded");
    error_until_ = ImGui::GetTime() + 4.0;
    return;
  }
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("capture", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  DrawBackdrop(dl, width, height,
               capture_game_ < background_art_.size() ? background_art_[capture_game_] : std::filesystem::path());
  const Game& game = AllGames()[capture_game_];
  const std::string title = std::string(german_ ? "Titelmusik: " : "Title music: ") + (german_ ? game.name_de : game.name_en);
  Text(dl, bold_, 44 * u, {(width - TextWidth(bold_, 44 * u, title.c_str())) * 0.5f, height * 0.36f}, kGold, title.c_str());
  const char* line = german_ ? "Das Spiel l\xC3\xA4uft kurz stumm bis zu seinem Titelbild und nimmt die Musik auf."
                             : "The game runs briefly, muted, to its title screen and records the music.";
  Text(dl, regular_, 22 * u, {(width - TextWidth(regular_, 22 * u, line)) * 0.5f, height * 0.36f + 64 * u}, kDim, line);
  const float bw = 520 * u, bh = 16 * u;
  const ImVec2 a{(width - bw) * 0.5f, height * 0.36f + 116 * u};
  const float progress = float(std::min(1.0, elapsed / 60.0));
  dl->AddRectFilled(a, {a.x + bw, a.y + bh}, Rgba(30, 40, 90, 230), bh * 0.5f);
  dl->AddRectFilled(a, {a.x + bw * progress, a.y + bh}, kGold, bh * 0.5f);
  char seconds[64];
  std::snprintf(seconds, sizeof(seconds), german_ ? "noch etwa %d s" : "about %d s left",
                std::max(0, 60 - int(elapsed)));
  Text(dl, regular_, 18 * u, {(width - TextWidth(regular_, 18 * u, seconds)) * 0.5f, a.y + 28 * u}, kDim, seconds);
  ImGui::End();
}

bool LauncherUi::DrawScreens(float width, float height) {
  const ImGuiIO& io = ImGui::GetIO();
  if (capture_process_) {
    DrawCapture(width, height, std::min(width / 1280.0f, height / 720.0f));
    return true;
  }
  if (browser_.is_open()) {
    browser_.Draw(width, height);
    // The chosen disc is recognised by its default.xex and unpacked into its game's rom folder.
    if (const std::filesystem::path chosen = browser_.TakeChosen(); !chosen.empty() && !import_.running()) {
      import_.Start(chosen, found_, german_);
    }
    return true;
  }
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
  if (hub_screen_.is_open()) {
    hub_screen_.Draw(width, height);
    // Saved settings apply at once (the language with the next frame).
    if (!hub_screen_.TakeSaved().empty()) {
      hub_ = LoadHubSettings();
      ApplyHubSettings();
      LoadProfiles(german_);
    }
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

  // Background: the chosen game's dashboard picture from its disc (1920x1080, filling the window),
  // darkened; without one deep blue. The Core of Light glows in the middle either way.
  DrawBackdrop(dl, width, height,
               selected_ < background_art_.size() ? background_art_[selected_] : std::filesystem::path());
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
    if (lift_.size() != n) lift_.assign(n, 0.0f);
    const float target = i == selected_ ? 1.0f : 0.0f;
    lift_[i] = hub_.animations ? lift_[i] + (target - lift_[i]) * std::min(1.0f, io.DeltaTime * 12.0f) : target;
    DrawCard(i, x, y - lift_[i] * 12 * u, card_w, card_h, i == selected_);
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
    figures_screen_.Configure(hub_.figures_only_mine, hub_.figures_marks);
    figures_screen_.Open(FiguresDir());
  }
  // A disc just set up: its title music, once the disc check says it is ready.
  if (pending_capture_ >= 0 && size_t(pending_capture_) < found_.size() &&
      found_[size_t(pending_capture_)].disc.load() != DiscState::kChecking) {
    const size_t game = size_t(pending_capture_);
    pending_capture_ = -1;
    if (found_[game].disc.load() == DiscState::kMatches) StartCapture(game);
  }
  // M / R3 (the right stick pressed): the chosen game records its title music (music.h).
  if (!capture_process_ && (ImGui::IsKeyPressed(ImGuiKey_M, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadR3, false))) {
    StartCapture(selected_);
  }
  // LB / Q: the launcher's own settings (hub_settings.h).
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
    hub_loader_.SetReady(HubSettingsPages(hub_));
    hub_screen_.Open(german_ ? "Einstellungen des Hubs" : "Launcher settings", &hub_loader_);
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
    browser_.Open();  // ISO files and unpacked discs alike, with the pad (file_browser.h)
  }
  // O: the Windows dialog for a folder with an unpacked disc (dropping one on the window works too).
  if (!import_.running() && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
    std::filesystem::path folder;
    if (DiscImport::PickFolder(folder, german_)) import_.Start(folder, found_, german_);
  }
  // No game ready yet (the first start): say how to set one up, with a button for the mouse.
  {
    bool any_ready = false, checking = false;
    for (const Found& g : found_) {
      if (g.exe.empty()) continue;
      const DiscState state = g.disc.load();
      any_ready = any_ready || state == DiscState::kMatches;
      checking = checking || state == DiscState::kChecking;
    }
    if (!any_ready && !checking && !import_.running()) {
      const float bw = std::min(760 * u, width - 96 * u), bh = 64 * u;
      const ImVec2 a{(width - bw) * 0.5f, y0 + rows_h + 18 * u}, b{a.x + bw, a.y + bh};
      const bool over = io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y;
      dl->AddRectFilled(a, b, over ? Rgba(70, 80, 150, 245) : Rgba(24, 30, 70, 240), 20 * u);
      dl->AddRect(a, b, kGold, 20 * u, 0, 3 * u);
      const char* line = german_ ? "Noch keine Disc eingelesen  \xC2\xB7  X: Disc einlesen (ISO ausw\xC3\xA4hlen)"
                                 : "No disc set up yet  \xC2\xB7  X: add a disc (choose its ISO)";
      Text(dl, bold_, 22 * u, {a.x + (bw - TextWidth(bold_, 22 * u, line)) * 0.5f, a.y + 19 * u}, kWhite, line);
      if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) browser_.Open();
    }
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
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "LB", Rgba(80, 90, 140), german_ ? "Hub" : "Hub");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Beenden" : "Quit");
  // The disc being set up: what happens, and how far the unpacking is.
  const DiscImport::Phase phase = import_.phase();
  if (phase != import_seen_) {
    if (phase == DiscImport::Phase::kDone) {
      const int game = import_.game();
      Refresh();  // the disc check again
      if (game >= 0) selected_ = size_t(game);
      std::error_code music_ec;
      if (game >= 0 && hub_.title_music && !std::filesystem::is_regular_file(TitleMusicFile(size_t(game)), music_ec)) {
        pending_capture_ = game;
      }
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
