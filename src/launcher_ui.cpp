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
#include "pad_state.h"
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

std::wstring Wide(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}

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
  // The first-run setup: on a first start with no game there (a friend's PC, not a developer's).
  if (!hub_.setup_done && std::none_of(found_.begin(), found_.end(), [](const Found& f) { return !f.exe.empty(); })) {
    setup_.Open();
  }
}

void LauncherUi::ApplyHubSettings() {
  german_ = HubGerman(hub_);
  settings_.Init(regular_, bold_, german_);
  hub_screen_.Init(regular_, bold_, german_);
  browser_.Init(regular_, bold_, german_);
  figures_screen_.Init(regular_, bold_, german_);
  profiles_screen_.Init(regular_, bold_, german_);
  achievements_screen_.Init(regular_, bold_, german_);
  setup_.Init(regular_, bold_, german_);
}

void LauncherUi::Refresh() {
  FindGames(found_, launcher_dir_);
  setups_.clear();
  installed_versions_.clear();
  for (size_t i = 0; i < AllGames().size(); ++i) {
    setups_.push_back(FindSetup(AllGames()[i], launcher_dir_));
    installed_versions_.push_back(i < found_.size() && !found_[i].exe.empty()
                                      ? InstalledVersion(AllGames()[i], found_[i].exe)
                                      : std::string());
  }
  update_put_off_.resize(AllGames().size(), false);
  LoadProfiles(german_);
  figures_ = CountFigures();
  const std::vector<GameAchievements> before = achievements_;
  CountAchievements();
  // A platinum trophy since the last look (a game ended with its last achievement): shown once.
  for (size_t i = 0; i < before.size() && i < achievements_.size(); ++i) {
    if (before[i].known && !before[i].platinum && achievements_[i].platinum) {
      platinum_new_ = int(i);
      platinum_until_ = -12.0;  // 12 s from the next frame on (Draw)
    }
  }
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

void LauncherUi::UpdateImport() {
  const DiscImport::Phase phase = import_.phase();
  if (phase == import_seen_) return;
  if (phase == DiscImport::Phase::kDone) {
    const int game = import_.game();
    Refresh();  // the disc check again
    if (game >= 0) selected_ = size_t(game);
    // Its title music again, now that the disc is there.
    if (game >= 0 && size_t(game) < export_tried_.size()) export_tried_[size_t(game)] = false;
  }
  if (phase == DiscImport::Phase::kDone || phase == DiscImport::Phase::kFailed) {
    import_shown_until_ = ImGui::GetTime() + 10.0;
  }
  import_seen_ = phase;
}

// A game's setup running from its card: when it ends, the games are looked for again; the card
// then asks for the disc ("Disc einlesen" on A). It copies about 100 MB, a few seconds.
void LauncherUi::UpdateInstall() {
  if (!install_process_) return;
  const DWORD wait = WaitForSingleObject(install_process_, 0);
  if (wait != WAIT_OBJECT_0 && ImGui::GetTime() - install_since_ < 300.0) return;
  DWORD code = 3;
  if (wait == WAIT_OBJECT_0) {
    GetExitCodeProcess(install_process_, &code);
  } else {
    TerminateProcess(install_process_, 3);
  }
  CloseHandle(install_process_);
  install_process_ = nullptr;
  const size_t game = install_game_;
  Refresh();
  const std::string name = german_ ? AllGames()[game].name_de : AllGames()[game].name_en;
  const bool update = std::exchange(install_is_update_, false);
  if (code == 0 && game < found_.size() && !found_[game].exe.empty()) {
    selected_ = game;
    if (update) {
      const std::string version = game < installed_versions_.size() ? installed_versions_[game] : std::string();
      error_ = Wide(name + (german_ ? " ist aktualisiert (" : " is updated (") + version + ")");
    } else {
      error_ = Wide(name + (german_ ? " ist installiert. A: die Disc einlesen" : " is installed. A: add the disc"));
    }
  } else {
    const std::string why = GameInstallError(game);
    char text[256];
    std::snprintf(text, sizeof(text), german_ ? "%s: Installation abgebrochen (Code %lu)%s%s" : "%s: installation stopped (code %lu)%s%s",
                  name.c_str(), code, why.empty() ? "" : ": ", why.c_str());
    error_ = Wide(text);
  }
  error_until_ = ImGui::GetTime() + 8.0;
}

void LauncherUi::GameExitCode(unsigned long code) {
  starting_ = false;
  if (code == 0) return;
  wchar_t text[160];
  std::swprintf(text, std::size(text),
                german_ ? L"Das Spiel wurde unerwartet beendet (Code 0x%08lX); sein Log liegt im Ordner logs des Spiels"
                        : L"The game ended unexpectedly (code 0x%08lX); its log is in the game's logs folder",
                code);
  error_ = text;
  error_until_ = -12.0;  // 12 s from the next frame on (Draw)
}

void LauncherUi::ToggleFullscreen() {
  hub_.fullscreen = !hub_.fullscreen;
  WriteGameSetting(HubSettingsFile(), "Hub", "fullscreen", "bool", hub_.fullscreen ? "true" : "false");
}

void LauncherUi::GameEnded() {
  if (running_key_.empty()) return;
  AddPlaySeconds(running_profile_, running_key_, (GetTickCount64() - running_since_) / 1000);
  running_key_.clear();
}

void LauncherUi::AddDisc(const std::filesystem::path& iso) {
  // During the first-run setup a dropped ISO is filled in there (its game's row) instead.
  if (setup_.is_open()) {
    setup_.AddIso(iso);
    return;
  }
  if (!import_.running()) import_.Start(iso, found_, german_);
}

void* LauncherUi::TakeStartedProcess() {
  void* p = started_;
  started_ = nullptr;
  return p;
}

void LauncherUi::Start(size_t index) {
  if (index >= found_.size()) return;
  // A does what the card needs: a game without its disc opens the disc browser, one that isn't
  // installed or has another disc version says so (nothing could run).
  auto say = [&](const wchar_t* de, const wchar_t* en) {
    error_ = german_ ? de : en;
    error_until_ = ImGui::GetTime() + 6.0;
  };
  if (found_[index].exe.empty()) {
    // Not installed: its setup, silently, when the launcher has one (games.h StartGameInstall);
    // the disc comes after, with the launcher's own import.
    if (install_process_) {
      say(L"Ein Spiel wird gerade installiert", L"A game is being installed");
      return;
    }
    if (index < setups_.size() && !setups_[index].empty()) {
      if (StartGameInstall(index, setups_[index], german_, &install_process_)) {
        install_game_ = index;
        install_since_ = ImGui::GetTime();
      } else {
        install_process_ = nullptr;
        say(L"Der Installer lie\x00DF sich nicht starten", L"Could not start the installer");
      }
      return;
    }
    say(L"Dieses Spiel ist nicht installiert (sein Installer aus seinem Projekt)",
        L"This game isn't installed (its installer from its project)");
    return;
  }
  // A newer setup than the installed game: ask first (once a launch; "Erst spielen" plays).
  if (UpdateAvailable(index) && !install_process_ && index < update_put_off_.size() && !update_put_off_[index]) {
    ask_update_ = int(index);
    return;
  }
  switch (found_[index].disc.load()) {
    case DiscState::kMatches:
      break;
    case DiscState::kMissing:
      if (!import_.running()) browser_.Open();
      return;
    case DiscState::kOther:
      say(L"Diese Disc-Version unterst\x00FCtzt das Spiel nicht (X: eine andere Disc einlesen)",
          L"The game doesn't support this disc version (X: set up another disc)");
      return;
    default:
      say(L"Die Disc wird noch gepr\x00FC" L"ft", L"The disc is still being checked");
      return;
  }
  music_.Stop();
  void* process = nullptr;
  std::wstring error;
  if (StartGame(found_[index], error, &process)) {
    started_ = process;
    starting_ = true;
    starting_game_ = index;
    starting_since_ = ImGui::GetTime();
    running_key_ = AllGames()[index].key;
    running_profile_ = ActiveProfile();
    running_since_ = GetTickCount64();
  } else {
    error_ = (german_ ? L"Konnte nicht starten: " : L"Could not start: ") + error;
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
    // The active profile's achievements, once the game left its list; with the platinum trophy
    // behind them when every one is unlocked.
    std::string achievements_line;
    if (index < achievements_.size() && achievements_[index].known) {
      char text[64];
      std::snprintf(text, sizeof(text), german_ ? "Erfolge %d / %zu" : "Achievements %d / %zu",
                    achievements_[index].unlocked, achievements_[index].items.size());
      lines.insert(lines.begin(), text);
      if (achievements_[index].platinum) achievements_line = text;
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
    // A picture export running for it (#12): first, so it shows on every card.
    if (pictures_process_ && pictures_game_ == index) {
      lines.insert(lines.begin(), german_ ? "Bilder werden vorbereitet ..." : "Getting its pictures ...");
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
      if (!achievements_line.empty() && line == achievements_line) {
        DrawPlatinumTrophy(dl, x + 22 * s + TextWidth(regular_, 21 * s, line.c_str()) + 16 * s, ly + 11 * s, 22 * s,
                           true);
      }
      ly += 24 * s;
    }
  }
  // The disc's state.
  const char* state = nullptr;
  ImU32 state_col = kDim;
  if (!installed) {
    const bool installing = install_process_ && install_game_ == index;
    state = installing ? (german_ ? "Wird installiert ..." : "Installing ...")
                       : (german_ ? "Nicht installiert" : "Not installed");
  } else {
    switch (f.disc.load()) {
      case DiscState::kMatches:
        state = UpdateAvailable(index) ? (german_ ? "Bereit  \xC2\xB7  Update verf\xC3\xBC" "gbar" : "Ready  \xC2\xB7  update available")
                                       : (german_ ? "Bereit" : "Ready");
        state_col = UpdateAvailable(index) ? kGold : Rgba(110, 220, 110);
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
  const DiscState disc_state = f.disc.load();
  const bool can_install = !installed && !install_process_ && index < setups_.size() && !setups_[index].empty();
  if (selected && (can_install || (installed && (disc_state == DiscState::kMatches ||
                                                 disc_state == DiscState::kMissing)))) {
    const char* go = can_install                         ? (german_ ? "Installieren" : "Install")
                     : disc_state == DiscState::kMissing ? (german_ ? "Disc einlesen" : "Add the disc")
                                                         : (german_ ? "Starten" : "Play");
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
  // The connected controllers left of it, as on the Xbox: each pad's number and its battery
  // (three bars; a wired pad has none).
  float px = left - 26 * u;
  for (int pad = XUSER_MAX_COUNT - 1; pad >= 0; --pad) {
    if (!g_pad_connected[pad]) continue;
    const int level = PadBattery(DWORD(pad));
    const float w = (level >= 0 ? 52 : 32) * u, h = 30 * u;
    const ImVec2 a{px - w, c.y - h * 0.5f}, b{px, c.y + h * 0.5f};
    dl->AddRectFilled(a, b, Rgba(40, 50, 100, 230), 10 * u);
    dl->AddRect(a, b, Rgba(90, 100, 150), 10 * u, 0, 1.5f * u);
    const std::string number = std::to_string(pad + 1);
    const float nx = level >= 0 ? a.x + 10 * u : a.x + (w - TextWidth(bold_, 20 * u, number.c_str())) * 0.5f;
    Text(dl, bold_, 20 * u, {nx, c.y - 12 * u}, kWhite, number.c_str());
    for (int bar = 0; bar < 3 && level >= 0; ++bar) {
      const float bx = a.x + 26 * u + bar * 7 * u;
      const ImU32 col = bar < level ? (level == 1 ? Rgba(240, 110, 110) : Rgba(110, 220, 110)) : Rgba(70, 80, 120);
      dl->AddRectFilled({bx, c.y - 8 * u}, {bx + 5 * u, c.y + 8 * u}, col, 1.5f * u);
    }
    px = a.x - 8 * u;
  }
  // DirectInput pads (pad_state.h) have no XInput slot: a small pad shape for each.
  for (const GenericPad& pad : g_generic_pads) {
    if (!pad.connected || pad.skip) continue;
    const float w = 40 * u, h = 30 * u;
    const ImVec2 a{px - w, c.y - h * 0.5f}, b{px, c.y + h * 0.5f};
    dl->AddRectFilled(a, b, Rgba(40, 50, 100, 230), 10 * u);
    dl->AddRect(a, b, Rgba(90, 100, 150), 10 * u, 0, 1.5f * u);
    const ImVec2 m{(a.x + b.x) * 0.5f, c.y};
    dl->AddRectFilled({m.x - 12 * u, m.y - 6 * u}, {m.x + 12 * u, m.y + 6 * u}, kWhite, 6 * u);
    dl->AddCircleFilled({m.x - 6 * u, m.y}, 2.5f * u, Rgba(40, 50, 100));
    dl->AddCircleFilled({m.x + 6 * u, m.y}, 2.5f * u, Rgba(40, 50, 100));
    px = a.x - 8 * u;
  }
  return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

bool LauncherUi::Draw(float width, float height) {
  // The screen this frame will draw, in DrawScreens' order.
  const char* window = starting_                       ? "starting"
                       : setup_.is_open()              ? "setup"
                       : confirm_quit_                 ? "quit"
                       : ask_update_ >= 0              ? "update"
                       : browser_.is_open()             ? "files"
                       : profiles_screen_.is_open()     ? "profiles"
                       : achievements_screen_.is_open() ? "achievements"
                       : figures_screen_.is_open()      ? "figures"
                       : hub_screen_.is_open() || settings_.is_open() ? "settings"
                                                                      : "launcher";
  const double now = ImGui::GetTime();
  // A message set when a game ended (a negative time: its length) counts from this frame on: no
  // frames were drawn while the game ran, and ImGui's clock jumps ahead on the first one after it.
  if (error_until_ < 0) error_until_ = now - error_until_;
  if (platinum_until_ < 0) platinum_until_ = now - platinum_until_;
  if (window != shown_window_) {
    shown_window_ = window;
    shown_since_ = now;
  }
  const bool keep = DrawScreens(width, height);
  // The chosen game's title music, once it is there (UpdateTitleMusicExport fetches it).
  UpdateTitleMusicExport();
  UpdatePictureExport();
  UpdateInstall();
  UpdateImport();
  {
    std::filesystem::path wanted;
    if (hub_.title_music && selected_ < found_.size() && !found_[selected_].exe.empty()) {
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

// A game installed with its disc but without its pictures (achievements.txt is the last thing the
// export writes): the game writes them, hidden and muted, one game at a time and after the title
// music (games.h StartPictureExport). The library, the cards and the achievements then have them.
void LauncherUi::UpdatePictureExport() {
  if (pictures_tried_.size() != found_.size()) pictures_tried_.assign(found_.size(), false);
  if (pictures_process_) {
    const DWORD wait = WaitForSingleObject(pictures_process_, 0);
    if (wait != WAIT_OBJECT_0 && ImGui::GetTime() - pictures_since_ < 200.0) return;
    if (wait != WAIT_OBJECT_0) TerminateProcess(pictures_process_, 3);
    CloseHandle(pictures_process_);
    pictures_process_ = nullptr;
    hero_checked_ = -100.0;  // the cards' hero pictures looked for again
    CountAchievements();
  }
  if (export_process_ || starting_ || install_process_ || import_.running()) return;
  for (size_t i = 0; i < found_.size(); ++i) {
    const Found& f = found_[i];
    if (pictures_tried_[i] || f.exe.empty() || f.disc.load() != DiscState::kMatches) continue;
    std::error_code ec;
    if (std::filesystem::is_regular_file(PicturesDir(i) / "achievements.txt", ec)) continue;
    pictures_tried_[i] = true;
    if (StartPictureExport(i, f, &pictures_process_)) {
      pictures_game_ = i;
      pictures_since_ = ImGui::GetTime();
    } else {
      pictures_process_ = nullptr;
    }
    return;
  }
}

// The chosen game's title music, written from its disc by the game when it has none yet: once per
// launch and game, hidden, a second or two (games.h StartTitleMusicExport). A build from before
// the export can't (it plays none then until it ran once).
void LauncherUi::UpdateTitleMusicExport() {
  if (export_tried_.size() != found_.size()) export_tried_.assign(found_.size(), false);
  if (export_process_) {
    const DWORD wait = WaitForSingleObject(export_process_, 0);
    if (wait != WAIT_OBJECT_0 && ImGui::GetTime() - export_since_ < 30.0) return;
    if (wait != WAIT_OBJECT_0) TerminateProcess(export_process_, 3);
    CloseHandle(export_process_);
    export_process_ = nullptr;
  }
  if (!hub_.title_music || selected_ >= found_.size() || export_tried_[selected_]) return;
  const Found& f = found_[selected_];
  if (f.exe.empty() || f.disc.load() != DiscState::kMatches) return;
  std::error_code ec;
  if (std::filesystem::is_regular_file(TitleMusicFile(selected_), ec)) return;
  export_tried_[selected_] = true;
  if (StartTitleMusicExport(selected_, f, &export_process_)) {
    export_game_ = selected_;
    export_since_ = ImGui::GetTime();
  } else {
    export_process_ = nullptr;
  }
}

// While a game starts: its background and name, until its window is up (main.cpp hides the
// launcher then), so the desktop doesn't show for the seconds before.
bool LauncherUi::DrawStarting(float width, float height) {
  const float u = std::min(width / 1280.0f, height / 720.0f);
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("starting", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  DrawBackdrop(dl, width, height,
               starting_game_ < background_art_.size() ? background_art_[starting_game_] : std::filesystem::path());
  const Game& game = AllGames()[std::min(starting_game_, AllGames().size() - 1)];
  const std::string title = german_ ? std::string(game.name_de) + " startet ..." : std::string(game.name_en) + " is starting ...";
  Text(dl, bold_, 44 * u, {(width - TextWidth(bold_, 44 * u, title.c_str())) * 0.5f, height * 0.42f}, kGold,
       title.c_str());
  // A ring of dots turning, so it doesn't look stuck.
  const ImVec2 c{width * 0.5f, height * 0.42f + 110 * u};
  const double t = ImGui::GetTime() - starting_since_;
  for (int i = 0; i < 12; ++i) {
    const float a = float(i) / 12.0f * 6.2832f;
    const float lit = float(std::fmod(t * 1.5 - i / 12.0, 1.0));
    dl->AddCircleFilled({c.x + std::cos(a) * 26 * u, c.y + std::sin(a) * 26 * u}, 5 * u,
                        Rgba(255, 210, 90, int(60 + 195 * (1.0f - lit))));
  }
  ImGui::End();
  return true;
}

// "Beenden?": A / Enter quits, B / Esc goes back to the start screen; the mouse can click either.
bool LauncherUi::DrawQuitConfirm(float width, float height) {
  const float u = std::min(width / 1280.0f, height / 720.0f);
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("quit", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  DrawBackdrop(dl, width, height,
               selected_ < background_art_.size() ? background_art_[selected_] : std::filesystem::path());
  const float bw = 560 * u, bh = 210 * u;
  const ImVec2 a{(width - bw) * 0.5f, (height - bh) * 0.5f}, b{a.x + bw, a.y + bh};
  dl->AddRectFilled(a, b, Rgba(16, 22, 60, 245), 24 * u);
  dl->AddRect(a, b, kGold, 24 * u, 0, 3 * u);
  const char* title = german_ ? "Core of Light beenden?" : "Quit Core of Light?";
  Text(dl, bold_, 34 * u, {(width - TextWidth(bold_, 34 * u, title)) * 0.5f, a.y + 40 * u}, kGold, title);
  // The two answers as buttons with their pad button.
  const ImGuiIO& io = ImGui::GetIO();
  auto button = [&](float x, const char* pad, ImU32 col, const char* label) {
    const ImVec2 p{x, a.y + 120 * u}, q{x + 220 * u, a.y + 172 * u};
    const bool over = io.MousePos.x >= p.x && io.MousePos.x < q.x && io.MousePos.y >= p.y && io.MousePos.y < q.y;
    dl->AddRectFilled(p, q, over ? Rgba(70, 80, 150, 245) : Rgba(30, 40, 90, 240), 16 * u);
    const float w = TextWidth(bold_, 24 * u, label) + 2 * 0.62f * 24 * u + 24 * u * 0.4f;
    Hint(dl, bold_, 24 * u, {p.x + (220 * u - w) * 0.5f, p.y + 12 * u}, pad, col, label);
    return over && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  };
  const bool yes = button(a.x + 50 * u, "A", Rgba(70, 170, 70), german_ ? "Beenden" : "Quit");
  const bool no = button(b.x - 270 * u, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
  ImGui::End();
  if (yes || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
    return false;
  }
  if (no || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    confirm_quit_ = false;
  }
  return true;
}

bool LauncherUi::UpdateAvailable(size_t index) const {
  if (index >= installed_versions_.size() || installed_versions_[index].empty() || index >= setups_.size() ||
      setups_[index].empty()) {
    return false;
  }
  return VersionLess(installed_versions_[index], SetupVersion(AllGames()[index], setups_[index]));
}

// "Update for <game>?": A runs the newer setup over the installed game (silently, as an install;
// saves, figures, the disc and the settings file stay), B plays the installed one for now.
bool LauncherUi::DrawUpdateConfirm(float width, float height) {
  const size_t index = size_t(ask_update_);
  const float u = std::min(width / 1280.0f, height / 720.0f);
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("update", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  DrawBackdrop(dl, width, height, index < background_art_.size() ? background_art_[index] : std::filesystem::path());
  const float bw = 640 * u, bh = 260 * u;
  const ImVec2 a{(width - bw) * 0.5f, (height - bh) * 0.5f}, b{a.x + bw, a.y + bh};
  dl->AddRectFilled(a, b, Rgba(16, 22, 60, 245), 24 * u);
  dl->AddRect(a, b, kGold, 24 * u, 0, 3 * u);
  const Game& game = AllGames()[index];
  const std::string title = (german_ ? "Update f\xC3\xBCr " : "Update for ") + std::string(german_ ? game.name_de : game.name_en);
  Text(dl, bold_, 32 * u, {(width - TextWidth(bold_, 32 * u, title.c_str())) * 0.5f, a.y + 30 * u}, kGold, title.c_str());
  const std::string versions = (german_ ? "Installiert: " : "Installed: ") + installed_versions_[index] +
                               (german_ ? "   \xC2\xB7   Neu: " : "   \xC2\xB7   New: ") +
                               SetupVersion(game, setups_[index]);
  Text(dl, regular_, 21 * u, {(width - TextWidth(regular_, 21 * u, versions.c_str())) * 0.5f, a.y + 84 * u}, kWhite,
       versions.c_str());
  const char* keeps = german_ ? "Spielst\xC3\xA4nde, Figuren, Disc und Einstellungen bleiben."
                              : "Saves, figures, the disc and the settings stay.";
  Text(dl, regular_, 18 * u, {(width - TextWidth(regular_, 18 * u, keeps)) * 0.5f, a.y + 118 * u}, kDim, keeps);
  const ImGuiIO& io = ImGui::GetIO();
  auto button = [&](float x, const char* pad, ImU32 col, const char* label) {
    const ImVec2 p{x, a.y + 170 * u}, q{x + 260 * u, a.y + 222 * u};
    const bool over = io.MousePos.x >= p.x && io.MousePos.x < q.x && io.MousePos.y >= p.y && io.MousePos.y < q.y;
    dl->AddRectFilled(p, q, over ? Rgba(70, 80, 150, 245) : Rgba(30, 40, 90, 240), 16 * u);
    const float w = TextWidth(bold_, 24 * u, label) + 2 * 0.62f * 24 * u + 24 * u * 0.4f;
    Hint(dl, bold_, 24 * u, {p.x + (260 * u - w) * 0.5f, p.y + 12 * u}, pad, col, label);
    return over && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  };
  const bool yes = button(a.x + 50 * u, "A", Rgba(70, 170, 70), german_ ? "Aktualisieren" : "Update");
  const bool no = button(b.x - 310 * u, "B", Rgba(200, 60, 60), german_ ? "Erst spielen" : "Play first");
  ImGui::End();
  if (yes || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
    ask_update_ = -1;
    if (StartGameInstall(index, setups_[index], german_, &install_process_, true)) {
      install_game_ = index;
      install_since_ = ImGui::GetTime();
      install_is_update_ = true;
    } else {
      install_process_ = nullptr;
      error_ = german_ ? L"Der Installer lie\x00DF sich nicht starten" : L"Could not start the installer";
      error_until_ = ImGui::GetTime() + 6.0;
    }
  } else if (no || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    ask_update_ = -1;
    if (index < update_put_off_.size()) update_put_off_[index] = true;
    Start(index);
  }
  return true;
}

bool LauncherUi::DrawScreens(float width, float height) {
  const ImGuiIO& io = ImGui::GetIO();
  if (starting_) return DrawStarting(width, height);
  if (setup_.is_open()) {
    // X on its disc page: the file browser for an ISO the search didn't find, over the setup.
    if (browser_.is_open()) {
      browser_.Draw(width, height);
      if (const std::filesystem::path chosen = browser_.TakeChosen(); !chosen.empty()) setup_.AddIso(chosen);
      return true;
    }
    setup_.Draw(width, height, {&found_, &setups_, install_process_ != nullptr, &import_});
    // It says what comes next; the launcher's own install and disc import do it.
    const SetupScreen::Job job = setup_.TakeJob();
    if (job.kind == SetupScreen::Job::Kind::kInstall && !install_process_ && job.game < setups_.size()) {
      if (StartGameInstall(job.game, setups_[job.game], german_, &install_process_)) {
        install_game_ = job.game;
        install_since_ = ImGui::GetTime();
      } else {
        install_process_ = nullptr;
      }
    } else if (job.kind == SetupScreen::Job::Kind::kImport && !import_.running()) {
      import_.Start(job.iso, found_, german_);
    } else if (job.kind == SetupScreen::Job::Kind::kCancel) {
      import_.Cancel();
    } else if (job.kind == SetupScreen::Job::Kind::kPickIso) {
      browser_.Open();
    }
    if (const std::string language = setup_.TakeLanguage(); !language.empty()) {
      hub_.language = language;
      WriteGameSetting(HubSettingsFile(), "Hub", "language", "string", language);
      ApplyHubSettings();
      LoadProfiles(german_);
    }
    if (setup_.TakeFinished()) {
      hub_.setup_done = true;
      WriteGameSetting(HubSettingsFile(), "Hub", "setup_done", "bool", "true");
    }
    return true;
  }
  if (confirm_quit_) return DrawQuitConfirm(width, height);
  if (ask_update_ >= 0) return DrawUpdateConfirm(width, height);
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
                     pressed(ImGuiKey_GamepadDpadDown) || pressed(ImGuiKey_GamepadLStickUp) ||
                     pressed(ImGuiKey_GamepadLStickDown))) {
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
  // B / Escape: quit (asks first), or while a disc is being set up, stop that: the first press
  // asks, a second one within a few seconds stops it (an 8 GB unpack is easy to start by mistake).
  const bool back = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
  const bool quit = back && !import_.running();
  if (back && import_.running()) {
    if (ImGui::GetTime() < cancel_import_until_) {
      import_.Cancel();
      cancel_import_until_ = 0;
    } else {
      cancel_import_until_ = ImGui::GetTime() + 4.0;
    }
  }
  if (!import_.running()) cancel_import_until_ = 0;

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
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60),
            import_.running() ? (german_ ? "Einlesen abbrechen" : "Stop") : (german_ ? "Beenden" : "Quit"));
  // The disc being set up: what happens, and how far the unpacking is.
  const DiscImport::Phase phase = import_.phase();  // UpdateImport follows it
  if (import_.running() || (phase != DiscImport::Phase::kIdle && ImGui::GetTime() < import_shown_until_)) {
    const float bx = width * 0.5f, by = height - 100 * u, bw = width * 0.5f - 48 * u;
    std::string text = import_.message();
    if (ImGui::GetTime() < cancel_import_until_) {
      text = german_ ? "Noch einmal B: das Einlesen abbrechen (die Disc ist dann nicht eingerichtet)."
                     : "B again: stop setting up the disc (it isn't set up then).";
    }
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
  // A message (a game that didn't start, a disc that isn't set up): above the hints, where the
  // figure line is (the hints fill the row's width).
  if (!error_.empty() && ImGui::GetTime() < error_until_) {
    const std::string text = Utf8(error_);
    Text(dl, regular_, 20 * u, {48 * u, height - 124 * u}, Rgba(240, 110, 110), text.c_str(), width - 96 * u);
  }
  // A new platinum trophy: a banner at the top, fading in and out.
  if (platinum_new_ >= 0 && size_t(platinum_new_) < AllGames().size() && ImGui::GetTime() < platinum_until_) {
    const float left_s = float(platinum_until_ - ImGui::GetTime());
    const float fade = std::clamp(std::min(left_s, 12.0f - left_s) * 2.0f, 0.0f, 1.0f);
    const Game& game = AllGames()[size_t(platinum_new_)];
    const std::string title = german_ ? "PLATIN-TROPH\xC3\x84" "E!" : "PLATINUM TROPHY!";
    const std::string about = std::string(german_ ? game.name_de : game.name_en) +
                              (german_ ? ": alle Erfolge freigeschaltet" : ": every achievement unlocked");
    const float bw = std::max(TextWidth(bold_, 30 * u, title.c_str()), TextWidth(regular_, 20 * u, about.c_str())) +
                     130 * u;
    const ImVec2 a{(width - bw) * 0.5f, 26 * u}, b{a.x + bw, a.y + 92 * u};
    dl->AddRectFilled(a, b, Rgba(20, 26, 60, int(240 * fade)), 18 * u);
    dl->AddRect(a, b, Rgba(222, 232, 248, int(255 * fade)), 18 * u, 0, 2.5f * u);
    if (fade > 0.5f) {
      DrawPlatinumTrophy(dl, a.x + 52 * u, a.y + 46 * u, 62 * u, true);
      Text(dl, bold_, 30 * u, {a.x + 100 * u, a.y + 14 * u}, Rgba(222, 232, 248), title.c_str());
      Text(dl, regular_, 20 * u, {a.x + 100 * u, a.y + 54 * u}, kWhite, about.c_str());
    }
  }
  ImGui::End();
  if (quit) confirm_quit_ = true;
  return true;
}

}  // namespace coreoflight
