#include "setup_ui.h"
#include "hints.h"

#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <mutex>
#include <optional>
#include <system_error>
#include <utility>

#include "game_settings.h"
#include "hub_settings.h"
#include "imgui.h"
#include "nxe_art.h"
#include "xiso.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;

ImU32 Rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

const ImU32 kGold = Rgba(255, 204, 64);
const ImU32 kWhite = Rgba(255, 255, 255);
const ImU32 kDim = Rgba(180, 190, 215);
const ImU32 kPanel = Rgba(24, 30, 70, 235);
const ImU32 kEdge = Rgba(90, 100, 150);
const ImU32 kGreen = Rgba(110, 220, 110);
const ImU32 kRed = Rgba(240, 110, 110);

// What a game takes once installed (its exe and libraries, about 80 MB), and the room the disc
// import keeps free on top (disc_import.cpp kSpareBytes).
constexpr uint64_t kInstallBytes = 150ull << 20;
constexpr uint64_t kSpareBytes = 256ull << 20;

float TextWidth(ImFont* font, float size, const std::string& text) {
  return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
}

void Text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const std::string& text,
          float wrap = 0.0f) {
  dl->AddText(font, size, pos, col, text.c_str(), nullptr, wrap);
}

// `text` cut with "..." in front so it fits `width` (a long path keeps its end).
std::string FitStart(ImFont* font, float size, std::string text, float width) {
  if (TextWidth(font, size, text) <= width) return text;
  while (text.size() > 4 && TextWidth(font, size, "..." + text) > width) {
    size_t cut = 1;
    while (cut < text.size() && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) ++cut;
    text.erase(0, cut);
  }
  return "..." + text;
}

// A hint: the controller button or the keyboard key (hints.h), and what it does.
float Hint(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const char* button, ImU32 col,
           const std::string& label, const char* key = nullptr) {
  const float x = pos.x + HintGlyph(dl, font, size, pos, button, col, key) + size * 0.4f;
  Text(dl, font, size, {x, pos.y}, kWhite, label);
  HintClickArea({pos.x - size * 0.2f, pos.y - size * 0.25f}, {x + TextWidth(font, size, label) + size * 0.3f, pos.y + size * 1.3f},
                button);  // a click on it presses the button (hints.h)
  return x + TextWidth(font, size, label) + size * 1.2f;
}

std::string Utf8(const fs::path& p) {
  const std::u8string u8 = p.u8string();
  return std::string(u8.begin(), u8.end());
}

std::string Gigabytes(uint64_t bytes, bool german) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.1f GB", double(bytes) / double(1ull << 30));
  std::string out = text;
  if (german) std::replace(out.begin(), out.end(), '.', ',');
  return out;
}

// Bytes free for this user on the drive `path` is on (its nearest existing folder), or nullopt.
std::optional<uint64_t> FreeBytes(fs::path path) {
  std::error_code ec;
  while (!path.empty() && !fs::exists(path, ec)) {
    if (path == path.parent_path()) break;
    path = path.parent_path();
  }
  ULARGE_INTEGER free{};
  if (!GetDiskFreeSpaceExW(path.c_str(), &free, nullptr, nullptr)) return std::nullopt;
  return free.QuadPart;
}

bool Pressed(ImGuiKey key) { return ImGui::IsKeyPressed(key, true); }
bool Once(ImGuiKey key) { return ImGui::IsKeyPressed(key, false); }
bool Up() { return Pressed(ImGuiKey_UpArrow) || Pressed(ImGuiKey_GamepadDpadUp) || Pressed(ImGuiKey_GamepadLStickUp); }
bool Down() {
  return Pressed(ImGuiKey_DownArrow) || Pressed(ImGuiKey_GamepadDpadDown) || Pressed(ImGuiKey_GamepadLStickDown);
}
bool Left() {
  return Once(ImGuiKey_LeftArrow) || Once(ImGuiKey_GamepadDpadLeft) || Once(ImGuiKey_GamepadLStickLeft);
}
bool Right() {
  return Once(ImGuiKey_RightArrow) || Once(ImGuiKey_GamepadDpadRight) || Once(ImGuiKey_GamepadLStickRight);
}
bool Accept() { return Once(ImGuiKey_Enter) || Once(ImGuiKey_KeypadEnter) || Once(ImGuiKey_GamepadFaceDown); }
bool Back() { return Once(ImGuiKey_Escape) || Once(ImGuiKey_GamepadFaceRight); }

