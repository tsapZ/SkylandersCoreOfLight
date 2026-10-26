#include "saves_ui.h"
#include "hints.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "games.h"
#include "imgui.h"
#include "nxe_art.h"
#include "profiles.h"

namespace coreoflight {
namespace {

ImU32 Rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

const ImU32 kGold = Rgba(255, 204, 64);
const ImU32 kWhite = Rgba(255, 255, 255);
const ImU32 kDim = Rgba(180, 190, 215);
const ImU32 kPanel = Rgba(24, 30, 70, 235);
const ImU32 kChosen = Rgba(60, 70, 140, 240);

float TextWidth(ImFont* font, float size, const std::string& text) {
  return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
}

void Text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const std::string& text,
          float wrap = 0.0f) {
  dl->AddText(font, size, pos, col, text.c_str(), nullptr, wrap);
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

// A local date's day number, to tell today and yesterday.
int64_t DayNumber(const SYSTEMTIME& t) {
  SYSTEMTIME day = t;
  day.wHour = day.wMinute = day.wSecond = day.wMilliseconds = 0;
  FILETIME ft{};
  SystemTimeToFileTime(&day, &ft);
  return int64_t((uint64_t(ft.dwHighDateTime) << 32 | ft.dwLowDateTime) / 864000000000ULL);
}

// A Windows file time as the player reads it: "heute, 22:32", "gestern, 08:15" or "23.10.2026, 12:30"
// ("today, 22:32", "yesterday, ...", "2026-10-23, 12:30").
std::string When(uint64_t file_time, bool german) {
  FILETIME ft{DWORD(file_time & 0xFFFFFFFF), DWORD(file_time >> 32)};
  SYSTEMTIME utc{}, local{}, now{};
  if (!file_time || !FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
    return "?";
  }
  GetLocalTime(&now);
  char clock[8];
  std::snprintf(clock, sizeof(clock), "%02u:%02u", local.wHour, local.wMinute);
  const int64_t days = DayNumber(now) - DayNumber(local);
  if (days == 0) return std::string(german ? "heute, " : "today, ") + clock;
  if (days == 1) return std::string(german ? "gestern, " : "yesterday, ") + clock;
  char text[40];
  if (german) {
    std::snprintf(text, sizeof(text), "%02u.%02u.%04u, %s", local.wDay, local.wMonth, local.wYear, clock);
  } else {
    std::snprintf(text, sizeof(text), "%04u-%02u-%02u, %s", local.wYear, local.wMonth, local.wDay, clock);
  }
  return text;
}

std::string SlotName(const SaveSlot& s, bool german) {
  if (s.options) return german ? "Einstellungen des Spiels" : "The game's settings";
  if (s.number) return (german ? "Spielstand " : "Save ") + std::to_string(s.number);
  return s.name;
}

// Where an older version comes from, in words.
std::string CopyKind(const SaveCopy& c, bool german) {
  if (c.previous) {
    return german ? "Vom Spiel: der Stand vor dem letzten Speichern" : "From the game: the save before its last save";
  }
  if (c.before_restore) {
    return german ? "Dein Stand, bevor eine \xC3\xA4ltere Version zur\xC3\xBC" "ckgeholt wurde"
                  : "Your save before an older version was brought back";
  }
  if (c.automatic) return german ? "Automatisch vom Hub gesichert" : "Kept by the launcher on its own";
  return german ? "Von dir gesichert" : "Backed up by you";
}

// When a copy was made: its folder's last write, as a Windows file time (0 if unknown).
uint64_t FolderTime(const std::filesystem::path& folder) {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(folder.c_str(), GetFileExInfoStandard, &data)) return 0;
  return uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32 | data.ftLastWriteTime.dwLowDateTime;
}

std::string VersionCount(size_t n, bool german) {
  if (german) return n == 1 ? "1 \xC3\xA4ltere Version" : std::to_string(n) + " \xC3\xA4ltere Versionen";
  return n == 1 ? "1 older version" : std::to_string(n) + " older versions";
}

bool Pressed(ImGuiKey pad, ImGuiKey key, bool repeat = false) {
  return ImGui::IsKeyPressed(pad, repeat) || ImGui::IsKeyPressed(key, repeat);
}

bool Inside(const ImGuiIO& io, ImVec2 a, ImVec2 b) {
  return io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y;
}

}  // namespace

void SavesScreen::Init(ImFont* regular, ImFont* bold, bool german) {
  regular_ = regular;
  bold_ = bold;
  german_ = german;
}

void SavesScreen::Open(size_t game, const std::filesystem::path& exe, std::filesystem::path background) {
  game_ = game;
  exe_ = exe;
  background_ = std::move(background);
  open_ = true;
  slot_ = copy_ = column_ = 0;
  confirm_ = false;
  message_.clear();
  Load();
}

