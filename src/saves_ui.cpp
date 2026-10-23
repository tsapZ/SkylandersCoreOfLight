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
const ImU32 kEdge = Rgba(90, 100, 150);

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

// A Windows file time as the local "23.10.2026 12:30" (German) or "2026-10-23 12:30".
std::string When(uint64_t file_time, bool german) {
  FILETIME ft{DWORD(file_time & 0xFFFFFFFF), DWORD(file_time >> 32)};
  SYSTEMTIME utc{}, local{};
  if (!file_time || !FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
    return "?";
  }
  char text[32];
  if (german) {
    std::snprintf(text, sizeof(text), "%02u.%02u.%04u %02u:%02u", local.wDay, local.wMonth, local.wYear,
                  local.wHour, local.wMinute);
  } else {
    std::snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u", local.wYear, local.wMonth, local.wDay,
                  local.wHour, local.wMinute);
  }
  return text;
}

std::string SlotName(const SaveSlot& s, bool german) {
  if (s.options) return german ? "Optionen" : "Options";
  if (s.number) return (german ? "Spielstand " : "Save ") + std::to_string(s.number);
  return s.name;
}

std::string CopyName(const SaveCopy& c, bool german) {
  if (c.previous) return german ? "Vorherige Version (vom Spiel)" : "Previous version (the game's)";
  if (c.before_restore) return german ? "Vor dem Wiederherstellen" : "Before a restore";
  if (c.automatic) return german ? "Automatisch gesichert" : "Automatic copy";
  return german ? "Sicherung" : "Backup";
}