void Title(ImDrawList* dl, ImFont* bold, float u, const char* title) {
  Text(dl, bold, 22 * u, {48 * u, 34 * u}, kDim, "SKYLANDERS: CORE OF LIGHT");
  Text(dl, bold, 56 * u, {46 * u, 56 * u}, kGold, title);
}

// A button row: gold when chosen.
void Button(ImDrawList* dl, ImFont* bold, float u, float x, float y, float w, const std::string& label, bool chosen,
            bool enabled = true) {
  const ImU32 fill = !enabled ? Rgba(40, 46, 80) : chosen ? kGold : Rgba(30, 38, 80);
  dl->AddRectFilled({x, y}, {x + w, y + 46 * u}, fill, 23 * u);
  const ImU32 col = !enabled ? kDim : chosen ? Rgba(60, 35, 0) : kWhite;
  Text(dl, bold, 22 * u, {x + (w - TextWidth(bold, 22 * u, label)) * 0.5f, y + 10 * u}, col, label);
}

}  // namespace

void SetupScreen::Init(ImFont* regular, ImFont* bold, bool german) {
  regular_ = regular;
  bold_ = bold;
  german_ = german;
}

void SetupScreen::Open() {
  open_ = true;
  page_ = Page::kWelcome;
  selected_ = 0;
  rows_.assign(AllGames().size(), Row{});
  search_.reset();
  taken_ = 0;
  current_ = -1;
  stopping_ = false;
  job_ = {};
  FindGameFolders();
}

void SetupScreen::FindGameFolders() {
  folders_ = {std::filesystem::path()};
  folder_ = 0;
  wchar_t windows[MAX_PATH] = L"";
  GetWindowsDirectoryW(windows, MAX_PATH);
  const std::wstring system_drive = std::filesystem::path(windows).root_name().wstring();
  const DWORD drives = GetLogicalDrives();
  for (int d = 0; d < 26; ++d) {
    if (!(drives & (1u << d))) continue;
    const std::wstring root = std::wstring(1, wchar_t(L'A' + d)) + L":\\";
    if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED || root.substr(0, 2) == system_drive) continue;
    const std::filesystem::path dir = std::filesystem::path(root) / L"Skylanders Recompiled";
    // Only a drive the folder can go on: under Wine / Proton Z: is the Linux root, not writable.
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
      const std::wstring probe = root + L"coreoflight-probe.tmp";
      const HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
      if (file == INVALID_HANDLE_VALUE) continue;
      CloseHandle(file);
    }
    folders_.push_back(dir);
  }
  // One set before (an earlier setup, coreoflight.ini, --install_dir) is chosen, and kept.
  if (GamesFolderChosen()) {
    const auto it = std::find(folders_.begin(), folders_.end(), GamesFolder());
    if (it == folders_.end()) folders_.push_back(GamesFolder());
    folder_ = int(std::find(folders_.begin(), folders_.end(), GamesFolder()) - folders_.begin());
  }
}

void SetupScreen::ChooseGameFolder(int choice) {
  if (folders_.empty()) return;
  folder_ = (choice + int(folders_.size())) % int(folders_.size());
  const std::filesystem::path& dir = folders_[size_t(folder_)];
  SetInstallDir(dir);
  std::error_code ec;
  std::filesystem::create_directories(HubSettingsFile().parent_path(), ec);  // a first start: not there yet
  const std::u8string u8 = dir.u8string();
  WriteGameSetting(HubSettingsFile(), "Hub", "games_dir", "string", std::string(u8.begin(), u8.end()));
}

void SetupScreen::UseGamesFolder(const std::filesystem::path& dir) {
  auto it = std::find(folders_.begin(), folders_.end(), dir);
  if (it == folders_.end()) it = folders_.insert(folders_.end(), dir);
  folder_ = int(it - folders_.begin());
}

