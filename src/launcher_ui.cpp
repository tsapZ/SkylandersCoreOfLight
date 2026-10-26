#include "launcher_ui.h"
#include "hints.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <string_view>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "nxe_art.h"
#include "pad_state.h"
#include "portal/data_folder.h"
#include "portal/figure_catalog.h"
#include "portal/figure_file.h"
#include "portal/figure_info.h"
#include "game_files.h"
#include "problem_report.h"
#include "textures.h"
#include "whats_new.h"

namespace sp = skylanders::portal;

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

// A hint: the controller button or the keyboard key (hints.h), and what it does.
float Hint(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const char* button, ImU32 col,
           const char* label, const char* key = nullptr) {
  const float x = pos.x + HintGlyph(dl, font, size, pos, button, col, key) + size * 0.4f;
  Text(dl, font, size, {x, pos.y}, kWhite, label);
  HintClickArea({pos.x - size * 0.2f, pos.y - size * 0.25f}, {x + TextWidth(font, size, label) + size * 0.3f, pos.y + size * 1.3f},
                button);  // a click on it presses the button (hints.h)
  return x + TextWidth(font, size, label) + size * 1.2f;
}

// The same inside a dialog's button, which takes its own clicks (no second press from the hint).
void ButtonHint(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const char* button, ImU32 col,
                const char* label) {
  const float x = pos.x + HintGlyph(dl, font, size, pos, button, col) + size * 0.4f;
  Text(dl, font, size, {x, pos.y}, kWhite, label);
}

// The point `d` along a rounded rectangle's border, clockwise from the end of its top left corner.
ImVec2 AlongRoundedRect(ImVec2 a, ImVec2 b, float r, float d) {
  const float w = b.x - a.x - 2 * r, h = b.y - a.y - 2 * r, q = 1.5707963f * r;
  const float total = 2 * (w + h) + 4 * q;
  d = std::fmod(d, total);
  if (d < 0) d += total;
  if (d < w) return {a.x + r + d, a.y};
  d -= w;
  if (d < q) return {b.x - r + std::sin(d / r) * r, a.y + r - std::cos(d / r) * r};
  d -= q;
  if (d < h) return {b.x, a.y + r + d};
  d -= h;
  if (d < q) return {b.x - r + std::cos(d / r) * r, b.y - r + std::sin(d / r) * r};
  d -= q;
  if (d < w) return {b.x - r - d, b.y};
  d -= w;
  if (d < q) return {a.x + r - std::sin(d / r) * r, b.y - r + std::cos(d / r) * r};
  d -= q;
  if (d < h) return {a.x, b.y - r - d};
  d -= h;
  return {a.x + r - std::cos(d / r) * r, a.y + r - std::sin(d / r) * r};
}

// A spark of light with a fading tail, `head` the position of its front along a path.
template <typename Along>
void DrawComet(ImDrawList* dl, Along along, float head, float step, int tail, float size) {
  for (int k = tail - 1; k >= 0; --k) {
    const float fade = 1.0f - float(k) / float(tail);
    dl->AddCircleFilled(along(head - float(k) * step), size * (0.35f + 0.65f * fade),
                        Rgba(255, 236, 170, int(235 * fade * fade)));
  }
  dl->AddCircleFilled(along(head), size * 2.6f, Rgba(255, 215, 110, 55));
}

}  // namespace

void LauncherUi::Init(const std::filesystem::path& launcher_dir, ImFont* regular, ImFont* bold) {
  launcher_dir_ = launcher_dir;
  regular_ = regular;
  bold_ = bold;
  // What is still in the old per-game folders goes into the one data folder first (data_move.h), so
  // the figures, saves and profiles below are read from there; the old ones are offered for deleting.
  MoveOldData();
  hub_ = LoadHubSettings();
  ApplyHubSettings();
  old_data_ = OldDataToDelete();
  ask_old_data_ = !old_data_.empty() && !hub_.old_data_kept;
  work_ = std::make_shared<UpdateWork>();
  if (!Offline()) releases_ = LoadKnownReleases();  // --offline: no release, nothing to download
  Refresh();
  if (!Offline() && UpdateCheckDue()) StartUpdateCheck(false);
  else AutoUpdateGames();  // updates the last check found and that weren't installed yet
  // The first-run setup: on a first start with no game there (a friend's PC, not a developer's).
  if (!hub_.setup_done && std::none_of(found_.begin(), found_.end(), [](const Found& f) { return !f.exe.empty(); })) {
    setup_.Open();
  }
}

void LauncherUi::ApplyHubSettings() {
  german_ = HubGerman(hub_);
  // The games' folder chosen in the setup, unless the command line or coreoflight.ini set one.
  if (!hub_.games_dir.empty() && !GamesFolderChosen()) {
    // Under Wine through Z:\var\home rather than the /home link (RealFolder).
    SetInstallDir(RealFolder(std::filesystem::path(std::u8string(hub_.games_dir.begin(), hub_.games_dir.end()))));
  }
  settings_.Init(regular_, bold_, german_);
  hub_screen_.Init(regular_, bold_, german_);
  browser_.Init(regular_, bold_, german_);
  folder_browser_.Init(regular_, bold_, german_, true);
  figures_screen_.Init(regular_, bold_, german_);
  profiles_screen_.Init(regular_, bold_, german_);
  achievements_screen_.Init(regular_, bold_, german_);
  saves_screen_.Init(regular_, bold_, german_);
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
  ApplyReleases();
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
    if (update_total_ && update) update_failed_.push_back(name);  // "Alle aktualisieren" sums it up
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
  // "Problem melden?" comes up by itself (DrawCrashReport).
  crash_code_ = code;
  crash_key_ = last_key_;
}