void SavesScreen::Load() {
  slots_ = ListSaves(ActiveProfile(), exe_);
  slot_ = std::clamp(slot_, 0, std::max(0, int(slots_.size()) - 1));
  copy_counts_.clear();
  for (const SaveSlot& s : slots_) {
    copy_counts_.push_back(ListCopies(ActiveProfile(), AllGames()[game_], exe_, s).size());
  }
  copies_ = slots_.empty() ? std::vector<SaveCopy>{}
                           : ListCopies(ActiveProfile(), AllGames()[game_], exe_, slots_[size_t(slot_)]);
  copy_ = std::clamp(copy_, 0, std::max(0, int(copies_.size()) - 1));
  if (copies_.empty()) column_ = 0;
}

void SavesScreen::Message(const std::string& text) {
  message_ = text;
  message_until_ = ImGui::GetTime() + 5.0;
}

void SavesScreen::Draw(float width, float height) {
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("saves", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float u = UiScale(width, height);
  DrawBackdrop(dl, width, height, background_);
  const Game& game = AllGames()[game_];
  Text(dl, bold_, 22 * u, {48 * u, 30 * u}, kDim, german_ ? "SPIELST\xC3\x84NDE" : "SAVES");
  Text(dl, bold_, 44 * u, {46 * u, 52 * u}, kGold, german_ ? game.name_de : game.name_en);
  const std::string profile = (german_ ? "Profil: " : "Profile: ") + ActiveProfile().name;
  Text(dl, regular_, 20 * u, {width - 48 * u - TextWidth(regular_, 20 * u, profile), 64 * u}, kDim, profile);
  // What the screen is for, in one line.
  Text(dl, regular_, 19 * u, {48 * u, 106 * u}, kDim,
       german_ ? "Hier sicherst du deine Spielst\xC3\xA4nde und holst bei Bedarf eine \xC3\xA4ltere Version zur\xC3\xBC" "ck."
               : "Back up your saves here, or bring back an older version when you need it.",
       width - 96 * u);

  const float left = 48 * u, top = 150 * u, bottom = height - 80 * u;
  const float col_w = (width - 2 * left - 24 * u) * 0.5f;
  const bool asking = confirm_;
  const bool back = Pressed(ImGuiKey_GamepadFaceRight, ImGuiKey_Escape);
  bool backup_now = false;

  if (!asking) {
    const int n = column_ == 0 ? int(slots_.size()) : int(copies_.size());
    int& at = column_ == 0 ? slot_ : copy_;
    const int before = slot_;
    if (n && (Pressed(ImGuiKey_GamepadDpadUp, ImGuiKey_UpArrow, true) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickUp, true))) {
      at = (at + n - 1) % n;
    }
    if (n && (Pressed(ImGuiKey_GamepadDpadDown, ImGuiKey_DownArrow, true) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickDown, true))) {
      at = (at + 1) % n;
    }
    if (Pressed(ImGuiKey_GamepadDpadLeft, ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft, false)) column_ = 0;
    if ((Pressed(ImGuiKey_GamepadDpadRight, ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight, false)) &&
        !copies_.empty()) {
      column_ = 1;
    }
    if (slot_ != before) {
      copy_ = 0;
      Load();
    }
    if (!slots_.empty() && Pressed(ImGuiKey_GamepadFaceLeft, ImGuiKey_X)) backup_now = true;
    // A: on a save to its older versions, on a version the question.
    if (Pressed(ImGuiKey_GamepadFaceDown, ImGuiKey_Enter)) {
      if (column_ == 0 && !copies_.empty()) {
        column_ = 1;
      } else if (column_ == 1 && !copies_.empty()) {
        confirm_ = true;
      }
    }
    // Y: the backups' folder in Explorer.
    if (Pressed(ImGuiKey_GamepadFaceUp, ImGuiKey_Y)) {
      std::error_code ec;
      const std::filesystem::path dir = BackupsDir(ActiveProfile(), game);
      std::filesystem::create_directories(dir, ec);
      ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    // B: from the versions back to the saves, from the saves out.
    if (back) {
      if (column_ == 1) {
        column_ = 0;
      } else {
        open_ = false;
      }
    }
  }

  // The saves: the games first, the game's own settings file apart at the end.
  Text(dl, bold_, 24 * u, {left, top}, column_ == 0 ? kGold : kDim, german_ ? "Deine Spielst\xC3\xA4nde" : "Your saves");
  const float list_top = top + 42 * u;
  const float row_h = 76 * u;
  if (slots_.empty()) {
    Text(dl, regular_, 21 * u, {left, list_top}, kDim,
         german_ ? "Noch kein Spielstand: Das Spiel legt ihn beim ersten Speichern an."
                 : "No save yet: the game makes one when it first saves.",
         col_w);
  }
  float y = list_top;
  for (int i = 0; i < int(slots_.size()); ++i) {
    const SaveSlot& s = slots_[size_t(i)];
    if (s.options && i > 0) y += 14 * u;  // apart from the games
    if (y + row_h > bottom) break;
    const ImVec2 a{left, y}, b{left + col_w, y + row_h};
    if (!asking && Inside(io, a, b) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      column_ = 0;
      if (slot_ != i) {
        slot_ = i;
        copy_ = 0;
        Load();
      }
    }
    const bool chosen = i == slot_;
    dl->AddRectFilled(a, b, chosen ? kChosen : kPanel, 16 * u);
    if (chosen) dl->AddRect(a, b, column_ == 0 ? kGold : Rgba(255, 255, 255, 120), 16 * u, 0, (column_ == 0 ? 3 : 1.5f) * u);
    Text(dl, bold_, (s.options ? 21 : 24) * u, {a.x + 18 * u, y + 10 * u}, s.options ? kDim : kWhite, SlotName(s, german_));
    const std::string saved = (german_ ? "Zuletzt gespeichert: " : "Last saved: ") + When(s.written, german_);
    Text(dl, regular_, 18 * u, {a.x + 18 * u, y + 44 * u}, kDim, saved);
    // How many older versions there are, at the right.
    const size_t count = size_t(i) < copy_counts_.size() ? copy_counts_[size_t(i)] : 0;
    if (count) {
      const std::string pill = VersionCount(count, german_);
      const float pw = TextWidth(regular_, 16 * u, pill) + 20 * u;
      const ImVec2 p0{b.x - 16 * u - pw, y + 12 * u}, p1{b.x - 16 * u, y + 38 * u};
      dl->AddRectFilled(p0, p1, Rgba(255, 255, 255, 30), 13 * u);
      Text(dl, regular_, 16 * u, {p0.x + 10 * u, p0.y + 4 * u}, kWhite, pill);
    }
    y += row_h + 8 * u;
  }

  // The older versions of the chosen save, under a "back up now" button.
  const float rx = left + col_w + 24 * u;
  if (!slots_.empty()) {
    const std::string title = german_ ? "\xC3\x84ltere Versionen von " + SlotName(slots_[size_t(slot_)], true)
                                      : "Older versions of " + SlotName(slots_[size_t(slot_)], false);
    Text(dl, bold_, 24 * u, {rx, top}, column_ == 1 ? kGold : kDim, title);
    const ImVec2 ba{rx, list_top}, bb{rx + col_w, list_top + 58 * u};
    const bool hover = !asking && Inside(io, ba, bb);
    dl->AddRectFilled(ba, bb, hover ? Rgba(50, 120, 220, 120) : Rgba(50, 120, 220, 70), 14 * u);
    dl->AddRect(ba, bb, Rgba(90, 160, 255, 200), 14 * u, 0, 1.5f * u);
    float bx = Hint(dl, bold_, 21 * u, {ba.x + 16 * u, ba.y + 8 * u}, "X", Rgba(50, 120, 220),
                    german_ ? "Jetzt sichern" : "Back up now");
    (void)bx;
    Text(dl, regular_, 16 * u, {ba.x + 16 * u, ba.y + 34 * u}, kDim,
         german_ ? "Legt eine Kopie des jetzigen Stands an." : "Keeps a copy of the save as it is now.");
    if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) backup_now = true;
  }
  const float versions_top = list_top + 72 * u;
  if (!slots_.empty() && copies_.empty()) {
    Text(dl, regular_, 20 * u, {rx, versions_top}, kDim,
         german_ ? "Noch keine \xC3\xA4lteren Versionen. Das Spiel legt beim n\xC3\xA4" "chsten Speichern eine an, "
                   "oder du sicherst jetzt mit X."
                 : "No older versions yet. The game keeps one at its next save, or back it up now with X.",
         col_w);
  }
  const float vrow_h = 66 * u;
  for (int i = 0; i < int(copies_.size()); ++i) {
    const float vy = versions_top + i * (vrow_h + 8 * u);
    if (vy + vrow_h > bottom) break;
    const ImVec2 a{rx, vy}, b{rx + col_w, vy + vrow_h};
    if (!asking && Inside(io, a, b) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      if (column_ == 1 && copy_ == i) confirm_ = true;
      column_ = 1;
      copy_ = i;
    }
    const bool chosen = column_ == 1 && i == copy_;
    dl->AddRectFilled(a, b, chosen ? kChosen : kPanel, 14 * u);
    if (chosen) dl->AddRect(a, b, kGold, 14 * u, 0, 3 * u);
    const SaveCopy& c = copies_[size_t(i)];
    Text(dl, bold_, 22 * u, {a.x + 18 * u, vy + 8 * u}, kWhite, (german_ ? "Stand von " : "As of ") + When(c.written, german_));
    // Where it comes from, and when it was kept (two copies of one save state differ only there).
    std::string kind = CopyKind(c, german_);
    if (const uint64_t made = FolderTime(c.folder)) kind += " \xC2\xB7 " + When(made, german_);
    Text(dl, regular_, 17 * u, {a.x + 18 * u, vy + 38 * u}, c.previous ? Rgba(150, 210, 255) : kDim, kind, col_w - 36 * u);
  }

  if (backup_now && !slots_.empty()) {
    // The name first: Load() makes the list anew.
    const std::string name = SlotName(slots_[size_t(slot_)], german_);
    const bool ok = BackupSave(ActiveProfile(), game, slots_[size_t(slot_)]);
    Load();
    Message(ok ? name + (german_ ? " gesichert" : " backed up")
               : (german_ ? "Konnte nicht sichern: " : "Could not back it up: ") + LastSaveError());
  }

  // Hints for what is chosen, the message, or the question.
  float hx = left;
  const float hy = height - 52 * u;
  if (!asking) {
    if (column_ == 0) {
      if (!copies_.empty()) {
        hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 170, 70), german_ ? "\xC3\x84ltere Versionen" : "Older versions");
      }
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "X", Rgba(50, 120, 220), german_ ? "Jetzt sichern" : "Back up now");
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Y", Rgba(220, 180, 40), german_ ? "Ordner \xC3\xB6" "ffnen" : "Open the folder");
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
    } else {
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 170, 70), german_ ? "Diese Version zur\xC3\xBC" "ckholen" : "Bring this one back");
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "X", Rgba(50, 120, 220), german_ ? "Jetzt sichern" : "Back up now");
      hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zu den Spielst\xC3\xA4nden" : "To the saves");
    }
  } else {
    const SaveSlot& s = slots_[size_t(slot_)];
    const SaveCopy& c = copies_[size_t(copy_)];
    const float pw = std::min(720 * u, width - 96 * u), ph = 240 * u;
    const ImVec2 a{(width - pw) * 0.5f, (height - ph) * 0.5f}, b{a.x + pw, a.y + ph};
    dl->AddRectFilled({0, 0}, {width, height}, Rgba(0, 0, 0, 140));
    dl->AddRectFilled(a, b, Rgba(20, 26, 64, 250), 18 * u);
    dl->AddRect(a, b, kGold, 18 * u, 0, 3 * u);
    Text(dl, bold_, 28 * u, {a.x + 28 * u, a.y + 22 * u}, kGold,
         german_ ? "\xC3\x84ltere Version zur\xC3\xBC" "ckholen?" : "Bring back the older version?");
    const std::string what = german_ ? SlotName(s, true) + " wird durch den Stand von " + When(c.written, true) + " ersetzt."
                                     : SlotName(s, false) + " is replaced with the save as of " + When(c.written, false) + ".";
    Text(dl, regular_, 21 * u, {a.x + 28 * u, a.y + 70 * u}, kWhite, what, pw - 56 * u);
    Text(dl, regular_, 19 * u, {a.x + 28 * u, a.y + 110 * u}, kDim,
         german_ ? "Dein jetziger Stand wird vorher gesichert, du kannst also jederzeit zur\xC3\xBC" "ck."
                 : "Your save as it is now is kept first, so you can always go back.",
         pw - 56 * u);
    float bx = a.x + 28 * u;
    bx = Hint(dl, bold_, 22 * u, {bx, b.y - 52 * u}, "A", Rgba(70, 170, 70), german_ ? "Zur\xC3\xBC" "ckholen" : "Bring it back");
    bx = Hint(dl, bold_, 22 * u, {bx, b.y - 52 * u}, "B", Rgba(200, 60, 60), german_ ? "Abbrechen" : "Cancel");
    if (Pressed(ImGuiKey_GamepadFaceDown, ImGuiKey_Enter)) {
      const std::string name = SlotName(s, german_);  // before Load() makes the list anew
      const bool ok = RestoreSave(ActiveProfile(), game, s, c);
      confirm_ = false;
      Load();
      Message(ok ? name + (german_ ? " zur\xC3\xBC" "ckgeholt" : " brought back")
                 : (german_ ? "Konnte nicht zur\xC3\xBC" "ckholen: " : "Could not bring it back: ") + LastSaveError());
    } else if (back) {
      confirm_ = false;
    }
  }
  if (!message_.empty() && ImGui::GetTime() < message_until_) {
    Text(dl, regular_, 20 * u, {hx + 10 * u, hy}, Rgba(110, 220, 110), message_);
  }
  ImGui::End();
}

}  // namespace coreoflight