SetupScreen::Job SetupScreen::TakeJob() { return std::exchange(job_, Job{}); }
std::string SetupScreen::TakeLanguage() { return std::exchange(language_, {}); }
bool SetupScreen::TakeFinished() { return std::exchange(finished_, false); }

// The discs the search has found since the last look: the first ISO of each game, with its size.
void SetupScreen::TakeSearchResults() {
  if (!search_) return;
  std::vector<FoundDisc> found;
  {
    std::lock_guard lock(search_->mutex);
    for (; taken_ < search_->found.size(); ++taken_) found.push_back(search_->found[taken_]);
  }
  for (const FoundDisc& d : found) {
    if (d.game < 0 || size_t(d.game) >= rows_.size()) continue;
    Row& r = rows_[size_t(d.game)];
    if (!r.iso.empty() || r.state == State::kReady) continue;
    auto image = XisoImage::Open(d.iso);
    if (!image) continue;
    r.iso = d.iso;
    r.bytes = image->total_size("$SystemUpdate");
    r.state = State::kFound;
    r.chosen = true;
    r.automatic = true;
  }
}

void SetupScreen::AddIso(const std::filesystem::path& iso) {
  if (page_ == Page::kWelcome) page_ = Page::kDiscs;  // dropped on the window before
  if (page_ != Page::kDiscs) return;
  auto image = XisoImage::Open(iso);
  const XisoImage::Entry* xex = image ? image->Find("default.xex") : nullptr;
  std::vector<uint8_t> data;
  if (!xex || !image->Read(*xex, data)) {
    message_ = german_ ? "Das ist keine Xbox-360-Disc (ISO)." : "That isn't an Xbox 360 disc (ISO).";
    return;
  }
  const std::string hash = Sha256Hex(data.data(), data.size());
  for (size_t i = 0; i < AllGames().size() && i < rows_.size(); ++i) {
    if (!IsGameDisc(AllGames()[i], hash)) continue;
    Row& r = rows_[i];
    r.iso = iso;
    r.bytes = image->total_size("$SystemUpdate");
    r.automatic = false;
    if (r.state != State::kReady) {
      r.state = State::kFound;
      r.chosen = true;
    }
    message_.clear();
    if (pick_for_ >= 0 && size_t(pick_for_) != i) {
      const Game& g = AllGames()[i];
      message_ = (german_ ? "Das ist die Disc von " : "That is the disc of ") + std::string(german_ ? g.name_de : g.name_en) +
                 (german_ ? ": dort eingetragen." : ": filled in there.");
    }
    selected_ = int(i);
    pick_for_ = -1;
    // The other games' ISOs are often next to it: its folder and the one above, one level down.
    if (!search_ || search_->done) {
      const fs::path folder = iso.parent_path();
      const fs::path above = folder.has_relative_path() ? folder.parent_path() : folder;
      if (search_) search_->stop = true;
      search_ = StartDiscSearch({{above, 1}});
      taken_ = 0;
      nearby_ = true;
    }
    return;
  }
  message_ = german_ ? "Diese Disc-Version unterst\xC3\xBCtzt keines der Spiele."
                     : "None of the games supports this disc version.";
}

uint64_t SetupScreen::NeededBytes() const {
  uint64_t bytes = 0;
  for (const Row& r : rows_) {
    if (r.chosen) bytes += r.bytes + kInstallBytes;
  }
  return bytes ? bytes + kSpareBytes : 0;
}