// A game ended with an error: what happened and the report one press away (the owner, 2026-10-23:
// "Problem melden" should come up by itself when a game crashes).
bool LauncherUi::DrawCrashReport(float width, float height) {
  const float u = std::min(width / 1280.0f, height / 720.0f);
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("crash", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  size_t index = AllGames().size();
  for (size_t i = 0; i < AllGames().size(); ++i) {
    if (crash_key_ == AllGames()[i].key) index = i;
  }
  DrawBackdrop(dl, width, height, index < background_art_.size() ? background_art_[index] : std::filesystem::path());
  const float bw = 680 * u, bh = 270 * u;
  const ImVec2 a{(width - bw) * 0.5f, (height - bh) * 0.5f}, b{a.x + bw, a.y + bh};
  dl->AddRectFilled(a, b, Rgba(16, 22, 60, 245), 24 * u);
  dl->AddRect(a, b, kGold, 24 * u, 0, 3 * u);
  const std::string name = index < AllGames().size()
                               ? std::string(german_ ? AllGames()[index].name_de : AllGames()[index].name_en)
                               : std::string(german_ ? "Das Spiel" : "The game");
  const std::string title = name + (german_ ? " wurde unerwartet beendet" : " ended unexpectedly");
  Text(dl, bold_, 30 * u, {(width - TextWidth(bold_, 30 * u, title.c_str())) * 0.5f, a.y + 30 * u}, kGold, title.c_str());
  char code[48];
  std::snprintf(code, sizeof(code), german_ ? "Fehlercode 0x%08lX" : "Error code 0x%08lX", crash_code_);
  Text(dl, regular_, 20 * u, {(width - TextWidth(regular_, 20 * u, code)) * 0.5f, a.y + 80 * u}, kDim, code);
  const char* line = german_ ? "Problem melden legt die Logs als Textdatei auf den Desktop, ohne deinen Benutzernamen."
                             : "Report a problem puts the logs on the desktop as a text file, without your user name.";
  Text(dl, regular_, 19 * u, {(width - TextWidth(regular_, 19 * u, line)) * 0.5f, a.y + 118 * u}, kWhite, line);
  const ImGuiIO& io = ImGui::GetIO();
  auto button = [&](float x, const char* pad, ImU32 col, const char* label) {
    const ImVec2 p{x, a.y + 180 * u}, q{x + 270 * u, a.y + 232 * u};
    const bool over = io.MousePos.x >= p.x && io.MousePos.x < q.x && io.MousePos.y >= p.y && io.MousePos.y < q.y;
    dl->AddRectFilled(p, q, over ? Rgba(70, 80, 150, 245) : Rgba(30, 40, 90, 240), 16 * u);
    const float w = TextWidth(bold_, 24 * u, label) + 2 * 0.62f * 24 * u + 24 * u * 0.4f;
    ButtonHint(dl, bold_, 24 * u, {p.x + (270 * u - w) * 0.5f, p.y + 12 * u}, pad, col, label);
    return over && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  };
  const bool yes = button(a.x + 50 * u, "A", Rgba(70, 170, 70), german_ ? "Problem melden" : "Report it");
  const bool no = button(b.x - 320 * u, "B", Rgba(200, 60, 60), german_ ? "Schlie\xC3\x9F" "en" : "Close");
  ImGui::End();
  if (yes || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
    crash_code_ = 0;
    std::string error;
    const std::filesystem::path report = WriteProblemReport(found_, launcher_dir_, german_, error);
    if (report.empty()) {
      error_ = Wide(error);
    } else {
      error_ = (german_ ? L"Auf dem Desktop: " : L"On the desktop: ") + report.filename().wstring();
      const std::wstring select = L"/select,\"" + report.wstring() + L"\"";
      ShellExecuteW(nullptr, L"open", L"explorer.exe", select.c_str(), nullptr, SW_SHOWNORMAL);
    }
    error_until_ = ImGui::GetTime() + 8.0;
  } else if (no || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    crash_code_ = 0;
  }
  return true;
}

void LauncherUi::ToggleFullscreen() {
  hub_.fullscreen = !fullscreen();
  fullscreen_forced_ = false;
  WriteGameSetting(HubSettingsFile(), "Hub", "fullscreen", "bool", hub_.fullscreen ? "true" : "false");
}

void LauncherUi::GameEnded() {
  if (running_key_.empty()) return;
  AddPlaySeconds(running_profile_, running_key_, (GetTickCount64() - running_since_) / 1000);
  // And the saves as the session left them.
  if (hub_.auto_backup && running_game_ < found_.size()) {
    AutoBackup(running_profile_, AllGames()[running_game_], found_[running_game_].exe);
  }
  last_key_ = running_key_;
  running_key_.clear();
}

void LauncherUi::GameRunning() {
  if (running_key_.empty() || !hub_.auto_backup || running_game_ >= found_.size()) return;
  const uint64_t now = GetTickCount64();
  if (now - last_auto_backup_ < 60ull * 60 * 1000) return;
  last_auto_backup_ = now;
  AutoBackup(running_profile_, AllGames()[running_game_], found_[running_game_].exe);
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
    if (Downloading()) {
      say(L"Ein Spiel wird gerade heruntergeladen", L"A game is being downloaded");
      return;
    }
    if (index < setups_.size() && !setups_[index].empty()) {
      if (!BeginInstall(index, false)) say(L"Der Installer lie\x00DF sich nicht starten", L"Could not start the installer");
      return;
    }
    say(L"Dieses Spiel ist nicht installiert (sein Installer aus seinem Projekt)",
        L"This game isn't installed (its installer from its project)");
    return;
  }
  // Its update running or queued ("Alle aktualisieren"): not while its files are being replaced.
  if (BeingUpdated(index)) {
    say(L"Dieses Spiel wird gerade aktualisiert", L"This game is being updated");
    return;
  }
  if (index == ImaginatorsIndex() && language_import_.running()) {
    say(L"Die Sprache von Imaginators wird gerade eingespielt", L"Imaginators' language is being put in");
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
  // The saves as they are before the game touches them (unchanged ones aren't copied again).
  if (hub_.auto_backup) AutoBackup(ActiveProfile(), AllGames()[index], found_[index].exe);
  if (StartGame(found_[index], error, &process)) {
    started_ = process;
    starting_ = true;
    starting_game_ = index;
    starting_since_ = ImGui::GetTime();
    running_key_ = AllGames()[index].key;
    running_profile_ = ActiveProfile();
    running_since_ = GetTickCount64();
    running_game_ = index;
    last_auto_backup_ = running_since_;
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
  // Two sparks running round the chosen card's border (the owner's wish for more life, 2026-10-24).
  if (selected && hub_.animations) {
    const float head = float(ImGui::GetTime()) * 260 * s;
    const float half = (w + h) - 4 * r + 3.14159265f * r;  // half the border
    auto along = [&](float d) { return AlongRoundedRect({x, y}, {x + w, y + h}, r, d); };
    DrawComet(dl, along, head, 8 * s, 14, 4.6f * s);
    DrawComet(dl, along, head + half, 8 * s, 14, 4.6f * s);
  }
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
      // A line that wraps (a wider font, Noto Sans under Wine) pushes the next one down.
      const float height = regular_->CalcTextSizeA(21 * s, FLT_MAX, w - 44 * s, line.c_str()).y;
      ly += std::max(24 * s, height + 3 * s);
    }
  }
  // The disc's state.
  const char* state = nullptr;
  ImU32 state_col = kDim;
  const bool installing = install_process_ && install_game_ == index;
  static char downloading[64];
  std::snprintf(downloading, sizeof(downloading), german_ ? "L\xC3\xA4" "dt herunter ... %d %%" : "Downloading ... %d %%",
                int(DownloadProgress() * 100.0f));
  if (!installed) {
    state = DownloadingFor(index) ? downloading
            : installing          ? (german_ ? "Wird installiert ..." : "Installing ...")
                                  : (german_ ? "Nicht installiert" : "Not installed");
  } else if (BeingUpdated(index)) {
    // Its update: downloading, its setup running, or waiting its turn ("Alle aktualisieren").
    state = DownloadingFor(index) ? downloading
            : installing          ? (german_ ? "Wird aktualisiert ..." : "Updating ...")
                                  : (german_ ? "Update wartet ..." : "Update waiting ...");
    state_col = kGold;
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
  if (selected && !BeingUpdated(index) &&
      (can_install || (installed && (disc_state == DiscState::kMatches || disc_state == DiscState::kMissing)))) {
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

void LauncherUi::OpenHubSettings(int page) {
  // The vibration rows show this launcher's value, or while it has none the first game's (its F4
  // may have set one), else the games' default of 100 %.
  HubSettings shown = hub_;
  for (int p = 0; p < 2; ++p) {
    if (shown.pad_rumble[p] >= 0) continue;
    for (const Found& f : found_) {
      auto it = f.settings.find(p == 0 ? "pad_rumble_p1" : "pad_rumble_p2");
      if (f.exe.empty() || it == f.settings.end()) continue;
      shown.pad_rumble[p] = std::clamp(std::atoi(it->second.c_str()), 0, 100);
      break;
    }
  }
  hub_loader_.SetReady(HubSettingsPages(shown));
  // What's new in this version, at the bottom right (#44).
  std::vector<std::pair<std::string, std::string>> news;
  for (const WhatsNewLine& line : kWhatsNew) news.emplace_back(line.en, line.de);
  hub_screen_.SetNote({"New in Core of Light " COREOFLIGHT_VERSION, "Neu in Core of Light " COREOFLIGHT_VERSION},
                      std::move(news));
  hub_screen_.Open(german_ ? "Einstellungen des Hubs" : "Launcher settings", &hub_loader_, {}, page);
  for (int p = 0; p < 2; ++p) rumble_seen_[p].clear();
  language_label_pending_ = true;
}

size_t LauncherUi::ImaginatorsIndex() const {
  size_t im = 0;
  while (im < AllGames().size() && std::string_view(AllGames()[im].name_en) != "Imaginators") ++im;
  return im;
}

bool LauncherUi::ImaginatorsReady() const {
  const size_t im = ImaginatorsIndex();
  std::error_code ec;
  return im < found_.size() && !found_[im].exe.empty() && !found_[im].rom.empty() &&
         std::filesystem::exists(found_[im].rom / "archives" / "permanent_xenon.pak", ec);
}

bool LauncherUi::LanguageLabel() {
  if (!hub_screen_.is_open()) return true;
  std::string en = "Imaginators isn't set up", de = "Imaginators ist nicht eingerichtet";
  if (ImaginatorsReady()) {
    const std::string now = ImportedLanguage(found_[ImaginatorsIndex()].rom);
    en = now.empty() ? "The disc's own (Spanish, Italian)" : "Changed (by a tool)";
    de = now.empty() ? "Wie die Disc (Spanisch, Italienisch)" : "Ge\xC3\xA4ndert (mit einem Werkzeug)";
    size_t count = 0;
    const LanguageImport::Language* languages = LanguageImport::Languages(&count);
    for (size_t i = 0; i < count; ++i) {
      if (now != languages[i].code) continue;
      en = languages[i].name_en;
      de = languages[i].name_de;
    }
  }
  return hub_screen_.SetActionLabel("im_language", en, de);
}

void LauncherUi::AskLanguage(const std::filesystem::path& source) {
  if (language_import_.running()) return;  // its note shows how far it is
  if (!ImaginatorsReady()) {
    ShowToast(german_ ? "Imaginators braucht zuerst seine Xbox-360-Disc, dann die PS3-Disc f\xC3\xBCr die Sprache."
                      : "Imaginators needs its Xbox 360 disc first, then the PS3 disc for the language.",
              10.0);
    return;
  }
  language_source_ = source;
  language_now_ = ImportedLanguage(found_[ImaginatorsIndex()].rom);
  language_row_ = 0;
  ask_language_ = true;
}

// A disc picked in the disc browser that is a PS3 disc, or one picked for the language chosen in
// the settings: with that language the import starts, else the question which one.
void LauncherUi::TakeLanguageDisc(const std::filesystem::path& chosen) {
  const bool for_language = std::exchange(browse_for_language_, false);
  browser_.SetForLanguage(false);
  if (!IsImaginatorsPs3Disc(chosen)) {
    ShowToast(german_ ? "Das ist keine PS3-Disc von Imaginators." : "That isn't a PS3 disc of Imaginators.", 8.0);
    return;
  }
  if (for_language && !language_wanted_.empty() && ImaginatorsReady() && !language_import_.running()) {
    language_import_.Start(chosen, found_[ImaginatorsIndex()].rom, language_wanted_, german_);
    return;
  }
  AskLanguage(chosen);
}

// "Imaginators: language": English, Deutsch, Francais, and the disc's own ones back when another
// is in; up / down and A, or a click; B back.
bool LauncherUi::DrawLanguage(float width, float height) {
  const float u = std::min(width / 1280.0f, height / 720.0f);
  size_t count = 0;
  const LanguageImport::Language* languages = LanguageImport::Languages(&count);
  const int rows = int(count) + (language_now_.empty() ? 0 : 1);
  language_row_ = std::clamp(language_row_, 0, rows - 1);
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("language", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const size_t im = ImaginatorsIndex();
  DrawBackdrop(dl, width, height, im < background_art_.size() ? background_art_[im] : std::filesystem::path());
  const float row_h = 54 * u;
  const float bw = 800 * u, bh = 236 * u + float(rows) * row_h;
  const ImVec2 a{(width - bw) * 0.5f, (height - bh) * 0.5f}, b{a.x + bw, a.y + bh};
  dl->AddRectFilled(a, b, Rgba(16, 22, 60, 245), 24 * u);
  dl->AddRect(a, b, kGold, 24 * u, 0, 3 * u);
  const char* title = german_ ? "Imaginators: Sprache" : "Imaginators: language";
  Text(dl, bold_, 32 * u, {(width - TextWidth(bold_, 32 * u, title)) * 0.5f, a.y + 26 * u}, kGold, title);
  const char* line = language_source_.empty()
                         ? (german_ ? "Welche Sprache soll Imaginators sprechen? Danach w\xC3\xA4hlst du deine PS3-Disc "
                                      "des Spiels (.iso oder entpackter Ordner)."
                                    : "Which language should Imaginators speak? Then you pick your PS3 disc of the "
                                      "game (.iso or unpacked folder).")
                         : (german_ ? "Deine PS3-Disc von Imaginators hat Texte, Stimmen und Filme in mehr Sprachen. "
                                      "Welche soll Imaginators sprechen?"
                                    : "Your PS3 disc of Imaginators has texts, voices and movies in more languages. "
                                      "Which one should Imaginators speak?");
  Text(dl, regular_, 21 * u, {a.x + 40 * u, a.y + 78 * u}, kWhite, line, bw - 80 * u);
  const ImGuiIO& io = ImGui::GetIO();
  int clicked = -1;
  for (int r = 0; r < rows; ++r) {
    const bool own = r == int(count);  // the disc's own languages back
    const ImVec2 p{a.x + 40 * u, a.y + 150 * u + float(r) * row_h}, q{b.x - 40 * u, p.y + row_h - 8 * u};
    const bool over = io.MousePos.x >= p.x && io.MousePos.x < q.x && io.MousePos.y >= p.y && io.MousePos.y < q.y;
    if (over && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) language_row_ = r;
    const bool chosen = r == language_row_;
    dl->AddRectFilled(p, q, chosen ? Rgba(70, 80, 150, 245) : Rgba(30, 40, 90, 240), 14 * u);
    if (chosen) dl->AddRect(p, q, kGold, 14 * u, 0, 2 * u);
    const std::string label =
        own ? (german_ ? "Espa\xC3\xB1ol / Italiano (wie die Disc)" : "Espa\xC3\xB1ol / Italiano (the disc's own)")
            : std::string(languages[r].own);
    Text(dl, bold_, 24 * u, {p.x + 24 * u, p.y + 9 * u}, kWhite, label.c_str());
    if (!own && language_now_ == languages[r].code) {
      const char* now = german_ ? "jetzt" : "now";
      Text(dl, regular_, 20 * u, {q.x - 24 * u - TextWidth(regular_, 20 * u, now), p.y + 12 * u}, kGold, now);
    }
    if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) clicked = r;
  }
  const char* note = german_ ? "Etwa eine Minute. Spielst\xC3\xA4nde bleiben; zur\xC3\xBC" "ck geht es jederzeit unter "
                               "Einstellungen des Hubs > Imaginators-Sprache."
                             : "About a minute. Your saves stay; you can go back any time in Launcher settings > "
                               "Imaginators language.";
  Text(dl, regular_, 18 * u, {a.x + 40 * u, b.y - 74 * u}, kDim, note, bw - 80 * u);
  ButtonHint(dl, bold_, 22 * u, {a.x + 40 * u, b.y - 42 * u}, "A", Rgba(70, 170, 70), german_ ? "W\xC3\xA4hlen" : "Choose");
  ButtonHint(dl, bold_, 22 * u, {a.x + 250 * u, b.y - 42 * u}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
  ImGui::End();
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickUp) ||
      ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
    language_row_ = (language_row_ + rows - 1) % rows;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickDown) ||
      ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
    language_row_ = (language_row_ + 1) % rows;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    ask_language_ = false;
    return true;
  }
  if (clicked >= 0) language_row_ = clicked;
  if (clicked < 0 && !ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) && !ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
    return true;
  }
  ask_language_ = false;
  if (!ImaginatorsReady() || language_import_.running()) return true;
  const std::filesystem::path rom = found_[im].rom;
  if (language_row_ == int(count)) {
    language_import_.StartRestore(rom, german_);
    return true;
  }
  const std::string code = languages[language_row_].code;
  if (code == language_now_) {
    ShowToast(german_ ? std::string("Imaginators spricht schon ") + languages[language_row_].name_de + "."
                      : std::string("Imaginators speaks ") + languages[language_row_].name_en + " already.");
    return true;
  }
  if (!language_source_.empty()) {
    language_import_.Start(language_source_, rom, code, german_);
  } else {
    language_wanted_ = code;
    browse_for_language_ = true;
    browser_.SetForLanguage(true);
    browser_.Open();  // TakeLanguageDisc takes the disc
  }
  return true;
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
  if (hovered || top_focus_ == 1) {
    dl->AddRectFilled({left - 14 * u, c.y - radius - 8 * u}, {right + 8 * u, c.y + radius + 8 * u},
                      Rgba(255, 210, 90, 30), radius);
  }
  if (top_focus_ == 1) {  // chosen with the pad
    dl->AddRect({left - 14 * u, c.y - radius - 8 * u}, {right + 8 * u, c.y + radius + 8 * u}, kGold, radius, 0,
                2.5f * u);
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
  // Light round the profile's border (the owner's wish, 2026-10-24: little sparks along it): a glow
  // that breathes, three sparks circling at their own pace and motes drifting off. Off with the
  // "Animationen" setting.
  if (hub_.animations) {
    const float t = float(ImGui::GetTime());
    const float breathe = 0.5f + 0.5f * std::sin(t * 1.6f);
    dl->AddCircle(c, radius + 3 * u, Rgba(255, 210, 90, int(35 + 45 * breathe)), 48, 4.0f * u);
    // Only particles here (the owner, #26: the three circling sparks were too much); the chosen
    // card keeps its sparks.
    for (int i = 0; i < 10; ++i) {
      float phase = t * 0.45f + float(i) * 0.618034f;
      phase -= std::floor(phase);
      const float a = float(i) * 2.399963f + t * 0.25f;
      const float d = radius + 4 * u + phase * 18 * u;
      dl->AddCircleFilled({c.x + std::cos(a) * d, c.y + std::sin(a) * d}, 1.7f * u,
                          Rgba(255, 230, 160, int(200 * (1.0f - phase) * std::min(1.0f, phase * 5.0f))));
    }
  }
  // The connected controllers left of it, as on the Xbox: each pad's number and its battery
  // (three bars; a wired pad has none). A click on one opens the controller settings (#35).
  pad_clicked_ = false;
  auto pad_hover = [&](ImVec2 a, ImVec2 b) {
    if (!ImGui::IsMouseHoveringRect(a, b)) return;
    dl->AddRectFilled({a.x - 4 * u, a.y - 4 * u}, {b.x + 4 * u, b.y + 4 * u}, Rgba(255, 210, 90, 40), 13 * u);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) pad_clicked_ = true;
  };
  float px = left - 26 * u;
  for (int pad = XUSER_MAX_COUNT - 1; pad >= 0; --pad) {
    if (!g_pad_connected[pad]) continue;
    const int level = PadBattery(DWORD(pad));
    const float w = (level >= 0 ? 52 : 32) * u, h = 30 * u;
    const ImVec2 a{px - w, c.y - h * 0.5f}, b{px, c.y + h * 0.5f};
    pad_hover(a, b);
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
    pad_hover(a, b);
    dl->AddRectFilled(a, b, Rgba(40, 50, 100, 230), 10 * u);
    dl->AddRect(a, b, Rgba(90, 100, 150), 10 * u, 0, 1.5f * u);
    const ImVec2 m{(a.x + b.x) * 0.5f, c.y};
    dl->AddRectFilled({m.x - 12 * u, m.y - 6 * u}, {m.x + 12 * u, m.y + 6 * u}, kWhite, 6 * u);
    dl->AddCircleFilled({m.x - 6 * u, m.y}, 2.5f * u, Rgba(40, 50, 100));
    dl->AddCircleFilled({m.x + 6 * u, m.y}, 2.5f * u, Rgba(40, 50, 100));
    px = a.x - 8 * u;
  }
  // The controllers chosen with the pad: a gold frame round all of them.
  if (top_focus_ == 0 && px < left - 26 * u) {
    dl->AddRect({px + 2 * u, c.y - 21 * u}, {left - 20 * u, c.y + 21 * u}, kGold, 14 * u, 0, 2.5f * u);
  }
  // A real USB portal left of them (#42): a small portal, lit while the chosen game uses it (its
  // Portal setting "Both" or "USB only"), grey while it plays with the virtual one. A click says
  // which portal it is and how the game gets to use it.
  if (const UsbPortalKind portal = g_usb_portal.load(); portal != UsbPortalKind::kNone) {
    std::string mode;
    if (selected_ < found_.size()) {
      if (auto it = found_[selected_].settings.find("portal_mode"); it != found_[selected_].settings.end()) {
        mode = it->second;
        std::erase(mode, '"');
      }
    }
    const bool used = mode == "hybrid" || mode == "usb";
    const float w = 46 * u, h = 30 * u;
    const ImVec2 a{px - w, c.y - h * 0.5f}, b{px, c.y + h * 0.5f};
    const bool over = ImGui::IsMouseHoveringRect(a, b);
    if (over) dl->AddRectFilled({a.x - 4 * u, a.y - 4 * u}, {b.x + 4 * u, b.y + 4 * u}, Rgba(255, 210, 90, 40), 13 * u);
    dl->AddRectFilled(a, b, Rgba(40, 50, 100, 230), 10 * u);
    dl->AddRect(a, b, Rgba(90, 100, 150), 10 * u, 0, 1.5f * u);
    // The portal: a dark base and its top, glowing in the portal's blue when in use.
    const ImVec2 m{(a.x + b.x) * 0.5f, c.y + 3 * u};
    dl->AddEllipseFilled({m.x, m.y + 4 * u}, {15 * u, 6 * u}, Rgba(20, 22, 40));
    dl->AddRectFilled({m.x - 15 * u, m.y - 1 * u}, {m.x + 15 * u, m.y + 4 * u}, Rgba(20, 22, 40));
    dl->AddEllipseFilled({m.x, m.y - 1 * u}, {15 * u, 6 * u}, used ? Rgba(70, 150, 255) : Rgba(95, 105, 135));
    dl->AddEllipse({m.x, m.y - 1 * u}, {15 * u, 6 * u}, Rgba(210, 215, 235), 0, 0, 1.5f * u);
    if (used) {
      const float pulse = 0.5f + 0.5f * std::sin(float(ImGui::GetTime()) * 2.5f);
      dl->AddEllipseFilled({m.x, m.y - 6 * u}, {9 * u, 4 * u}, Rgba(150, 210, 255, int(60 + 80 * pulse)));
    }
    if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      const std::string game = selected_ < AllGames().size()
                                   ? (german_ ? AllGames()[selected_].name_de : AllGames()[selected_].name_en)
                                   : std::string();
      std::string text = portal == UsbPortalKind::kXbox360
                             ? (german_ ? "Xbox-360-Portal am USB (noch nicht getestet). " : "Xbox 360 portal on USB (not tested yet). ")
                             : (german_ ? "Echtes Portal am USB (PS3 / Wii / Wii U). " : "Real portal on USB (PS3 / Wii / Wii U). ");
      if (used) {
        text += german_ ? game + " benutzt es." : game + " uses it.";
      } else {
        text += german_ ? game + " spielt mit dem virtuellen Portal. Zum Benutzen: Einstellungen des Spiels (Y / Tab) > Portal > \"Beides\" oder \"Nur echtes USB-Portal\"."
                        : game + " plays with the virtual portal. To use it: the game's settings (Y / Tab) > Portal > \"Both\" or \"Real USB portal only\".";
      }
      ShowToast(text, 10.0);
    }
    px = a.x - 8 * u;
  }
  badge_left_ = px;  // DrawUpdateAll keeps left of it
  return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