bool Pressed(ImGuiKey pad, ImGuiKey key, bool repeat = false) {
  return ImGui::IsKeyPressed(pad, repeat) || ImGui::IsKeyPressed(key, repeat);
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
  const float u = std::min(width / 1280.0f, height / 720.0f);
  DrawBackdrop(dl, width, height, background_);
  const Game& game = AllGames()[game_];
  Text(dl, bold_, 22 * u, {48 * u, 30 * u}, kDim, german_ ? "SPIELST\xC3\x84NDE" : "SAVES");
  Text(dl, bold_, 44 * u, {46 * u, 52 * u}, kGold, german_ ? game.name_de : game.name_en);
  const std::string profile = (german_ ? "Profil: " : "Profile: ") + ActiveProfile().name;
  Text(dl, regular_, 20 * u, {width - 48 * u - TextWidth(regular_, 20 * u, profile), 64 * u}, kDim, profile);

  const float left = 48 * u, top = 130 * u, bottom = height - 80 * u;
  const float col_w = (width - 2 * left - 24 * u) * 0.5f;
  const float row_h = 64 * u;
  const bool asking = confirm_;
  const bool back = Pressed(ImGuiKey_GamepadFaceRight, ImGuiKey_Escape);

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
    // X: back the chosen save up.
    if (!slots_.empty() && Pressed(ImGuiKey_GamepadFaceLeft, ImGuiKey_X)) {
      const bool ok = BackupSave(ActiveProfile(), game, slots_[size_t(slot_)]);
      Load();
      Message(ok ? (german_ ? "Gesichert" : "Backed up")
                 : (german_ ? "Konnte nicht sichern: " : "Could not back it up: ") + LastSaveError());
    }
    // A: on a save to its copies, on a copy the question.
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
    if (back) open_ = false;
  }

  // The saves.
  Text(dl, bold_, 24 * u, {left, top}, column_ == 0 ? kGold : kDim, german_ ? "Spielst\xC3\xA4nde" : "Saves");
  const float list_top = top + 44 * u;
  if (slots_.empty()) {
    Text(dl, regular_, 21 * u, {left, list_top}, kDim,
         german_ ? "Noch kein Spielstand: Das Spiel legt ihn beim ersten Speichern an."
                 : "No save yet: the game makes one when it first saves.",
         left + col_w);
  }
  for (int i = 0; i < int(slots_.size()); ++i) {
    const float y = list_top + i * (row_h + 8 * u);
    if (y + row_h > bottom) break;
    const ImVec2 a{left, y}, b{left + col_w, y + row_h};
    const bool hovered = !asking && io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      column_ = 0;
      if (slot_ != i) {
        slot_ = i;
        copy_ = 0;
        Load();
      }
    }
    const bool chosen = i == slot_;
    dl->AddRectFilled(a, b, chosen ? Rgba(60, 70, 140, 240) : kPanel, 16 * u);
    if (chosen) dl->AddRect(a, b, column_ == 0 ? kGold : Rgba(255, 255, 255, 120), 16 * u, 0, (column_ == 0 ? 3 : 1.5f) * u);
    const SaveSlot& s = slots_[size_t(i)];
    Text(dl, bold_, 23 * u, {a.x + 18 * u, y + 8 * u}, s.options ? kDim : kWhite, SlotName(s, german_));
    Text(dl, regular_, 18 * u, {a.x + 18 * u, y + 36 * u}, kDim,
         (german_ ? "gespeichert " : "saved ") + When(s.written, german_));
  }

  // The copies of the chosen save.
  const float rx = left + col_w + 24 * u;
  Text(dl, bold_, 24 * u, {rx, top}, column_ == 1 ? kGold : kDim, german_ ? "Sicherungen" : "Copies");
  if (!slots_.empty() && copies_.empty()) {
    Text(dl, regular_, 21 * u, {rx, list_top}, kDim,
         german_ ? "Noch keine Sicherung. X: jetzt sichern" : "No copy yet. X: back it up now", rx + col_w);
  }
  for (int i = 0; i < int(copies_.size()); ++i) {
    const float y = list_top + i * (row_h + 8 * u);
    if (y + row_h > bottom) break;
    const ImVec2 a{rx, y}, b{rx + col_w, y + row_h};
    const bool hovered = !asking && io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      if (column_ == 1 && copy_ == i) confirm_ = true;
      column_ = 1;
      copy_ = i;
    }
    const bool chosen = column_ == 1 && i == copy_;
    dl->AddRectFilled(a, b, chosen ? Rgba(60, 70, 140, 240) : kPanel, 16 * u);
    if (chosen) dl->AddRect(a, b, kGold, 16 * u, 0, 3 * u);
    const SaveCopy& c = copies_[size_t(i)];
    Text(dl, bold_, 23 * u, {a.x + 18 * u, y + 8 * u}, c.previous ? Rgba(150, 210, 255) : kWhite, CopyName(c, german_));
    Text(dl, regular_, 18 * u, {a.x + 18 * u, y + 36 * u}, kDim, (german_ ? "Stand vom " : "as of ") + When(c.written, german_));
  }

  // Hints, the message, or the question.
  float hx = left;
  const float hy = height - 52 * u;
  if (!asking) {
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "X", Rgba(50, 120, 220), german_ ? "Sichern" : "Back up");
    if (column_ == 1) hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 170, 70), german_ ? "Wiederherstellen" : "Restore");
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Y", Rgba(220, 180, 40), german_ ? "Ordner" : "Folder");
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
  } else {
    const SaveSlot& s = slots_[size_t(slot_)];
    const SaveCopy& c = copies_[size_t(copy_)];
    const float pw = std::min(700 * u, width - 96 * u), ph = 220 * u;
    const ImVec2 a{(width - pw) * 0.5f, (height - ph) * 0.5f}, b{a.x + pw, a.y + ph};
    dl->AddRectFilled({0, 0}, {width, height}, Rgba(0, 0, 0, 140));
    dl->AddRectFilled(a, b, Rgba(20, 26, 64, 250), 18 * u);
    dl->AddRect(a, b, kGold, 18 * u, 0, 3 * u);
    const std::string q = german_ ? SlotName(s, true) + " durch diese Version ersetzen?"
                                  : "Replace " + SlotName(s, false) + " with this version?";
    Text(dl, bold_, 28 * u, {a.x + 28 * u, a.y + 22 * u}, kGold, q);
    Text(dl, regular_, 20 * u, {a.x + 28 * u, a.y + 68 * u}, kWhite,
         CopyName(c, german_) + (german_ ? ", Stand vom " : ", as of ") + When(c.written, german_));
    Text(dl, regular_, 19 * u, {a.x + 28 * u, a.y + 102 * u}, kDim,
         german_ ? "Der jetzige Stand wird vorher gesichert." : "The save as it is now is backed up first.",
         a.x + pw - 28 * u);
    float bx = a.x + 28 * u;
    bx = Hint(dl, bold_, 22 * u, {bx, b.y - 52 * u}, "A", Rgba(70, 170, 70), german_ ? "Ersetzen" : "Replace");
    bx = Hint(dl, bold_, 22 * u, {bx, b.y - 52 * u}, "B", Rgba(200, 60, 60), german_ ? "Abbrechen" : "Cancel");
    if (Pressed(ImGuiKey_GamepadFaceDown, ImGuiKey_Enter)) {
      const bool ok = RestoreSave(ActiveProfile(), game, s, c);
      confirm_ = false;
      Load();
      Message(ok ? (german_ ? "Wiederhergestellt" : "Restored")
                 : (german_ ? "Konnte nicht wiederherstellen: " : "Could not restore it: ") + LastSaveError());
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