void SetupScreen::Draw(float width, float height, const Launcher& launcher) {
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("setup", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  // The games already there (installed, their disc checked): nothing to do for them.
  for (size_t i = 0; i < rows_.size() && launcher.found && i < launcher.found->size(); ++i) {
    const Found& f = (*launcher.found)[i];
    Row& r = rows_[i];
    if ((r.state == State::kNoDisc || r.state == State::kFound) && !f.exe.empty() &&
        f.disc.load() == DiscState::kMatches) {
      r.state = State::kReady;
      r.chosen = false;
    }
  }
  switch (page_) {
    case Page::kWelcome:
      DrawWelcome(width, height);
      break;
    case Page::kDiscs:
      TakeSearchResults();
      DrawDiscs(width, height, launcher);
      break;
    case Page::kWork:
    case Page::kDone:
      Advance(launcher);
      DrawWork(width, height, launcher);
      break;
  }
  ImGui::End();
}

void SetupScreen::DrawWelcome(float width, float height) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float u = std::min(width / 1280.0f, height / 720.0f);
  DrawBackdrop(dl, width, height, {});
  Title(dl, bold_, u, german_ ? "WILLKOMMEN" : "WELCOME");
  const float left = 48 * u, wrap = std::min(width - 2 * left, 900 * u);
  Text(dl, regular_, 24 * u, {left, 150 * u}, kWhite,
       german_ ? "Hier spielst du alle sechs Skylanders-Spiele der Xbox 360 auf dem PC. Die Einrichtung "
                 "dauert nur einen Moment: Core of Light sucht deine Discs (ISO-Dateien), zeigt, wie viel "
                 "Platz sie brauchen, und installiert die Spiele dazu."
               : "Here you play all six Xbox 360 Skylanders games on the PC. Setting up takes a moment: "
                 "Core of Light looks for your discs (ISO files), shows the room they need and installs "
                 "the games for them.",
       left + wrap);
  Text(dl, regular_, 20 * u, {left, 270 * u}, kDim,
       german_ ? "Spieldaten sind nicht dabei: es braucht deine eigenen Discs als ISO."
               : "No game data comes with it: it needs your own discs as ISOs.",
       left + wrap);
  // The language, left / right.
  const float y = 320 * u;
  const std::string label = german_ ? "Sprache" : "Language";
  Text(dl, bold_, 24 * u, {left, y + 9 * u}, kWhite, label);
  const float bx = left + 260 * u;
  Button(dl, bold_, u, bx, y, 200 * u, "<  " + std::string(german_ ? "Deutsch" : "English") + "  >", selected_ == 0);
  // Where the saves and figures go (the owner's call, 2026-10-24): one folder for every game,
  // Documents\Skylanders unless another is picked here (A: the folder browser).
  {
    const float dy = y + 70 * u;
    Text(dl, bold_, 24 * u, {left, dy + 9 * u}, kWhite, german_ ? "Spieldaten" : "Game data");
    std::string en, de;
    DataFolderLabels(en, de);
    const std::string where = german_ ? de : en;
    const float w = std::min(width - bx - left, 760 * u);
    Button(dl, bold_, u, bx, dy, w, FitStart(regular_, 20 * u, where, w - 40 * u), selected_ == 1);
    Text(dl, regular_, 18 * u, {bx, dy + 58 * u}, kDim,
         german_ ? "Spielst\xC3\xA4nde, Erfolge und Figuren aller Spiele  \xC2\xB7  A: anderer Ordner"
                 : "Saves, achievements and figures of every game  \xC2\xB7  A: another folder");
  }
  Button(dl, bold_, u, left, y + 180 * u, 380 * u, german_ ? "Weiter: Discs suchen" : "Next: find my discs",
         selected_ == 2);
  if (Up() && selected_ > 0) --selected_;
  if (Down() && selected_ < 2) ++selected_;
  if (selected_ == 0 && (Left() || Right() || Accept())) {
    german_ = !german_;
    language_ = german_ ? "german" : "english";
  } else if (selected_ == 1 && Accept()) {
    job_ = {Job::Kind::kPickDataFolder, 0, {}};
  } else if (selected_ == 2 && Accept()) {
    page_ = Page::kDiscs;
    selected_ = 0;
  }
  if (Back()) {
    finished_ = true;
    open_ = false;
  }
  float hx = 48 * u;
  const float hy = height - 52 * u;
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 160, 70), german_ ? "Weiter" : "Next");
  Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "\xC3\x9C" "berspringen" : "Skip");
}