bool LauncherUi::Draw(float width, float height) {
  // The screen this frame will draw, in DrawScreens' order.
  const char* window = starting_                       ? "starting"
                       : hub_update_ && !hub_update_put_off_ ? "hubupdate"
                       : ask_updates_found_ && !install_process_ ? "updatesfound"
                       : folder_browser_.is_open()     ? "files"
                       : ask_language_                 ? "language"
                       : setup_.is_open()              ? "setup"
                       : ask_import_settings_          ? "importquestion"
                       : !ask_reimport_.empty()        ? "reimportquestion"
                       : confirm_quit_                 ? "quit"
                       : ask_update_ >= 0              ? "update"
                       : crash_code_ != 0              ? "crash"
                       : browser_.is_open()             ? "files"
                       : profiles_screen_.is_open()     ? "profiles"
                       : saves_screen_.is_open()        ? "saves"
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
  // What the figure import brought in (#34), over whatever screen is open.
  if (const std::string summary = figure_import_.TakeSummary(); !summary.empty()) {
    ShowToast((german_ ? "Imaginators-Figuren: " : "Imaginators figures: ") + summary, 12.0);
  }
  if (const std::string summary = language_import_.TakeSummary(); !summary.empty()) {
    ShowToast(summary, 14.0);
    language_label_pending_ = true;
  }
  // The data export (shown in Explorer) or import (#40): the launcher's settings, the profiles
  // and the games' settings read again after an import.
  if (const std::string summary = data_transfer_.TakeSummary(); !summary.empty()) {
    ShowToast(summary, 16.0);
    if (const std::filesystem::path file = data_transfer_.TakeExported(); !file.empty()) {
      const std::wstring select = L"/select,\"" + file.wstring() + L"\"";
      ShellExecuteW(nullptr, L"open", L"explorer.exe", select.c_str(), nullptr, SW_SHOWNORMAL);
    } else {
      hub_ = LoadHubSettings();
      ApplyHubSettings();
      LoadProfiles(german_);
      for (Found& f : found_) {
        if (!f.exe.empty()) f.settings = ReadIni(f.exe.parent_path() / (UserDataName(f.exe) + ".toml"));
      }
      for (SettingsLoader& loader : loaders_) loader = SettingsLoader();
    }
  }
  DrawToast(width, height, std::string_view(window) == "launcher");
  // The chosen game's title music, once it is there (UpdateTitleMusicExport fetches it).
  UpdateTitleMusicExport();
  UpdatePictureExport();
  UpdateInstall();
  UpdateReleases();
  UpdateDownload();
  UpdateQueue();
  UpdateImport();
  {
    std::filesystem::path wanted;
    // None while a game starts or runs: Start's music_.Stop() was undone by the launch screen's next
    // frame, and the music went on behind the game (a colleague's Bazzite, #20).
    if (hub_.title_music && running_key_.empty() && selected_ < found_.size() && !found_[selected_].exe.empty()) {
      wanted = TitleMusicFile(selected_);
    }
    // The volume as the launcher's settings show it while they are open, before it is saved (#24).
    int volume = hub_.music_volume;
    if (hub_screen_.is_open()) {
      if (const std::string v = hub_screen_.EditedValue("music_volume"); !v.empty()) volume = std::atoi(v.c_str());
    }
    music_.SetLoudness(float(std::clamp(volume, 0, 100)) / 100.0f);
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
    ButtonHint(dl, bold_, 24 * u, {p.x + (220 * u - w) * 0.5f, p.y + 12 * u}, pad, col, label);
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

bool LauncherUi::DrawImportQuestion(float width, float height) {
  const float u = std::min(width / 1280.0f, height / 720.0f);
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("importquestion", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  DrawBackdrop(dl, width, height, {});
  const float bw = 720 * u, bh = 290 * u;
  const ImVec2 a{(width - bw) * 0.5f, (height - bh) * 0.5f}, b{a.x + bw, a.y + bh};
  dl->AddRectFilled(a, b, Rgba(16, 22, 60, 245), 24 * u);
  dl->AddRect(a, b, kGold, 24 * u, 0, 3 * u);
  const char* title = german_ ? "Einstellungen ersetzen?" : "Replace the settings?";
  Text(dl, bold_, 34 * u, {(width - TextWidth(bold_, 34 * u, title)) * 0.5f, a.y + 30 * u}, kGold, title);
  std::string games;
  for (const std::string& g : data_transfer_.settings_differ()) games += (games.empty() ? "" : ", ") + g;
  const std::string text =
      (german_ ? "Schon mit eigenen Einstellungen auf diesem PC: " : "Already with settings of their own on this PC: ") + games +
      (german_ ? ". Durch die aus der Datei ersetzen? Die jetzigen bleiben als Kopie daneben. Alles andere kommt so "
                 "oder so dazu, ohne etwas zu \xC3\xBC" "berschreiben."
               : ". Replace them with the ones from the file? The ones there now stay as a copy beside them. "
                 "Everything else comes in either way, without overwriting anything.");
  dl->AddText(regular_, 20 * u, {a.x + 36 * u, a.y + 88 * u}, kWhite, text.c_str(), nullptr, bw - 72 * u);
  const ImGuiIO& io = ImGui::GetIO();
  auto button = [&](float x, const char* pad, ImU32 col, const char* label) {
    const ImVec2 p{x, b.y - 76 * u}, q{x + 280 * u, b.y - 24 * u};
    const bool over = io.MousePos.x >= p.x && io.MousePos.x < q.x && io.MousePos.y >= p.y && io.MousePos.y < q.y;
    dl->AddRectFilled(p, q, over ? Rgba(70, 80, 150, 245) : Rgba(30, 40, 90, 240), 16 * u);
    const float w = TextWidth(bold_, 24 * u, label) + 2 * 0.62f * 24 * u + 24 * u * 0.4f;
    ButtonHint(dl, bold_, 24 * u, {p.x + (280 * u - w) * 0.5f, p.y + 12 * u}, pad, col, label);
    return over && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  };
  const bool replace = button(a.x + 50 * u, "A", Rgba(70, 170, 70), german_ ? "Ersetzen" : "Replace");
  const bool keep = button(b.x - 330 * u, "B", Rgba(200, 60, 60), german_ ? "Meine behalten" : "Keep mine");
  ImGui::End();
  if (replace || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
    ask_import_settings_ = false;
    data_transfer_.StartImport(true);
  } else if (keep || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    ask_import_settings_ = false;
    data_transfer_.StartImport(false);
  }
  return true;
}

bool LauncherUi::DrawReimportQuestion(float width, float height) {
  const float u = std::min(width / 1280.0f, height / 720.0f);
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("reimportquestion", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  DrawBackdrop(dl, width, height, {});
  const float bw = 720 * u, bh = 290 * u;
  const ImVec2 a{(width - bw) * 0.5f, (height - bh) * 0.5f}, b{a.x + bw, a.y + bh};
  dl->AddRectFilled(a, b, Rgba(16, 22, 60, 245), 24 * u);
  dl->AddRect(a, b, kGold, 24 * u, 0, 3 * u);
  const char* title = german_ ? "Spieldateien fehlen" : "Game files are missing";
  Text(dl, bold_, 34 * u, {(width - TextWidth(bold_, 34 * u, title)) * 0.5f, a.y + 30 * u}, kGold, title);
  const std::string text = ask_reimport_ +
                           (german_ ? ". Die Disc noch einmal einlesen? Spielst\xC3\xA4nde und Einstellungen bleiben."
                                    : ". Take the disc again? Saves and settings stay.");
  dl->AddText(regular_, 20 * u, {a.x + 36 * u, a.y + 88 * u}, kWhite, text.c_str(), nullptr, bw - 72 * u);
  const ImGuiIO& io = ImGui::GetIO();
  auto button = [&](float x, const char* pad, ImU32 col, const char* label) {
    const ImVec2 p{x, b.y - 76 * u}, q{x + 280 * u, b.y - 24 * u};
    const bool over = io.MousePos.x >= p.x && io.MousePos.x < q.x && io.MousePos.y >= p.y && io.MousePos.y < q.y;
    dl->AddRectFilled(p, q, over ? Rgba(70, 80, 150, 245) : Rgba(30, 40, 90, 240), 16 * u);
    const float w = TextWidth(bold_, 24 * u, label) + 2 * 0.62f * 24 * u + 24 * u * 0.4f;
    ButtonHint(dl, bold_, 24 * u, {p.x + (280 * u - w) * 0.5f, p.y + 12 * u}, pad, col, label);
    return over && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  };
  const bool again = button(a.x + 50 * u, "A", Rgba(70, 170, 70), german_ ? "Disc einlesen" : "Take the disc");
  const bool later = button(b.x - 330 * u, "B", Rgba(200, 60, 60), german_ ? "Sp\xC3\xA4ter" : "Later");
  ImGui::End();
  if (again || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
    // The disc browser (and its search) as from the start screen; the import sees the game's
    // default.xex missing or wrong and writes the disc again.
    ask_reimport_.clear();
    repair_disc_ = true;
    browse_for_language_ = false;
    browser_.SetForLanguage(false);
    browser_.Open();
  } else if (later || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    ask_reimport_.clear();
  }
  return true;
}

bool LauncherUi::UpdateAvailable(size_t index) const {
  if (index >= installed_versions_.size() || index >= setups_.size() || setups_[index].empty()) return false;
  // A game copied into the games folder has no known version (no uninstall entry): the newest
  // setup is offered once, and its entry knows the version afterwards. A developer's build (no
  // version either, not copied) is left alone.
  if (installed_versions_[index].empty()) return index < found_.size() && found_[index].copied;
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
  const std::string installed = !installed_versions_[index].empty() ? installed_versions_[index]
                                                                    : (german_ ? "unbekannt (kopiert)" : "unknown (copied)");
  const std::string versions = (german_ ? "Installiert: " : "Installed: ") + installed +
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
    ButtonHint(dl, bold_, 24 * u, {p.x + (260 * u - w) * 0.5f, p.y + 12 * u}, pad, col, label);
    return over && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  };
  const bool yes = button(a.x + 50 * u, "A", Rgba(70, 170, 70), german_ ? "Aktualisieren" : "Update");
  const bool no = button(b.x - 310 * u, "B", Rgba(200, 60, 60), german_ ? "Erst spielen" : "Play first");
  ImGui::End();
  if (yes || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
    ask_update_ = -1;
    if (!BeginInstall(index, true)) {
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

// --- Updates from GitHub Releases (updates.h) ------------------------------------------------

namespace {

// A release asset's file name as a path (the names are ASCII: <Repo>Setup-<version>.exe).
std::filesystem::path AssetPath(const std::string& name) { return std::filesystem::path(std::u8string(name.begin(), name.end())); }

}  // namespace

struct LauncherUi::UpdateWork {
  std::mutex mutex;
  std::atomic<bool> checking{false};
  bool fresh = false;              // a check ended (under `mutex`)
  std::vector<Release> releases;   // what it found (under `mutex`)
  std::atomic<int> download{0};    // 0 none, 1 running, 2 done, 3 failed
  std::atomic<float> progress{0.0f};
  std::atomic<bool> cancel{false};
  // What the download is for (UI thread only).
  size_t game = 0;
  bool update = false, hub = false;
  std::filesystem::path file;
};

void LauncherUi::StartUpdateCheck(bool manual) {
  if (Offline()) {
    if (manual) ShowToast(german_ ? "Offline gestartet (--offline): keine Updates" : "Started offline (--offline): no updates");
    return;
  }
  if (manual) manual_check_ = true;
  if (work_->checking.exchange(true)) return;
  std::vector<std::pair<std::string, std::string>> repos = {{"SkylandersCoreOfLight", "CoreOfLightSetup"}};
  for (const Game& game : AllGames()) repos.push_back({game.repo, std::string(game.repo) + "Setup"});
  std::thread([work = work_, repos] {
    std::vector<Release> found;
    for (const auto& [repo, prefix] : repos) {
      if (auto release = FetchLatestRelease(repo, prefix)) found.push_back(*release);
    }
    // Nothing at all: no network (or GitHub down); the releases known before stay.
    if (!found.empty()) SaveKnownReleases(found);
    {
      std::lock_guard lock(work->mutex);
      work->releases = std::move(found);
      work->fresh = true;
    }
    work->checking = false;
  }).detach();
}

void LauncherUi::UpdateReleases() {
  std::vector<Release> found;
  {
    std::lock_guard lock(work_->mutex);
    if (!work_->fresh) return;
    work_->fresh = false;
    found = std::move(work_->releases);
  }
  const bool manual = std::exchange(manual_check_, false);
  if (found.empty()) {
    if (manual) {
      ShowToast(german_ ? "Keine Verbindung zu GitHub: Updates lassen sich gerade nicht pr\xC3\xBC" "fen"
                        : "No connection to GitHub: updates can't be checked right now",
                8.0);
    }
    return;
  }
  releases_ = std::move(found);
  if (!install_process_ && !Downloading()) Refresh();
  if (!manual) AutoUpdateGames();
  if (manual) {
    // A newer launcher: its own question comes up (DrawHubUpdate); the games' updates: asked here.
    hub_update_put_off_ = false;
    bool games = false;
    for (size_t i = 0; i < found_.size(); ++i) {
      games = games || (!found_[i].exe.empty() && UpdateAvailable(i) && !BeingUpdated(i));
    }
    if (hub_update_) {
      ShowToast((german_ ? "Neue Version von Core of Light: " : "A new Core of Light: ") + hub_update_->version);
    } else if (games) {
      ask_updates_found_ = true;
    } else {
      ShowToast(german_ ? "Kein neues Update: alles ist auf dem neuesten Stand (Core of Light " COREOFLIGHT_VERSION ")"
                        : "No new update: everything is up to date (Core of Light " COREOFLIGHT_VERSION ")");
    }
  }
}

void LauncherUi::ShowToast(const std::string& text, double seconds) {
  toast_ = text;
  toast_until_ = ImGui::GetTime() + seconds;
}

// The note at the bottom right, over every screen: the manual check while it runs, what it found
// (ShowToast), and "Alle aktualisieren" with its progress where the start screen's button isn't.
void LauncherUi::DrawToast(float width, float height, bool start_screen) {
  const float u = std::min(width / 1280.0f, height / 720.0f);
  std::string text;
  float progress = -1.0f;  // a bar under the text when 0..1
  bool spinning = false;
  if (manual_check_ && work_->checking.load()) {
    text = german_ ? "Suche nach Updates ..." : "Looking for updates ...";
    spinning = true;
  } else if (update_total_ && !start_screen) {
    const size_t done = update_total_ - update_queue_.size();
    const size_t game = install_process_ ? install_game_ : work_->game;
    char line[200];
    std::snprintf(line, sizeof(line), german_ ? "Aktualisiere %zu von %zu: %s" : "Updating %zu of %zu: %s", done,
                  update_total_, game < AllGames().size() ? (german_ ? AllGames()[game].name_de : AllGames()[game].name_en) : "");
    text = line;
    spinning = true;
    if (Downloading()) progress = DownloadProgress();
  } else if (figure_import_.running()) {
    text = german_ ? "Imaginators pr\xC3\xBC" "ft die Figuren ..." : "Imaginators is checking the figures ...";
    spinning = true;
  } else if (language_import_.running()) {
    const std::string step = language_import_.step();
    text = (german_ ? "Imaginators-Sprache: " : "Imaginators language: ") + (step.empty() ? std::string("...") : step);
    spinning = true;
    progress = language_import_.progress();
  } else if (!toast_.empty() && ImGui::GetTime() < toast_until_) {
    text = toast_;
  }
  if (text.empty()) return;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const float size = 20 * u;
  const float pad = 18 * u, icon = spinning ? 28 * u : 0.0f;
  const float w = std::min(width - 96 * u, TextWidth(regular_, size, text.c_str()) + 2 * pad + icon);
  const float h = (progress >= 0 ? 66 : 48) * u;
  // The start screen has room above its hints; on the other screens (the settings) that is where
  // the description is, and the toast covered it (the owner, "Nach Updates suchen"): there it sits
  // at the top right, next to the screen's title.
  const ImVec2 b = start_screen ? ImVec2{width - 48 * u, height - 96 * u} : ImVec2{width - 48 * u, 30 * u + h};
  const ImVec2 a{b.x - w, b.y - h};
  dl->AddRectFilled(a, b, Rgba(20, 26, 64, 245), 16 * u);
  dl->AddRect(a, b, kGold, 16 * u, 0, 2 * u);
  if (spinning) {
    const ImVec2 c{a.x + pad + 8 * u, a.y + 24 * u};
    const float a0 = float(ImGui::GetTime()) * 5.0f;
    dl->PathArcTo(c, 8 * u, a0, a0 + 4.2f, 24);
    dl->PathStroke(kGold, 0, 2.5f * u);
  }
  Text(dl, regular_, size, {a.x + pad + icon, a.y + 12 * u}, kWhite, text.c_str(), w - 2 * pad - icon + 8 * u);
  if (progress >= 0) {
    const ImVec2 p{a.x + pad, a.y + 46 * u}, q{b.x - pad, a.y + 54 * u};
    dl->AddRectFilled(p, q, Rgba(40, 50, 100), 4 * u);
    dl->AddRectFilled(p, {p.x + (q.x - p.x) * std::clamp(progress, 0.0f, 1.0f), q.y}, kGold, 4 * u);
  }
}

// "Updates gefunden" after a manual check: each game with its versions; A downloads and installs
// them one after the other ("Alle aktualisieren"), B leaves them on the cards.
bool LauncherUi::DrawUpdatesFound(float width, float height) {
  const float u = std::min(width / 1280.0f, height / 720.0f);
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("updatesfound", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  DrawBackdrop(dl, width, height, std::filesystem::path());
  std::vector<std::string> lines;
  for (size_t i = 0; i < found_.size(); ++i) {
    if (found_[i].exe.empty() || !UpdateAvailable(i) || BeingUpdated(i)) continue;
    const Game& game = AllGames()[i];
    const std::string from = !installed_versions_[i].empty() ? installed_versions_[i] : (german_ ? "kopiert" : "copied");
    lines.push_back(std::string(german_ ? game.name_de : game.name_en) + ":  " + from + "  \xC2\xBB  " +
                    SetupVersion(game, setups_[i]));  // the fonts have Latin-1 only (no arrow)
  }
  const float bw = 680 * u, bh = (200 + 30 * float(lines.size())) * u;
  const ImVec2 a{(width - bw) * 0.5f, (height - bh) * 0.5f}, b{a.x + bw, a.y + bh};
  dl->AddRectFilled(a, b, Rgba(16, 22, 60, 245), 24 * u);
  dl->AddRect(a, b, kGold, 24 * u, 0, 3 * u);
  const char* title = german_ ? "Updates gefunden" : "Updates found";
  Text(dl, bold_, 32 * u, {(width - TextWidth(bold_, 32 * u, title)) * 0.5f, a.y + 28 * u}, kGold, title);
  float y = a.y + 80 * u;
  for (const std::string& line : lines) {
    Text(dl, regular_, 21 * u, {a.x + 60 * u, y}, kWhite, line.c_str());
    y += 30 * u;
  }
  const char* keeps = german_ ? "Spielst\xC3\xA4nde, Figuren, Discs und Einstellungen bleiben."
                              : "Saves, figures, the discs and the settings stay.";
  Text(dl, regular_, 18 * u, {(width - TextWidth(regular_, 18 * u, keeps)) * 0.5f, y + 8 * u}, kDim, keeps);
  const ImGuiIO& io = ImGui::GetIO();
  auto button = [&](float x, const char* pad, ImU32 col, const char* label) {
    const ImVec2 p{x, b.y - 74 * u}, q{x + 260 * u, b.y - 22 * u};
    const bool over = io.MousePos.x >= p.x && io.MousePos.x < q.x && io.MousePos.y >= p.y && io.MousePos.y < q.y;
    dl->AddRectFilled(p, q, over ? Rgba(70, 80, 150, 245) : Rgba(30, 40, 90, 240), 16 * u);
    const float w = TextWidth(bold_, 24 * u, label) + 2 * 0.62f * 24 * u + 24 * u * 0.4f;
    ButtonHint(dl, bold_, 24 * u, {p.x + (260 * u - w) * 0.5f, p.y + 12 * u}, pad, col, label);
    return over && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  };
  const bool yes = button(a.x + 60 * u, "A", Rgba(70, 170, 70), german_ ? "Herunterladen" : "Download");
  const bool no = button(b.x - 320 * u, "B", Rgba(200, 60, 60), german_ ? "Sp\xC3\xA4ter" : "Later");
  ImGui::End();
  if (lines.empty()) {
    ask_updates_found_ = false;
  } else if (yes || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
    ask_updates_found_ = false;
    StartUpdateAll();
  } else if (no || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    ask_updates_found_ = false;
  }
  return true;
}

void LauncherUi::ApplyReleases() {
  setup_urls_.assign(AllGames().size(), std::string());
  for (size_t i = 0; i < AllGames().size() && i < setups_.size(); ++i) {
    const Game& game = AllGames()[i];
    for (const Release& r : releases_) {
      if (r.repo != game.repo) continue;
      // The release's setup when it's newer than the one here, or there is none here.
      if (setups_[i].empty() || VersionLess(SetupVersion(game, setups_[i]), r.version)) {
        setups_[i] = DownloadsDir() / AssetPath(r.asset);
        setup_urls_[i] = r.url;
      }
    }
  }
  hub_update_.reset();
  // Only a launcher its setup installed updates itself (a developer's build is left alone).
  std::error_code ec;
  if (!std::filesystem::exists(launcher_dir_ / "unins000.exe", ec)) return;
  for (const Release& r : releases_) {
    if (r.repo == "SkylandersCoreOfLight" && VersionLess(COREOFLIGHT_VERSION, r.version)) hub_update_ = r;
  }
}

bool LauncherUi::Downloading() const { return work_ && work_->download.load() == 1; }

bool LauncherUi::DownloadingFor(size_t game) const { return Downloading() && !work_->hub && work_->game == game; }

float LauncherUi::DownloadProgress() const { return work_ ? work_->progress.load() : 0.0f; }

bool LauncherUi::UpdateQueued(size_t index) const {
  return std::find(update_queue_.begin(), update_queue_.end(), index) != update_queue_.end();
}

bool LauncherUi::BeingUpdated(size_t index) const {
  const bool installed = index < found_.size() && !found_[index].exe.empty();
  return installed && (UpdateQueued(index) || DownloadingFor(index) || (install_process_ && install_game_ == index));
}

// "Spiele automatisch aktualisieren" (#27): the games' updates go into the "Alle aktualisieren"
// queue without asking, with a note saying so. Not offline, not before the first-run setup.
void LauncherUi::AutoUpdateGames() {
  if (!hub_.auto_update_games || Offline() || setup_.is_open() || update_total_) return;
  std::string names;
  for (size_t i = 0; i < found_.size(); ++i) {
    if (found_[i].exe.empty() || !UpdateAvailable(i) || BeingUpdated(i)) continue;
    names += (names.empty() ? "" : ", ") + std::string(german_ ? AllGames()[i].name_de : AllGames()[i].name_en);
  }
  if (names.empty()) return;
  StartUpdateAll();
  ShowToast((german_ ? "Spiele werden aktualisiert: " : "Updating games: ") + names, 8.0);
}

// "Alle aktualisieren": every installed game with an update, in the cards' order.
void LauncherUi::StartUpdateAll() {
  if (update_total_) return;  // running already
  update_queue_.clear();
  update_failed_.clear();
  for (size_t i = 0; i < found_.size(); ++i) {
    if (!found_[i].exe.empty() && UpdateAvailable(i) && !BeingUpdated(i)) update_queue_.push_back(i);
  }
  update_total_ = update_queue_.size();
}

// One game at a time: the next one once no download and no setup is running; at the end what
// came of it, and a newer launcher offered again (it restarts, so it comes last).
void LauncherUi::UpdateQueue() {
  if (!update_total_ || install_process_ || work_->download.load() != 0) return;
  while (!update_queue_.empty()) {
    const size_t game = update_queue_.front();
    if (!running_key_.empty() && running_game_ == game) return;  // after it has ended
    update_queue_.erase(update_queue_.begin());
    if (!UpdateAvailable(game)) continue;  // updated meanwhile
    if (BeginInstall(game, true)) return;
    update_failed_.push_back(german_ ? AllGames()[game].name_de : AllGames()[game].name_en);
  }
  std::string text;
  if (update_failed_.empty()) {
    text = german_ ? "Alle Spiele sind aktualisiert" : "All games are updated";
  } else {
    text = german_ ? "Nicht aktualisiert: " : "Not updated: ";
    for (size_t i = 0; i < update_failed_.size(); ++i) text += (i ? ", " : "") + update_failed_[i];
  }
  error_ = Wide(text);
  error_until_ = ImGui::GetTime() + 8.0;
  update_total_ = 0;
  update_failed_.clear();
  if (hub_update_) hub_update_put_off_ = false;
}

// The button on the start screen while there are updates (click, LT or U), and while they run
// which game is on and how far.
void LauncherUi::DrawUpdateAll(float width, float u) {
  size_t count = 0;
  for (size_t i = 0; i < found_.size(); ++i) {
    if (!found_[i].exe.empty() && UpdateAvailable(i) && !BeingUpdated(i)) ++count;
  }
  if (!update_total_ && !count) return;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImGuiIO& io = ImGui::GetIO();
  const float size = 22 * u, h = 46 * u;
  std::string label;
  if (update_total_) {
    const size_t done = update_total_ - update_queue_.size();
    const size_t game = install_process_ ? install_game_ : work_->game;
    char line[200];
    std::snprintf(line, sizeof(line), german_ ? "Aktualisiere %zu von %zu: %s" : "Updating %zu of %zu: %s", done,
                  update_total_, game < AllGames().size() ? (german_ ? AllGames()[game].name_de : AllGames()[game].name_en) : "");
    label = line;
    if (Downloading()) label += "  " + std::to_string(int(DownloadProgress() * 100.0f)) + " %";
  } else {
    label = (german_ ? "Alle aktualisieren (" : "Update all (") + std::to_string(count) + ")";
  }
  const float glyph = update_total_ ? 30 * u : HintGlyphWidth(bold_, size, "LT", "U") + size * 0.4f;
  const float w = 36 * u + glyph + TextWidth(bold_, size, label.c_str());
  // Between the title and the profile badge; below the title where that is too narrow.
  const float title_right = 46 * u + TextWidth(bold_, 56 * u, "CORE OF LIGHT") + 30 * u;
  ImVec2 a{(title_right + badge_left_ - w) * 0.5f, 47 * u};
  if (a.x < title_right || a.x + w > badge_left_ - 12 * u) a = {48 * u, 116 * u};
  const ImVec2 b{a.x + w, a.y + h};
  const bool over = !update_total_ && io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y &&
                    io.MousePos.y < b.y;
  dl->AddRectFilled(a, b, over ? Rgba(70, 80, 150, 245) : Rgba(24, 30, 70, 235), h * 0.5f);
  dl->AddRect(a, b, kGold, h * 0.5f, 0, 2.5f * u);
  float x = a.x + 18 * u;
  if (update_total_) {
    // A turning arc while it works.
    const ImVec2 c{x + 8 * u, a.y + h * 0.5f};
    const float a0 = float(ImGui::GetTime()) * 5.0f;
    dl->PathArcTo(c, 8 * u, a0, a0 + 4.2f, 24);
    dl->PathStroke(kGold, 0, 2.5f * u);
    x += glyph;
  } else {
    x += HintGlyph(dl, bold_, size, {x, a.y + 11 * u}, "LT", Rgba(80, 90, 140), "U") + size * 0.4f;
  }
  Text(dl, bold_, size, {x, a.y + 11 * u}, update_total_ ? kWhite : kGold, label.c_str());
  if (!update_total_ && ((over && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ||
                         ImGui::IsKeyPressed(ImGuiKey_GamepadL2, false) || ImGui::IsKeyPressed(ImGuiKey_U, false))) {
    StartUpdateAll();
  }
}

// The games folder chosen in the folder browser: set, saved, the games looked for there.
void LauncherUi::ApplyGamesFolder(const std::filesystem::path& chosen) {
  const std::filesystem::path dir = RealFolder(chosen);  // Z:\home\... under Wine: its real place
  HubLog("games folder: " + std::string(reinterpret_cast<const char*>(dir.u8string().c_str())));
  SetInstallDir(dir);
  std::error_code ec;
  std::filesystem::create_directories(HubSettingsFile().parent_path(), ec);  // a first start: not there yet
  const std::u8string u8 = dir.u8string();
  hub_.games_dir = std::string(u8.begin(), u8.end());
  WriteGameSetting(HubSettingsFile(), "Hub", "games_dir", "string", hub_.games_dir);
  if (!install_process_ && !Downloading()) Refresh();
  size_t found = 0;
  for (const Found& f : found_) {
    if (!f.exe.empty() && f.exe.native().starts_with(dir.native())) ++found;
  }
  char line[160];
  std::snprintf(line, sizeof(line), german_ ? "%zu Spiel(e) in diesem Ordner gefunden" : "%zu game(s) found in this folder", found);
  if (setup_.is_open()) setup_.UseGamesFolder(dir);
  if (hub_screen_.is_open()) {
    std::string en, de;
    GamesFolderLabels(en, de);
    hub_screen_.SetActionLabel("choose_games_dir", en, de);
    hub_screen_.ShowMessage(line);
  } else {
    error_ = Wide(line);
    error_until_ = ImGui::GetTime() + 8.0;
  }
}

// The data folder picked in the folder browser: everything in the old one copied there in the
// background (DrawDataMove), then the games told (TakeDataMove).
void LauncherUi::ApplyDataFolder(const std::filesystem::path& picked) {
  const std::filesystem::path to = DataFolderFor(RealFolder(picked));
  const std::filesystem::path from = SkylandersDir();
  std::error_code ec;
  std::string message;
  const std::wstring inside = from.native() + L"\\";
  if (to == from || std::filesystem::equivalent(to, from, ec)) {
    message = german_ ? "Das ist schon der Spieldaten-Ordner" : "That is the data folder already";
  } else if (to.native().starts_with(inside)) {
    message = german_ ? "Der neue Ordner kann nicht im alten liegen" : "The new folder can't be inside the old one";
  } else {
    std::string error;
    if (data_move_.Start(from, to, error)) {
      HubLog("data folder: moving " + std::string(reinterpret_cast<const char*>(from.u8string().c_str())) + " to " +
             std::string(reinterpret_cast<const char*>(to.u8string().c_str())));
      return;
    }
    message = (german_ ? "Geht nicht: " : "Not possible: ") + error;
  }
  if (hub_screen_.is_open()) {
    hub_screen_.ShowMessage(message);
  } else {
    error_ = Wide(message);
    error_until_ = ImGui::GetTime() + 8.0;
  }
}

// The move has finished: the games told where the data is now (the pointer file), the launcher's
// settings, profiles and figures read from there, the old folder offered for deleting.
void LauncherUi::TakeDataMove() {
  std::string error;
  std::filesystem::path old_dir, new_dir;
  bool old_deleted = false;
  if (!data_move_.TakeResult(error, old_dir, new_dir, old_deleted)) return;
  const std::string to = reinterpret_cast<const char*>(new_dir.u8string().c_str());
  std::string message;
  if (!error.empty()) {
    HubLog("data folder: the move failed: " + error);
    message = (german_ ? "Verschieben fehlgeschlagen: " : "The move failed: ") + error;
  } else if (!DocumentsMoved() && !skylanders::SetDataFolder(new_dir)) {
    HubLog("data folder: copied to " + to + ", but the pointer file couldn't be written");
    message = german_ ? "Kopiert, aber die Spiele lie\xC3\x9F" "en sich nicht umstellen" : "Copied, but the games couldn't be told";
  } else {
    if (DocumentsMoved()) SetSkylandersDirOverride(new_dir);
    HubLog("data folder: now " + to + (old_deleted ? " (the old one held only the settings and is gone)" : ""));
    hub_ = LoadHubSettings();
    ApplyHubSettings();
    LoadProfiles(german_);
    figures_ = CountFigures();
    CountAchievements();
    if (!old_deleted) {
      old_data_.push_back({old_dir, new_dir});
      ask_old_data_ = true;
    }
    message = (german_ ? "Spieldaten jetzt in " : "Game data now in ") + to;
  }
  if (hub_screen_.is_open()) {
    std::string en, de;
    DataFolderLabels(en, de);
    hub_screen_.SetActionLabel("choose_data_dir", en, de);
    hub_screen_.ShowMessage(message);
  } else {
    error_ = Wide(message);
    error_until_ = ImGui::GetTime() + 8.0;
  }
}

bool LauncherUi::DrawDataMove(float width, float height) {
  const float u = std::min(width / 1280.0f, height / 720.0f);
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("datamove", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  DrawBackdrop(dl, width, height, std::filesystem::path());
  const char* title = german_ ? "Spieldaten werden verschoben ..." : "Moving the game data ...";
  Text(dl, bold_, 36 * u, {(width - TextWidth(bold_, 36 * u, title)) * 0.5f, height * 0.40f}, kGold, title);
  const char* note = german_ ? "Spielst\xC3\xA4nde, Figuren, Profile und Einstellungen. Der alte Ordner bleibt, bis du ihn l\xC3\xB6schst."
                             : "Saves, figures, profiles and settings. The old folder stays until you delete it.";
  Text(dl, regular_, 20 * u, {(width - TextWidth(regular_, 20 * u, note)) * 0.5f, height * 0.40f + 56 * u}, kDim, note);
  char count[64];
  std::snprintf(count, sizeof(count), german_ ? "%zu Dateien kopiert" : "%zu files copied", data_move_.files());
  Text(dl, bold_, 22 * u, {(width - TextWidth(bold_, 22 * u, count)) * 0.5f, height * 0.40f + 200 * u}, kWhite, count);
  const ImVec2 c{width * 0.5f, height * 0.40f + 150 * u};
  const double t = ImGui::GetTime();
  for (int i = 0; i < 12; ++i) {
    const float a = float(i) / 12.0f * 6.2832f;
    const float lit = float(std::fmod(t * 1.5 - i / 12.0, 1.0));
    dl->AddCircleFilled({c.x + std::cos(a) * 26 * u, c.y + std::sin(a) * 26 * u}, 5 * u,
                        Rgba(255, 210, 90, int(60 + 195 * (1.0f - lit))));
  }
  ImGui::End();
  return true;
}

// "Spieldaten umgezogen": the old folders whose every file is in the data folder now. A deletes
// them (each checked again), B keeps them and isn't asked again (a moved data folder asks anyway).
bool LauncherUi::DrawOldData(float width, float height) {
  const float u = std::min(width / 1280.0f, height / 720.0f);
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("olddata", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  DrawBackdrop(dl, width, height, std::filesystem::path());
  const float bw = 820 * u, bh = 330 * u;
  const ImVec2 a{(width - bw) * 0.5f, (height - bh) * 0.5f}, b{a.x + bw, a.y + bh};
  dl->AddRectFilled(a, b, Rgba(16, 22, 60, 245), 24 * u);
  dl->AddRect(a, b, kGold, 24 * u, 0, 3 * u);
  const char* title = german_ ? "Spieldaten umgezogen" : "Game data moved";
  Text(dl, bold_, 32 * u, {(width - TextWidth(bold_, 32 * u, title)) * 0.5f, a.y + 28 * u}, kGold, title);
  const std::string where = reinterpret_cast<const char*>(SkylandersDir().u8string().c_str());
  const std::string line1 = (german_ ? "Spielst\xC3\xA4nde und Figuren aller Spiele liegen jetzt in " : "Every game's saves and figures are now in ") + where + ".";
  const std::string line2 = (german_ ? "Die alten Ordner sind noch da, alles darin ist am neuen Ort: "
                                     : "The old folders are still there, everything in them is in the new place: ") +
                            OldDataNames(old_data_, 4, german_ ? "Dokumente\\" : "Documents\\");
  Text(dl, regular_, 21 * u, {a.x + 40 * u, a.y + 84 * u}, kWhite, line1.c_str(), bw - 80 * u);
  Text(dl, regular_, 19 * u, {a.x + 40 * u, a.y + 150 * u}, kDim, line2.c_str(), bw - 80 * u);
  const ImGuiIO& io = ImGui::GetIO();
  auto button = [&](float x, const char* pad, ImU32 col, const char* label) {
    const ImVec2 p{x, b.y - 74 * u}, q{x + 300 * u, b.y - 22 * u};
    const bool over = io.MousePos.x >= p.x && io.MousePos.x < q.x && io.MousePos.y >= p.y && io.MousePos.y < q.y;
    dl->AddRectFilled(p, q, over ? Rgba(70, 80, 150, 245) : Rgba(30, 40, 90, 240), 16 * u);
    const float w = TextWidth(bold_, 24 * u, label) + 2 * 0.62f * 24 * u + 24 * u * 0.4f;
    ButtonHint(dl, bold_, 24 * u, {p.x + (300 * u - w) * 0.5f, p.y + 12 * u}, pad, col, label);
    return over && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  };
  const bool yes = button(a.x + 60 * u, "A", Rgba(200, 60, 60), german_ ? "Alte l\xC3\xB6schen" : "Delete old ones");
  const bool no = button(b.x - 360 * u, "B", Rgba(70, 120, 200), german_ ? "Behalten" : "Keep them");
  ImGui::End();
  if (yes || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
    const int deleted = DeleteOldData(old_data_);
    char line[120];
    std::snprintf(line, sizeof(line), german_ ? "%d alte(r) Ordner gel\xC3\xB6scht" : "%d old folder(s) deleted", deleted);
    ShowToast(line);
    old_data_.clear();
    ask_old_data_ = false;
  } else if (no || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    hub_.old_data_kept = true;
    WriteGameSetting(HubSettingsFile(), "Hub", "old_data_kept", "bool", "true");
    HubLog("data folder: the old folders kept");
    old_data_.clear();
    ask_old_data_ = false;
  }
  return true;
}

void LauncherUi::StartDownload(const std::string& url, const std::filesystem::path& file, size_t game, bool update,
                               bool hub) {
  work_->game = game;
  work_->update = update;
  work_->hub = hub;
  work_->file = file;
  work_->progress = 0.0f;
  work_->cancel = false;
  work_->download = 1;
  std::thread([work = work_, url, file] {
    work->download = DownloadFile(url, file, &work->progress, &work->cancel) ? 2 : 3;
  }).detach();
}

bool LauncherUi::BeginInstall(size_t game, bool update) {
  if (game >= setups_.size() || setups_[game].empty() || install_process_ || Downloading()) return false;
  std::error_code ec;
  if (!std::filesystem::exists(setups_[game], ec)) {
    if (game >= setup_urls_.size() || setup_urls_[game].empty()) return false;
    StartDownload(setup_urls_[game], setups_[game], game, update, false);
    return true;
  }
  // An update goes into the folder the game is in (a copied one has no entry that would say so).
  const std::filesystem::path dir =
      update && game < found_.size() && !found_[game].exe.empty() ? found_[game].exe.parent_path() : std::filesystem::path();
  if (!StartGameInstall(game, setups_[game], german_, &install_process_, update, dir)) {
    install_process_ = nullptr;
    return false;
  }
  install_game_ = game;
  install_since_ = ImGui::GetTime();
  install_is_update_ = update;
  return true;
}

void LauncherUi::UpdateDownload() {
  const int state = work_->download.load();
  if (state != 2 && state != 3) return;
  work_->download = 0;
  if (work_->hub) return;  // DrawHubUpdate takes it
  const size_t game = work_->game;
  const std::string name = german_ ? AllGames()[game].name_de : AllGames()[game].name_en;
  if (state == 3 || !BeginInstall(game, work_->update)) {
    if (update_total_ && work_->update) update_failed_.push_back(name);
    error_ = Wide(name + (german_ ? ": Der Download ist fehlgeschlagen" : ": the download failed"));
    error_until_ = ImGui::GetTime() + 8.0;
  }
}

// "Core of Light <version> is here": A downloads the launcher's new setup, runs it silently (it
// keeps the install folder and starts the launcher again, /RELAUNCH=1) and quits; B leaves it
// for this launch.
bool LauncherUi::DrawHubUpdate(float width, float height) {
  const Release& release = *hub_update_;
  const float u = std::min(width / 1280.0f, height / 720.0f);
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("hubupdate", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  DrawBackdrop(dl, width, height, std::filesystem::path());
  const float bw = 680 * u, bh = 280 * u;
  const ImVec2 a{(width - bw) * 0.5f, (height - bh) * 0.5f}, b{a.x + bw, a.y + bh};
  dl->AddRectFilled(a, b, Rgba(16, 22, 60, 245), 24 * u);
  dl->AddRect(a, b, kGold, 24 * u, 0, 3 * u);
  const std::string title = "Core of Light " + release.version;
  Text(dl, bold_, 32 * u, {(width - TextWidth(bold_, 32 * u, title.c_str())) * 0.5f, a.y + 30 * u}, kGold, title.c_str());
  char line[200];
  std::snprintf(line, sizeof(line), german_ ? "Installiert: %s   \xC2\xB7   Download: %.1f MB" : "Installed: %s   \xC2\xB7   Download: %.1f MB",
                COREOFLIGHT_VERSION, double(release.size) / (1024.0 * 1024.0));
  Text(dl, regular_, 21 * u, {(width - TextWidth(regular_, 21 * u, line)) * 0.5f, a.y + 84 * u}, kWhite, line);
  const char* keeps = german_ ? "Der Hub l\xC3\xA4" "dt sie, installiert sie und startet neu. Spiele und Spielst\xC3\xA4nde bleiben."
                              : "The launcher downloads it, installs it and starts again. Games and saves stay.";
  Text(dl, regular_, 18 * u, {(width - TextWidth(regular_, 18 * u, keeps)) * 0.5f, a.y + 118 * u}, kDim, keeps);
  const bool downloading = Downloading() && work_->hub;
  const int state = work_->download.load();
  if (downloading || (work_->hub && state >= 2)) {
    // The bar while it downloads.
    const ImVec2 p{a.x + 60 * u, a.y + 180 * u}, q{b.x - 60 * u, a.y + 206 * u};
    dl->AddRectFilled(p, q, Rgba(30, 40, 90, 240), 13 * u);
    dl->AddRectFilled(p, {p.x + (q.x - p.x) * DownloadProgress(), q.y}, kGold, 13 * u);
    ImGui::End();
    if (state == 2) {
      work_->download = 0;
      STARTUPINFOW startup = {sizeof(startup)};
      PROCESS_INFORMATION info = {};
      // Into the folder this launcher runs from: under Wine the setup took its default folder in the
      // prefix and started the new launcher there, while Steam's shortcut pointed at the old one (a
      // colleague's Bazzite, noted in #19).
      std::wstring command = L"\"" + work_->file.wstring() + L"\" /SILENT /SUPPRESSMSGBOXES /NORESTART /RELAUNCH=1 \"/DIR=" +
                             launcher_dir_.wstring() + L"\"";
      if (CreateProcessW(work_->file.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &info)) {
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        return false;  // the launcher quits; the setup replaces it and starts the new one
      }
      error_ = german_ ? L"Das Update lie\x00DF sich nicht starten" : L"Could not start the update";
      error_until_ = ImGui::GetTime() + 8.0;
      hub_update_put_off_ = true;
    } else if (state == 3) {
      work_->download = 0;
      error_ = german_ ? L"Der Download des Updates ist fehlgeschlagen" : L"The update's download failed";
      error_until_ = ImGui::GetTime() + 8.0;
      hub_update_put_off_ = true;
    }
    return true;
  }
  const ImGuiIO& io = ImGui::GetIO();
  auto button = [&](float x, const char* pad, ImU32 col, const char* label) {
    const ImVec2 p{x, a.y + 180 * u}, q{x + 260 * u, a.y + 232 * u};
    const bool over = io.MousePos.x >= p.x && io.MousePos.x < q.x && io.MousePos.y >= p.y && io.MousePos.y < q.y;
    dl->AddRectFilled(p, q, over ? Rgba(70, 80, 150, 245) : Rgba(30, 40, 90, 240), 16 * u);
    const float w = TextWidth(bold_, 24 * u, label) + 2 * 0.62f * 24 * u + 24 * u * 0.4f;
    ButtonHint(dl, bold_, 24 * u, {p.x + (260 * u - w) * 0.5f, p.y + 12 * u}, pad, col, label);
    return over && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  };
  const bool yes = button(a.x + 60 * u, "A", Rgba(70, 170, 70), german_ ? "Jetzt aktualisieren" : "Update now");
  const bool no = button(b.x - 320 * u, "B", Rgba(200, 60, 60), german_ ? "Sp\xC3\xA4ter" : "Later");
  ImGui::End();
  if (yes || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
    if (!Downloading() && !install_process_) {
      StartDownload(release.url, DownloadsDir() / AssetPath(release.asset), 0, false, true);
    }
  } else if (no || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    hub_update_put_off_ = true;
  }
  return true;
}

bool LauncherUi::DrawScreens(float width, float height) {
  // The disc browser closed without a disc for the language: the next one it opens for is a disc.
  if (browse_for_language_ && !browser_.is_open()) {
    browse_for_language_ = false;
    browser_.SetForLanguage(false);
  }
  const ImGuiIO& io = ImGui::GetIO();
  if (starting_) return DrawStarting(width, height);
  // A newer launcher first, before the first-run setup too: the newest one sets the games up.
  if (hub_update_ && !hub_update_put_off_ && ask_update_ < 0 && !confirm_quit_ && !install_process_ &&
      !import_.running()) {
    return DrawHubUpdate(width, height);
  }
  if (ask_updates_found_ && !install_process_) return DrawUpdatesFound(width, height);
  TakeDataMove();
  if (data_move_.running()) return DrawDataMove(width, height);
  if (ask_old_data_ && !setup_.is_open() && !install_process_ && !import_.running() && !folder_browser_.is_open()) {
    return DrawOldData(width, height);
  }
  // The games folder's browser, over the first-run setup or the hub's settings it came from.
  if (folder_browser_.is_open()) {
    folder_browser_.Draw(width, height);
    if (const std::filesystem::path chosen = folder_browser_.TakeChosen(); !chosen.empty()) {
      if (folder_browser_.for_figures()) {
        // The Imaginators exe checks the files (figure_import.h); the toast says what came in.
        static const Found none;
        size_t im = 0;
        while (im < AllGames().size() && std::string_view(AllGames()[im].name_en) != "Imaginators") ++im;
        figure_import_.Start(chosen, im < found_.size() ? found_[im] : none, FiguresDir(), german_);
        folder_browser_.SetForData(false);
      } else if (folder_for_data_) {
        ApplyDataFolder(chosen);
      } else {
        ApplyGamesFolder(chosen);
      }
    }
    return true;
  }
  if (ask_language_) return DrawLanguage(width, height);
  if (setup_.is_open()) {
    // X on its disc page: the file browser for an ISO the search didn't find, over the setup.
    if (browser_.is_open()) {
      browser_.Draw(width, height);
      if (const std::filesystem::path chosen = browser_.TakeChosen(); !chosen.empty()) {
        // Imaginators' PS3 disc: its languages (TakeLanguageDisc), not a disc to unpack.
        if (browse_for_language_ || IsImaginatorsPs3Disc(chosen)) {
          TakeLanguageDisc(chosen);
        } else {
          setup_.AddIso(chosen);
        }
      }
      return true;
    }
    setup_.Draw(width, height, {&found_, &setups_, install_process_ != nullptr || Downloading(), &import_});
    // It says what comes next; the launcher's own install and disc import do it.
    const SetupScreen::Job job = setup_.TakeJob();
    if (job.kind == SetupScreen::Job::Kind::kInstall && !install_process_ && job.game < setups_.size()) {
      BeginInstall(job.game, false);
    } else if (job.kind == SetupScreen::Job::Kind::kImport && !import_.running()) {
      import_.Start(job.iso, found_, german_);
    } else if (job.kind == SetupScreen::Job::Kind::kCancel) {
      import_.Cancel();
    } else if (job.kind == SetupScreen::Job::Kind::kPickIso) {
      browser_.Open();
    } else if (job.kind == SetupScreen::Job::Kind::kPickFolder) {
      folder_for_data_ = false;
      folder_browser_.SetForData(false);
      folder_browser_.Open(GamesFolderChosen() ? GamesFolder() : std::filesystem::path());
    } else if (job.kind == SetupScreen::Job::Kind::kPickDataFolder) {
      folder_for_data_ = true;
      folder_browser_.SetForData(true);
      folder_browser_.Open(SkylandersDir().parent_path());
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
  if (ask_import_settings_) return DrawImportQuestion(width, height);
  if (!ask_reimport_.empty()) return DrawReimportQuestion(width, height);
  if (confirm_quit_) return DrawQuitConfirm(width, height);
  if (ask_update_ >= 0) return DrawUpdateConfirm(width, height);
  if (crash_code_ != 0) return DrawCrashReport(width, height);
  if (browser_.is_open()) {
    browser_.Draw(width, height);
    // The chosen disc is recognised by its default.xex and unpacked into its game's rom folder;
    // Imaginators' PS3 disc brings its languages (TakeLanguageDisc).
    if (const std::filesystem::path chosen = browser_.TakeChosen(); !chosen.empty()) {
      if (browser_.for_backup()) {
        std::string error;
        if (!data_transfer_.PlanImport(chosen, found_, german_, error)) {
          ShowToast(error, 8.0);
        } else if (!data_transfer_.settings_differ().empty()) {
          ask_import_settings_ = true;
        } else {
          data_transfer_.StartImport(false);
        }
      } else if (browse_for_language_ || IsImaginatorsPs3Disc(chosen)) {
        TakeLanguageDisc(chosen);
      } else if (!import_.running()) {
        import_.Start(chosen, found_, german_, repair_disc_);
      }
    }
    // Closed (chosen or not): the next time it is for discs again.
    if (!browser_.is_open()) {
      browser_.SetForBackup(false);
      repair_disc_ = false;
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
  if (saves_screen_.is_open()) {
    saves_screen_.Draw(width, height);
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
    if (language_label_pending_) language_label_pending_ = !LanguageLabel();
    // A changed vibration strength buzzes that player's pad (XInput slot 1 / 2) at once (#35).
    for (int p = 0; p < 2; ++p) {
      const std::string v = hub_screen_.EditedValue(p == 0 ? "pad_rumble_p1" : "pad_rumble_p2");
      if (!rumble_seen_[p].empty() && v != rumble_seen_[p] && g_pad_connected[p]) PadBuzz(DWORD(p), std::atoi(v.c_str()));
      rumble_seen_[p] = v;
    }
    // Saved settings apply at once (the language with the next frame).
    if (const auto saved = hub_screen_.TakeSaved(); !saved.empty()) {
      hub_ = LoadHubSettings();
      ApplyHubSettings();
      LoadProfiles(german_);
      // The vibration strength goes into every game's settings file, as its F4 > Controller would
      // write it ([Input], pad_rumble_p1 / _p2).
      for (const char* cvar : {"pad_rumble_p1", "pad_rumble_p2"}) {
        auto it = saved.find(cvar);
        if (it == saved.end()) continue;
        for (Found& f : found_) {
          if (f.exe.empty()) continue;
          const std::filesystem::path toml = f.exe.parent_path() / (UserDataName(f.exe) + ".toml");
          if (WriteGameSetting(toml, "Input", cvar, "int", it->second)) {
            f.settings[cvar] = it->second;
          } else {
            HubLog("controller page: could not write " + std::string(cvar) + " into " +
                   std::string(reinterpret_cast<const char*>(toml.u8string().c_str())));
          }
        }
      }
    }
    // "Problem melden": the report on the desktop, shown in Explorer.
    const std::string action = hub_screen_.TakeAction();
    if (action == "choose_games_dir") {
      folder_for_data_ = false;
      folder_browser_.SetForData(false);
      folder_browser_.Open(GamesFolderChosen() ? GamesFolder() : std::filesystem::path());
    }
    if (action == "choose_data_dir") {
      folder_for_data_ = true;
      folder_browser_.SetForData(true);
      folder_browser_.Open(SkylandersDir().parent_path());
    }
    if (action == "import_im_figures" && !figure_import_.running()) {
      folder_for_data_ = false;
      folder_browser_.SetForData(false);
      folder_browser_.SetForFigures(true);
      folder_browser_.Open();
    }
    if (action == "im_language") AskLanguage({});  // the language first, then the PS3 disc
    if (action == "check_updates") StartUpdateCheck(true);
    // "Meine Daten exportieren / importieren" (data_transfer.h, #40): the export onto the desktop,
    // the import from the file picked in the browser.
    if (action == "export_data" && !data_transfer_.running()) {
      data_transfer_.StartExport(found_, german_);
      hub_screen_.ShowMessage(german_ ? "Exportiere ..." : "Exporting ...");
    }
    if (action == "import_data" && !data_transfer_.running()) {
      browse_for_language_ = false;
      browser_.SetForLanguage(false);
      browser_.SetForBackup(true);
      browser_.Open(ExportFolder());  // where the export goes
    }  // DrawToast shows it and what it finds
    if (action == "report_problem") {
      std::string error;
      const std::filesystem::path report = WriteProblemReport(found_, launcher_dir_, german_, error);
      if (report.empty()) {
        hub_screen_.ShowMessage(error);
      } else {
        const std::u8string file = report.filename().u8string();
        hub_screen_.ShowMessage((german_ ? "Auf dem Desktop: " : "On the desktop: ") +
                                std::string(file.begin(), file.end()));
        const std::wstring select = L"/select,\"" + report.wstring() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", select.c_str(), nullptr, SW_SHOWNORMAL);
      }
    }
    return true;
  }
  if (settings_.is_open()) {
    settings_.Draw(width, height);
    // What was saved shows on the card at once.
    // The loader kept the values from before the save: the next look at the settings reads them
    // again, or the screen showed the old ones and the change seemed lost (#19; the owner too:
    // "Nativ" and "HD" were back although the file had the new values).
    bool saved = false;
    for (const auto& [cvar, value] : settings_.TakeSaved()) {
      found_[settings_game_].settings[cvar] = value;
      saved = true;
    }
    if (saved && settings_game_ < loaders_.size()) loaders_[settings_game_] = SettingsLoader();
    // "Shader-Cache leeren" (game_settings.cpp): the game's shaders in the data folder, and in its old
    // folder in Documents for a game from before the move; it builds them again on its next start.
    const std::string game_action = settings_.TakeAction();
    // "Spieldateien pr\xC3\xBC" "fen" (game_files.h, #41): all there, or what isn't and the offer
    // to set the disc up again.
    if (game_action == "check_game_files" && settings_game_ < found_.size()) {
      const Found& f = found_[settings_game_];
      const GameFilesCheck check = CheckGameFiles(f.rom, AllGames()[settings_game_].xex_sha256);
      if (check.ok()) {
        settings_.ShowMessage(german_ ? "Alles da (" + std::to_string(check.files) + " Dateien)"
                                      : "Everything's there (" + std::to_string(check.files) + " files)");
      } else {
        std::vector<std::string> bad = check.missing;
        bad.insert(bad.end(), check.changed.begin(), check.changed.end());
        std::string names;
        for (size_t i = 0; i < bad.size() && i < 4; ++i) names += (i ? ", " : "") + bad[i];
        if (bad.size() > 4) names += ", ...";
        // "2 Dateien fehlen, 1 Datei ist nicht vollständig: ...", only the parts that apply.
        auto count = [&](size_t n, const char* one_de, const char* many_de, const char* one_en, const char* many_en) {
          char text[96];
          std::snprintf(text, sizeof(text), n == 1 ? (german_ ? one_de : one_en) : (german_ ? many_de : many_en), n);
          return std::string(text);
        };
        std::string what;
        if (!check.missing.empty()) {
          what = count(check.missing.size(), "%zu Datei fehlt", "%zu Dateien fehlen", "%zu file is missing",
                       "%zu files are missing");
        }
        if (!check.changed.empty()) {
          what += (what.empty() ? "" : ", ") +
                  count(check.changed.size(), "%zu Datei ist nicht vollst\xC3\xA4ndig",
                        "%zu Dateien sind nicht vollst\xC3\xA4ndig", "%zu file isn't whole", "%zu files aren't whole");
        }
        ask_reimport_ = what + ": " + names;
      }
    }
    if (game_action == "reset_shader_cache" && settings_game_ < found_.size()) {
      const std::filesystem::path exe = found_[settings_game_].exe;
      std::error_code ec;
      uintmax_t removed = 0;
      for (const std::filesystem::path& dir : {SkylandersDir() / GameFolderName(exe) / "cache" / "shaders",
                                               DocumentsDir() / UserDataName(exe) / "cache" / "shaders"}) {
        const uintmax_t n = std::filesystem::remove_all(dir, ec);
        if (!ec && n != static_cast<uintmax_t>(-1)) removed += n;
      }
      HubLog("shader cache: " + UserDataName(exe) + ", " + std::to_string(removed) + " entries deleted");
      settings_.ShowMessage(removed ? (german_ ? "Shader-Cache geleert: der n\xC3\xA4" "chste Start baut ihn neu"
                                               : "Shader cache emptied: the next start builds it again")
                                    : (german_ ? "Der Shader-Cache ist schon leer" : "The shader cache is empty already"));
    }
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
  DrawUpdateAll(width, u);  // "Alle aktualisieren", while there are updates

  // The cards: six in a row, or three in two rows on a narrow window.
  const size_t n = found_.size();
  const bool two_rows = width / height < 1.45f;
  const size_t per_row = two_rows ? 3 : n;
  const float gap = 22 * u;
  const float area_w = width - 96 * u;
  float card_w = std::min((area_w - gap * (per_row - 1)) / per_row, 300 * u);
  float card_h = two_rows ? card_w * 1.25f : card_w * 1.9f;
  // On a window wider than 16:9 (ultrawide) or a tall one the width alone would make the cards
  // taller than the room between the title row and the hints at the bottom; there the height
  // decides, the cards keep their shape.
  const float room_h = (height - 380 * u - (two_rows ? gap : 0.0f)) / (two_rows ? 2.0f : 1.0f);
  if (room_h > 0 && card_h > room_h) {
    card_w *= room_h / card_h;
    card_h = room_h;
  }
  const float rows_h = two_rows ? 2 * card_h + gap : card_h;
  const float x0 = (width - (card_w * per_row + gap * (per_row - 1))) * 0.5f;
  const float y0 = 150 * u + (height - 230 * u - 150 * u - rows_h) * 0.5f;

  // Input: d-pad, stick, arrows; A / Enter starts, B / Escape closes.
  auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
  const bool up = pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_GamepadDpadUp) || pressed(ImGuiKey_GamepadLStickUp);
  const bool down = pressed(ImGuiKey_DownArrow) || pressed(ImGuiKey_GamepadDpadDown) || pressed(ImGuiKey_GamepadLStickDown);
  // Up from the (top row of) cards: the top bar, the controllers and the profile, so a pad reaches
  // them too (the owner, 2026-10-25: only the mouse could); left / right between the two, A opens,
  // B or down goes back to the cards.
  bool any_pad = std::any_of(std::begin(g_pad_connected), std::end(g_pad_connected), [](const auto& c) { return bool(c); });
  for (const GenericPad& pad : g_generic_pads) any_pad = any_pad || (pad.connected && !pad.skip);
  const bool on_top = top_focus_ >= 0;
  if (on_top) {
    if (pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_GamepadDpadLeft) || pressed(ImGuiKey_GamepadLStickLeft)) {
      top_focus_ = 0;
    }
    if (pressed(ImGuiKey_RightArrow) || pressed(ImGuiKey_GamepadDpadRight) || pressed(ImGuiKey_GamepadLStickRight)) {
      top_focus_ = 1;
    }
    if (!any_pad) top_focus_ = 1;
    if (down) top_focus_ = -1;
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)) {
      if (top_focus_ == 0) {
        OpenHubSettings(kHubControllerPage);
      } else {
        profiles_screen_.Open(&found_);
      }
    }
  } else if (up && (!two_rows || selected_ < per_row)) {
    top_focus_ = any_pad ? 0 : 1;
  }
  if (n && !on_top && top_focus_ < 0) {
    if (pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_GamepadDpadLeft) || pressed(ImGuiKey_GamepadLStickLeft)) {
      selected_ = (selected_ + n - 1) % n;
    }
    if (pressed(ImGuiKey_RightArrow) || pressed(ImGuiKey_GamepadDpadRight) || pressed(ImGuiKey_GamepadLStickRight)) {
      selected_ = (selected_ + 1) % n;
    }
    if (two_rows && (up || down)) {
      selected_ = (selected_ + per_row) % n;
    }
  }
  for (size_t i = 0; i < n; ++i) {
    const size_t row = i / per_row, col = i % per_row;
    const float x = x0 + col * (card_w + gap), y = y0 + row * (card_h + gap);
    const bool hovered = io.MousePos.x >= x && io.MousePos.x < x + card_w && io.MousePos.y >= y &&
                         io.MousePos.y < y + card_h;
    if (hovered && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
      selected_ = i;
      top_focus_ = -1;
    }
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
  if (!on_top && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false) ||
                  ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false))) {
    Start(selected_);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false) || ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
    OpenSettings(selected_);
  }
  // Start / F: the figure library (figures_ui.h).
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) || ImGui::IsKeyPressed(ImGuiKey_F, false)) {
    figures_screen_.Configure(hub_.figures_only_mine, hub_.figures_marks, hub_.figures_progress);
    figures_screen_.Open(FiguresDir());
  }
  // LB / Q: the launcher's own settings (hub_settings.h); a click on a pad at the top: their
  // "Controller" page.
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
    OpenHubSettings(0);
  } else if (pad_clicked_) {
    OpenHubSettings(kHubControllerPage);
  }
  // RB / E: the chosen game's achievements (achievements_ui.h).
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) || ImGui::IsKeyPressed(ImGuiKey_E, false)) {
    achievements_screen_.Open(selected_);
  }
  // RT / S: the chosen game's saves and their copies (saves_ui.h).
  if ((ImGui::IsKeyPressed(ImGuiKey_GamepadR2, false) || ImGui::IsKeyPressed(ImGuiKey_S, false)) &&
      selected_ < found_.size() && !found_[selected_].exe.empty()) {
    saves_screen_.Open(selected_, found_[selected_].exe, GameBackground(selected_));
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
  bool back = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
  if (back && on_top) {  // from the top bar: back to the cards, not quit
    top_focus_ = -1;
    back = false;
  }
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
    const std::filesystem::path usual = SkylandersDir() / "Figures";
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
  // The figure count line that was here went (the owner: nobody needs it there, #25); the
  // launcher's version small at the bottom right instead (#22).
  (void)figures;
  {
    const char* version = "Core of Light " COREOFLIGHT_VERSION;
    Text(dl, regular_, 16 * u, {width - 48 * u - TextWidth(regular_, 16 * u, version), height - 86 * u}, kDim, version);
  }
  // While a game writes its pictures (UpdatePictureExport), a note at the bottom right: the game
  // runs for it in the background, and should its window show for a moment, it isn't a pop-up to
  // close (a colleague did, 2026-10-23).
  if (pictures_process_ && pictures_game_ < AllGames().size()) {
    const Game& game = AllGames()[pictures_game_];
    const std::string note =
        german_ ? std::string(game.name_de) + ": Bilder werden von der Disc gelesen. L\xC3\xA4uft im Hintergrund und endet von selbst."
                : std::string(game.name_en) + ": reading its pictures from the disc. Runs in the background and ends by itself.";
    const float size = 19 * u, tw = TextWidth(regular_, size, note.c_str());
    const float right = width - 48 * u, x = right - tw - 46 * u;
    // Beside the figures line, or above it where the window is too narrow for both.
    const bool beside = x > 48 * u + TextWidth(regular_, 20 * u, figures) + 24 * u;
    const float y = height - (beside ? 100 : 144) * u;
    dl->AddRectFilled({x, y}, {right, y + 36 * u}, Rgba(24, 30, 70, 230), 18 * u);
    const ImVec2 c{x + 22 * u, y + 18 * u};
    const float a0 = float(ImGui::GetTime()) * 5.0f;
    dl->PathArcTo(c, 8 * u, a0, a0 + 4.2f, 24);
    dl->PathStroke(kGold, 0, 2.5f * u);
    Text(dl, regular_, size, {x + 38 * u, y + 7 * u}, kWhite, note.c_str());
  }
  float hx = 48 * u;
  const float hy = height - 52 * u;
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 170, 70), german_ ? "Starten" : "Play");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Y", Rgba(220, 180, 40), german_ ? "Einstellungen" : "Settings", "Tab");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "X", Rgba(50, 120, 220), german_ ? "Disc einlesen" : "Add a disc", "D");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Start", Rgba(80, 90, 140), german_ ? "Figuren" : "Figures");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "RB", Rgba(80, 90, 140), german_ ? "Erfolge" : "Achievements");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "RT", Rgba(80, 90, 140), german_ ? "Spielst\xC3\xA4nde" : "Saves");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Back", Rgba(80, 90, 140), german_ ? "Profile" : "Profiles", "P");
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