void SetupScreen::DrawDiscs(float width, float height, const Launcher& launcher) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float u = std::min(width / 1280.0f, height / 720.0f);
  DrawBackdrop(dl, width, height, {});
  Title(dl, bold_, u, german_ ? "DEINE DISCS" : "YOUR DISCS");
  const float left = 48 * u, right = width - 48 * u;
  // Nothing is looked for unasked (the owner, 2026-10-23): A on a game chooses its ISO, then its
  // folder and the one above are looked through for the others; Y searches the whole PC.
  // How far the search is.
  {
    const int folders = search_ ? search_->folders.load() : 0, isos = search_ ? search_->isos.load() : 0;
    const bool done = search_ && search_->done;
    int games = 0;
    for (const Row& r : rows_) games += r.state != State::kNoDisc ? 1 : 0;
    char text[160];
    if (!search_) {
      std::snprintf(text, sizeof(text), "%s",
                    german_ ? "A auf einem Spiel: seine ISO w\xC3\xA4hlen. Die anderen sucht Core of Light dann im selben Ordner."
                            : "A on a game: choose its ISO. Core of Light then looks for the others in the same folder.");
    } else if (done && nearby_) {
      std::snprintf(text, sizeof(text), german_ ? "Daneben gesucht: %d von 6 Spielen zugeordnet. Stimmt eines nicht: A darauf, andere ISO w\xC3\xA4hlen."
                                                : "Looked next to it: %d of 6 games filled in. One wrong: A on it, another ISO.",
                    games);
    } else if (done) {
      std::snprintf(text, sizeof(text), german_ ? "Suche fertig: %d ISOs, %d von 6 Spielen gefunden"
                                                : "Search done: %d ISOs, %d of 6 games found",
                    isos, games);
    } else {
      std::snprintf(text, sizeof(text), german_ ? "Suche l\xC3\xA4uft ...  %d Ordner, %d ISOs"
                                                : "Searching ...  %d folders, %d ISOs",
                    folders, isos);
    }
    Text(dl, regular_, 20 * u, {left, 128 * u}, done || !search_ ? kDim : kGold, text);
  }
  // One row per game.
  const int n = int(rows_.size());
  // Six games and the folder row: a little lower than they'd like, so the button stays above the hints.
  const float row_h = 52 * u, top = 166 * u;
  for (int i = 0; i < n; ++i) {
    const Row& r = rows_[size_t(i)];
    const Game& g = AllGames()[size_t(i)];
    const float y = top + i * (row_h + 4 * u);
    const bool chosen_row = selected_ == i;
    dl->AddRectFilled({left, y}, {right, y + row_h}, kPanel, 12 * u);
    dl->AddRect({left, y}, {right, y + row_h}, chosen_row ? kGold : kEdge, 12 * u, 0, (chosen_row ? 3.0f : 1.5f) * u);
    // The check box: chosen to be set up.
    const float bx = left + 18 * u, by = y + row_h * 0.5f - 13 * u;
    if (r.state == State::kFound) {
      dl->AddRect({bx, by}, {bx + 26 * u, by + 26 * u}, kWhite, 5 * u, 0, 2.0f * u);
      if (r.chosen) dl->AddRectFilled({bx + 6 * u, by + 6 * u}, {bx + 20 * u, by + 20 * u}, kGold, 3 * u);
    }
    char year[8];
    std::snprintf(year, sizeof(year), "%d", g.year);
    Text(dl, bold_, 18 * u, {left + 60 * u, y + 8 * u}, kDim, year);
    Text(dl, bold_, 24 * u, {left + 60 * u, y + 26 * u}, kWhite, german_ ? g.name_de : g.name_en);
    const float sx = left + 360 * u;
    std::string state;
    ImU32 col = kDim;
    switch (r.state) {
      case State::kReady:
        state = german_ ? "Schon eingerichtet" : "Set up already";
        col = kGreen;
        break;
      case State::kFound: {
        // the size first, the path in what is left (a long path keeps its end)
        const std::string size = Gigabytes(r.bytes, german_);
        col = r.chosen ? kWhite : kDim;
        Text(dl, bold_, 20 * u, {sx, y + 17 * u}, col, size);
        float px = sx + TextWidth(bold_, 20 * u, size) + 16 * u;
        if (r.automatic) {
          const std::string tag = german_ ? "automatisch erkannt" : "found automatically";
          Text(dl, bold_, 16 * u, {px, y + 21 * u}, kGold, tag);
          px += TextWidth(bold_, 16 * u, tag) + 14 * u;
        }
        Text(dl, regular_, 18 * u, {px, y + 19 * u}, kDim, FitStart(regular_, 18 * u, Utf8(r.iso), right - px - 18 * u));
        break;
      }
      default:
        state = !search_ ? (german_ ? "Noch keine Disc  \xC2\xB7  A: ISO w\xC3\xA4hlen" : "No disc yet  \xC2\xB7  A: choose its ISO")
                : search_->done ? (german_ ? "Keine Disc gefunden  \xC2\xB7  A: ISO w\xC3\xA4hlen" : "No disc found  \xC2\xB7  A: choose its ISO")
                                : (german_ ? "Wird gesucht ..." : "Looking ...");
        break;
    }
    if (!state.empty()) {
      Text(dl, regular_, 20 * u, {sx, y + 17 * u}, col, FitStart(regular_, 20 * u, state, right - sx - 18 * u));
    }
  }
  // The room the chosen ones need, and what is free where they go.
  const uint64_t needed = NeededBytes();
  std::optional<uint64_t> free;
  std::string drive;
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (!rows_[i].chosen) continue;
    const fs::path folder = launcher.found && i < launcher.found->size() && !(*launcher.found)[i].exe.empty()
                                ? (*launcher.found)[i].exe.parent_path()
                                : GamesFolder();
    free = FreeBytes(folder);
    drive = Utf8(folder.root_name());
    break;
  }
  const bool room = !needed || !free || *free >= needed;
  {
    const float y = top + n * (row_h + 4 * u);
    const bool on = selected_ == n;
    dl->AddRectFilled({left, y}, {right, y + row_h}, kPanel, 12 * u);
    dl->AddRect({left, y}, {right, y + row_h}, on ? kGold : kEdge, 12 * u, 0, (on ? 3.0f : 1.5f) * u);
    Text(dl, bold_, 22 * u, {left + 22 * u, y + 17 * u}, kWhite, german_ ? "Spiele-Ordner" : "Games folder");
    const bool standard = folder_ <= 0 || folders_.empty();
    const std::string where = (standard ? (german_ ? "Standard: " : "Default: ") : std::string()) + Utf8(GamesFolder());
    const std::optional<uint64_t> folder_free = FreeBytes(GamesFolder());
    const std::string room_text = folder_free ? "  \xC2\xB7  " + Gigabytes(*folder_free, german_) + (german_ ? " frei" : " free")
                                              : std::string();
    const std::string value = (on && folders_.size() > 1 ? "<  " : "") + where + room_text + (on && folders_.size() > 1 ? "  >" : "");
    Text(dl, regular_, 20 * u, {left + 360 * u, y + 17 * u}, on ? kGold : kDim,
         FitStart(regular_, 20 * u, value, right - left - 380 * u));
  }
  const float sy = top + (n + 1) * (row_h + 4 * u) + 10 * u;
  if (needed) {
    const std::string text = (german_ ? "Braucht " : "Needs ") + Gigabytes(needed, german_) + "  \xC2\xB7  " +
                             (free ? (german_ ? "frei auf " : "free on ") + drive + " " + Gigabytes(*free, german_)
                                   : std::string());
    Text(dl, bold_, 22 * u, {left, sy}, room ? kGreen : kRed, text);
    if (!room) {
      Text(dl, regular_, 19 * u, {left, sy + 30 * u}, kRed,
           german_ ? "Nicht genug Platz: weniger Spiele w\xC3\xA4hlen oder auf dem Laufwerk Platz schaffen."
                   : "Not enough room: choose fewer games or free some space on the drive.");
    }
  }
  int chosen = 0;
  for (const Row& r : rows_) chosen += r.chosen ? 1 : 0;
  const std::string go = chosen ? (german_ ? "Einrichten" : "Set up")
                                : (german_ ? "Ohne Disc weiter" : "Continue without a disc");
  const float go_y = sy + (room ? 44 : 64) * u;
  if (!message_.empty()) Text(dl, regular_, 19 * u, {left + 340 * u, go_y + 12 * u}, kRed, message_);
  Button(dl, bold_, u, left, go_y, 320 * u, go, selected_ == n + 1, room);

  if (Up() && selected_ > 0) --selected_;
  if (Down() && selected_ < n + 1) ++selected_;
  if (selected_ == n && (Left() || Right())) ChooseGameFolder(folder_ + (Left() ? -1 : 1));
  if (Accept()) {
    if (selected_ == n) {
      job_ = {Job::Kind::kPickFolder, 0, {}};  // any folder (left / right: the suggestions)
    } else if (selected_ < n) {
      if (rows_[size_t(selected_)].state != State::kReady) {
        job_ = {Job::Kind::kPickIso, size_t(selected_), {}};
        pick_for_ = selected_;
        message_.clear();
      }
    } else if (room) {
      if (search_) search_->stop = true;
      if (!chosen) {
        finished_ = true;
        open_ = false;
      } else {
        for (Row& r : rows_) {
          if (r.chosen) r.state = State::kWaiting;
        }
        page_ = Page::kWork;
      }
    }
  }
  if ((Once(ImGuiKey_GamepadFaceLeft) || Once(ImGuiKey_X)) && selected_ < n) {  // on / off
    Row& r = rows_[size_t(selected_)];
    if (r.state == State::kFound) r.chosen = !r.chosen;
  }
  if (Once(ImGuiKey_GamepadFaceUp) || Once(ImGuiKey_Y)) {  // search again
    if (search_) search_->stop = true;
    for (Row& r : rows_) {
      if (r.state == State::kFound && r.automatic) r = Row{};
    }
    search_ = StartDiscSearch();
    taken_ = 0;
    nearby_ = false;
  }
  if (Back()) {
    page_ = Page::kWelcome;
    selected_ = 1;
  }
  float hx = 48 * u;
  const float hy = height - 52 * u;
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 160, 70),
            selected_ < n ? (german_ ? "ISO w\xC3\xA4hlen" : "Choose its ISO")
            : selected_ == n ? (german_ ? "Anderer Ordner" : "Another folder")
                             : go);
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "X", Rgba(60, 110, 220), german_ ? "An / aus" : "On / off");
  hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Y", Rgba(200, 170, 40), german_ ? "Ganzen PC durchsuchen" : "Search the whole PC");
  Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
}

// The queue: each chosen game installed (when it isn't) and its disc set up, one after the other.
// A step is handed to the launcher as a Job; its end is seen in the launcher's state.
void SetupScreen::Advance(const Launcher& launcher) {
  if (job_.kind != Job::Kind::kNone || page_ != Page::kWork || !launcher.found || !launcher.import) return;
  const std::vector<Found>& found = *launcher.found;
  if (current_ >= 0 && size_t(current_) < rows_.size() && size_t(current_) < found.size()) {
    Row& r = rows_[size_t(current_)];
    if (r.state == State::kInstalling) {
      if (launcher.installing) return;
      if (found[size_t(current_)].exe.empty()) {
        r.state = State::kFailed;
        r.note = GameInstallError(size_t(current_));
        if (r.note.empty()) r.note = german_ ? "Installation abgebrochen" : "Installation stopped";
        current_ = -1;
      } else {
        r.state = State::kImporting;
        job_ = {Job::Kind::kImport, size_t(current_), r.iso};
        return;
      }
    } else if (r.state == State::kImporting) {
      const DiscImport::Phase phase = launcher.import->phase();
      if (launcher.import->running() || phase == DiscImport::Phase::kReading ||
          phase == DiscImport::Phase::kUnpacking) {
        return;
      }
      r.state = phase == DiscImport::Phase::kDone ? State::kDone : State::kFailed;
      r.note = launcher.import->message();
      current_ = -1;
    }
  }
  if (current_ >= 0) return;
  for (size_t i = 0; i < rows_.size() && i < found.size() && !stopping_; ++i) {
    Row& r = rows_[i];
    if (r.state != State::kWaiting) continue;
    current_ = int(i);
    if (found[i].exe.empty()) {
      if (!launcher.setups || i >= launcher.setups->size() || (*launcher.setups)[i].empty()) {
        r.state = State::kFailed;
        r.note = german_ ? "Kein Setup gefunden (keine Verbindung zu GitHub?)" : "No setup found (no connection to GitHub?)";
        current_ = -1;
        continue;
      }
      r.state = State::kInstalling;
      job_ = {Job::Kind::kInstall, i, {}};
    } else {
      r.state = State::kImporting;
      job_ = {Job::Kind::kImport, i, r.iso};
    }
    return;
  }
  page_ = Page::kDone;
}

void SetupScreen::DrawWork(float width, float height, const Launcher& launcher) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float u = std::min(width / 1280.0f, height / 720.0f);
  DrawBackdrop(dl, width, height, {});
  const bool done = page_ == Page::kDone;
  Title(dl, bold_, u, done ? (german_ ? "FERTIG" : "DONE") : (german_ ? "EINRICHTEN" : "SETTING UP"));
  const float left = 48 * u, right = width - 48 * u;
  float y = 140 * u;
  int ok = 0, failed = 0;
  for (size_t i = 0; i < rows_.size(); ++i) {
    const Row& r = rows_[i];
    if (r.state != State::kWaiting && r.state != State::kInstalling && r.state != State::kImporting &&
        r.state != State::kDone && r.state != State::kFailed) {
      continue;
    }
    const Game& g = AllGames()[i];
    const float row_h = 62 * u;
    dl->AddRectFilled({left, y}, {right, y + row_h}, kPanel, 12 * u);
    dl->AddRect({left, y}, {right, y + row_h}, int(i) == current_ ? kGold : kEdge, 12 * u, 0, 1.5f * u);
    Text(dl, bold_, 24 * u, {left + 20 * u, y + 17 * u}, kWhite, german_ ? g.name_de : g.name_en);
    const float sx = left + 340 * u;
    std::string state;
    ImU32 col = kDim;
    switch (r.state) {
      case State::kWaiting:
        state = stopping_ ? (german_ ? "Abgebrochen" : "Stopped") : (german_ ? "Wartet" : "Waiting");
        break;
      case State::kInstalling:
        state = german_ ? "Wird installiert ..." : "Installing ...";
        col = kGold;
        break;
      case State::kImporting: {
        const float p = std::clamp(launcher.import ? launcher.import->progress() : 0.0f, 0.0f, 1.0f);
        char text[64];
        std::snprintf(text, sizeof(text), german_ ? "Disc wird eingelesen ...  %d %%" : "Setting up the disc ...  %d %%",
                      int(p * 100));
        state = text;
        col = kGold;
        const float bar_x = sx, bar_w = right - sx - 24 * u;
        dl->AddRectFilled({bar_x, y + 44 * u}, {bar_x + bar_w, y + 52 * u}, Rgba(40, 50, 100), 4 * u);
        dl->AddRectFilled({bar_x, y + 44 * u}, {bar_x + bar_w * p, y + 52 * u}, kGold, 4 * u);
        break;
      }
      case State::kDone:
        state = german_ ? "Bereit zum Spielen" : "Ready to play";
        col = kGreen;
        ++ok;
        break;
      default:
        state = r.note;
        col = kRed;
        ++failed;
        break;
    }
    Text(dl, regular_, 20 * u, {sx, y + 12 * u}, col, FitStart(regular_, 20 * u, state, right - sx - 24 * u));
    y += row_h + 8 * u;
  }
  float hx = 48 * u;
  const float hy = height - 52 * u;
  if (done) {
    char text[160];
    std::snprintf(text, sizeof(text),
                  german_ ? "%d Spiel(e) bereit%s. A: zu den Spielen" : "%d game(s) ready%s. A: to the games", ok,
                  failed ? (german_ ? ", bei den roten hat es nicht geklappt (Karte: A versucht es noch einmal)"
                                    : ", the red ones failed (their card's A tries again)")
                         : "");
    Text(dl, bold_, 22 * u, {left, y + 16 * u}, ok ? kGreen : kDim, text, right);
    if (Accept() || Back()) {
      finished_ = true;
      open_ = false;
    }
    Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 160, 70), german_ ? "Zu den Spielen" : "To the games");
  } else {
    if (Back() && !stopping_) {
      // Stops after the step running now: the disc being unpacked stops at once (it isn't set up then).
      stopping_ = true;
      if (current_ >= 0 && rows_[size_t(current_)].state == State::kImporting) job_ = {Job::Kind::kCancel, 0, {}};
    }
    Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60),
         stopping_ ? (german_ ? "Wird angehalten ..." : "Stopping ...") : (german_ ? "Abbrechen" : "Stop"));
  }
}

}  // namespace coreoflight
