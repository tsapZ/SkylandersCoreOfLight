#include "file_browser.h"
#include "hints.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cfloat>
#include <cwctype>
#include <system_error>
#include <utility>

#include "disc_search.h"
#include "games.h"
#include "imgui.h"
#include "nxe_art.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;

ImU32 Rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

const ImU32 kGold = Rgba(255, 204, 64);
const ImU32 kWhite = Rgba(255, 255, 255);
const ImU32 kDim = Rgba(180, 190, 215);
const ImU32 kPanel = Rgba(24, 30, 70, 235);
const ImU32 kEdge = Rgba(90, 100, 150);

float TextWidth(ImFont* font, float size, const std::string& text) {
  return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
}

void Text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const std::string& text) {
  dl->AddText(font, size, pos, col, text.c_str());
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
           const char* label, const char* key = nullptr) {
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

fs::path Known(REFKNOWNFOLDERID id) {
  PWSTR path = nullptr;
  fs::path out;
  if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path))) out = path;
  CoTaskMemFree(path);
  return out;
}

bool IsFile(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

bool LessNoCase(const std::wstring& a, const std::wstring& b) {
  return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                      [](wchar_t x, wchar_t y) { return std::towlower(x) < std::towlower(y); });
}

}  // namespace

void FileBrowser::StartSearch() {
  StopSearch();
  search_ = StartDiscSearch();
  searching_view_ = true;
  listed_found_ = 0;
  ListSearch();
}

void FileBrowser::StopSearch() {
  if (search_) search_->stop = true;
}

void FileBrowser::ListSearch() {
  if (listed_found_ == 0) {
    entries_.clear();
    selected_ = 0;
    folder_.clear();
  }
  if (!search_) return;
  std::lock_guard lock(search_->mutex);
  for (; listed_found_ < search_->found.size(); ++listed_found_) {
    const FoundDisc& d = search_->found[listed_found_];
    if (d.game < 0) continue;  // another disc or version: counted, not offered
    const Game& g = AllGames()[size_t(d.game)];
    entries_.push_back({Entry::Kind::kFound, std::string(german_ ? g.name_de : g.name_en) + "  \xC2\xB7  " + Utf8(d.iso),
                        d.iso});
  }
}

void FileBrowser::Init(ImFont* regular, ImFont* bold, bool german, bool folders_only) {
  regular_ = regular;
  bold_ = bold;
  german_ = german;
  folders_only_ = folders_only;
}

void FileBrowser::Open(const fs::path& folder) {
  open_ = true;
  chosen_.clear();
  message_.clear();
  StopSearch();
  searching_view_ = false;
  fs::path start = folder;
  if (start.empty()) start = last_folder_;
  if (start.empty()) start = Known(folders_only_ ? FOLDERID_Documents : FOLDERID_Downloads);
  std::error_code ec;
  if (!start.empty() && !fs::is_directory(start, ec)) start = fs::path();  // gone: the drives and places
  List(start);
}

fs::path FileBrowser::TakeChosen() { return std::exchange(chosen_, {}); }

void FileBrowser::List(const fs::path& folder) {
  entries_.clear();
  selected_ = scroll_ = 0;
  folder_ = folder;
  if (folder.empty()) {
    // The disc search first, then the usual places, then every drive.
    if (!folders_only_) {
      entries_.push_back({Entry::Kind::kSearch,
                          german_ ? "Discs suchen (Downloads, Desktop, Dokumente und die Laufwerke)"
                                  : "Find my discs (Downloads, Desktop, Documents and the drives)",
                          {}});
    }
    // Under Wine / Proton (SteamOS) the Linux home folder: WINEHOMEDIR is "\??\Z:\home\<user>".
    if (folders_only_) {
      wchar_t home[MAX_PATH] = L"";
      const DWORD length = GetEnvironmentVariableW(L"WINEHOMEDIR", home, MAX_PATH);
      std::wstring path(home, length < MAX_PATH ? length : 0);
      if (path.starts_with(L"\\??\\")) path = path.substr(4);
      std::error_code ec;
      if (!path.empty() && fs::is_directory(path, ec)) {
        entries_.push_back({Entry::Kind::kPlace, german_ ? "Linux: Pers\xC3\xB6nlicher Ordner  " + Utf8(fs::path(path))
                                                         : "Linux: home folder  " + Utf8(fs::path(path)),
                            fs::path(path)});
      }
    }
    const std::pair<REFKNOWNFOLDERID, const char*> places[] = {
        {FOLDERID_Downloads, german_ ? "Downloads" : "Downloads"},
        {FOLDERID_Desktop, german_ ? "Desktop" : "Desktop"},
        {FOLDERID_Documents, german_ ? "Dokumente" : "Documents"},
    };
    for (const auto& [id, name] : places) {
      const fs::path p = Known(id);
      if (!p.empty()) entries_.push_back({Entry::Kind::kPlace, name, p});
    }
    const DWORD drives = GetLogicalDrives();
    for (int d = 0; d < 26; ++d) {
      if (!(drives & (1u << d))) continue;
      const std::wstring root = std::wstring(1, wchar_t(L'A' + d)) + L":\\";
      wchar_t label[MAX_PATH] = L"";
      GetVolumeInformationW(root.c_str(), label, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
      std::string name = Utf8(fs::path(root));
      if (label[0]) name += "  " + Utf8(fs::path(label));
      entries_.push_back({Entry::Kind::kPlace, name, fs::path(root)});
    }
    return;
  }
  entries_.push_back({Entry::Kind::kUp, german_ ? ".. (eine Ebene h\xC3\xB6her)" : ".. (one level up)", {}});
  // Choosing the games folder: this one, as the second entry in every folder.
  if (folders_only_) {
    entries_.push_back({Entry::Kind::kThisFolder,
                        german_ ? "Diesen Ordner f\xC3\xBCr die Spiele nehmen" : "Use this folder for the games", folder});
  }
  // A folder with an unpacked disc can be taken as it is.
  if (!folders_only_ && IsFile(folder / "default.xex")) {
    entries_.push_back({Entry::Kind::kThisFolder,
                        german_ ? "Diesen Ordner einlesen (entpackte Disc)" : "Take this folder (an unpacked disc)",
                        folder});
  }
  std::vector<std::pair<std::wstring, fs::path>> folders, isos;
  std::error_code ec;
  for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; it != end;
       it.increment(ec)) {
    if (ec) break;
    std::error_code type_ec;
    const fs::path p = it->path();
    const std::wstring name = p.filename().wstring();
    if (name.empty() || name[0] == L'$') continue;  // the system's own folders
    if (it->is_directory(type_ec)) {
      DWORD attributes = GetFileAttributesW(p.c_str());
      if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN)) continue;
      folders.emplace_back(name, p);
    } else {
      std::wstring ext = p.extension().wstring();
      std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
      if (ext == L".iso" && !folders_only_) isos.emplace_back(name, p);
    }
  }
  auto by_name = [](const auto& a, const auto& b) { return LessNoCase(a.first, b.first); };
  std::sort(folders.begin(), folders.end(), by_name);
  std::sort(isos.begin(), isos.end(), by_name);
  // The discs first: what the owner is looking for.
  for (const auto& [name, p] : isos) entries_.push_back({Entry::Kind::kIso, Utf8(fs::path(name)), p});
  for (const auto& [name, p] : folders) entries_.push_back({Entry::Kind::kFolder, Utf8(fs::path(name)), p});
  // The first disc chosen (the unpacked one here, else the first ISO); for a folder the first one
  // in it, or "this folder" when it has none.
  if (folders_only_) {
    selected_ = entries_.size() > 2 ? 2 : 1;
  } else if (!isos.empty() || (entries_.size() > 1 && entries_[1].kind == Entry::Kind::kThisFolder)) {
    selected_ = 1;
  }
}

// `e` is a copy: List() clears entries_, which a reference into it would point into (opening a
// folder then listed the drives instead, now and then).
void FileBrowser::Choose(Entry e) {
  switch (e.kind) {
    case Entry::Kind::kUp: {
      const fs::path parent = folder_.parent_path();
      List(parent == folder_ ? fs::path() : parent);  // above a drive: the drives
      break;
    }
    case Entry::Kind::kFolder:
    case Entry::Kind::kPlace:
      List(e.path);
      break;
    case Entry::Kind::kSearch:
      StartSearch();
      break;
    case Entry::Kind::kThisFolder:
    case Entry::Kind::kIso:
    case Entry::Kind::kFound:
      chosen_ = e.path;
      last_folder_ = e.path.parent_path();
      StopSearch();
      searching_view_ = false;
      open_ = false;
      break;
  }
}

void FileBrowser::Draw(float width, float height) {
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("files", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float u = std::min(width / 1280.0f, height / 720.0f);
  DrawBackdrop(dl, width, height, {}, true);
  if (folders_only_) {
    Text(dl, bold_, 22 * u, {48 * u, 34 * u}, kDim, german_ ? "SPIELE-ORDNER" : "GAMES FOLDER");
    Text(dl, bold_, 40 * u, {46 * u, 56 * u}, kGold,
         german_ ? "Wo liegen deine Spiele?" : "Where are your games?");
  } else {
    Text(dl, bold_, 22 * u, {48 * u, 34 * u}, kDim, german_ ? "DISC EINLESEN" : "ADD A DISC");
    Text(dl, bold_, 40 * u, {46 * u, 56 * u}, kGold,
         german_ ? "W\xC3\xA4hle die ISO deiner Disc" : "Choose your disc's ISO");
  }
  const float left = 48 * u, top = 128 * u;
  std::string where = folder_.empty() ? (german_ ? "Dieser PC" : "This PC") : Utf8(folder_);
  if (searching_view_ && search_) {
    ListSearch();
    const int folders = search_->folders.load(), isos = search_->isos.load();
    const int discs = int(entries_.size());
    char status[200];
    if (!search_->done) {
      std::snprintf(status, sizeof(status),
                    german_ ? "Suche Discs ... %d Ordner, %d ISO-Dateien angesehen" : "Looking for discs ... %d folders, %d ISO files seen",
                    folders, isos);
    } else {
      std::snprintf(status, sizeof(status),
                    german_ ? "Fertig: %d Skylanders-Disc(s) unter %d ISO-Dateien in %d Ordnern"
                            : "Done: %d Skylanders disc(s) among %d ISO files in %d folders",
                    discs, isos, folders);
    }
    where = status;
  }
  Text(dl, regular_, 20 * u, {left, top}, kDim, FitStart(regular_, 20 * u, where, width - 2 * left));

  auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
  auto once = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
  const int n = int(entries_.size());
  const float list_top = top + 40 * u, row_h = 44 * u, bottom = height - 80 * u;
  const int rows = std::max(1, int((bottom - list_top) / row_h));
  if (pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_GamepadDpadUp) || pressed(ImGuiKey_GamepadLStickUp)) {
    selected_ = std::max(0, selected_ - 1);
  }
  if (pressed(ImGuiKey_DownArrow) || pressed(ImGuiKey_GamepadDpadDown) || pressed(ImGuiKey_GamepadLStickDown)) {
    selected_ = std::min(n - 1, selected_ + 1);
  }
  if (pressed(ImGuiKey_PageUp) || pressed(ImGuiKey_GamepadL1)) selected_ = std::max(0, selected_ - rows);
  if (pressed(ImGuiKey_PageDown) || pressed(ImGuiKey_GamepadR1)) selected_ = std::min(n - 1, selected_ + rows);
  if (io.MouseWheel != 0) selected_ = std::clamp(selected_ - int(io.MouseWheel) * 3, 0, std::max(0, n - 1));
  // The view follows the chosen entry only when it leaves it (centred on it, the list jumped under
  // a resting mouse).
  if (selected_ < scroll_) scroll_ = selected_;
  if (selected_ >= scroll_ + rows) scroll_ = selected_ - rows + 1;
  scroll_ = std::clamp(scroll_, 0, std::max(0, n - rows));
  const int first = scroll_;
  int clicked = -1;
  for (int i = first; i < std::min(n, first + rows); ++i) {
    const Entry& e = entries_[i];
    const ImVec2 a{left, list_top + float(i - first) * row_h}, b{width - left, a.y + row_h - 6 * u};
    const bool over = io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y;
    if (over && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) selected_ = i;
    if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      if (selected_ == i) clicked = i;
      selected_ = i;
    }
    const bool chosen = i == selected_;
    dl->AddRectFilled(a, b, chosen ? Rgba(60, 70, 140, 240) : kPanel, 12 * u);
    if (chosen) dl->AddRect(a, b, kGold, 12 * u, 0, 2.5f * u);
    // A small symbol: a disc for an ISO or an unpacked disc, a folder for the others.
    const ImVec2 c{a.x + 26 * u, (a.y + b.y) * 0.5f};
    if (e.kind == Entry::Kind::kIso || e.kind == Entry::Kind::kThisFolder || e.kind == Entry::Kind::kFound) {
      dl->AddCircleFilled(c, 13 * u, Rgba(200, 210, 235), 24);
      dl->AddCircleFilled(c, 4 * u, Rgba(40, 50, 90), 12);
    } else if (e.kind == Entry::Kind::kSearch) {
      // A magnifying glass.
      dl->AddCircle({c.x - 3 * u, c.y - 3 * u}, 9 * u, kGold, 24, 3 * u);
      dl->AddLine({c.x + 4 * u, c.y + 4 * u}, {c.x + 12 * u, c.y + 12 * u}, kGold, 4 * u);
    } else if (e.kind != Entry::Kind::kUp) {
      dl->AddRectFilled({c.x - 13 * u, c.y - 8 * u}, {c.x + 13 * u, c.y + 10 * u}, Rgba(230, 180, 70), 3 * u);
      dl->AddRectFilled({c.x - 13 * u, c.y - 12 * u}, {c.x - 2 * u, c.y - 6 * u}, Rgba(230, 180, 70), 2 * u);
    }
    const bool disc = e.kind == Entry::Kind::kIso || e.kind == Entry::Kind::kThisFolder || e.kind == Entry::Kind::kFound;
    const ImU32 col = disc || e.kind == Entry::Kind::kSearch ? kGold : kWhite;
    const bool bold = e.kind == Entry::Kind::kIso || e.kind == Entry::Kind::kFound || e.kind == Entry::Kind::kSearch;
    Text(dl, bold ? bold_ : regular_, 20 * u, {a.x + 54 * u, a.y + 9 * u}, col,
         FitStart(regular_, 20 * u, e.label, b.x - a.x - 70 * u));
  }
  if (searching_view_ && n == 0) {
    Text(dl, regular_, 19 * u, {left, list_top + row_h * 0.4f}, kDim,
         search_ && search_->done
             ? (german_ ? "Keine Skylanders-Disc gefunden. B: selbst einen Ordner w\xC3\xA4hlen."
                        : "No Skylanders disc found. B: choose a folder yourself.")
             : (german_ ? "Einen Moment ..." : "One moment ..."));
  } else if (n <= (folders_only_ ? 2 : 1) && !folder_.empty()) {
    Text(dl, regular_, 19 * u, {left, list_top + row_h * (folders_only_ ? 2.2f : 1.2f)}, kDim,
         folders_only_ ? (german_ ? "Hier sind keine Ordner." : "No folders here.")
                       : (german_ ? "Hier sind keine ISO-Dateien und keine Ordner." : "No ISO files and no folders here."));
  }
  if (n && (clicked >= 0 || once(ImGuiKey_Enter) || once(ImGuiKey_GamepadFaceDown))) {
    Choose(entries_[clicked >= 0 ? clicked : selected_]);
  } else if (!folders_only_ && (once(ImGuiKey_GamepadFaceUp) || once(ImGuiKey_Y))) {
    StartSearch();  // Y: look for the discs from anywhere
  } else if (searching_view_ && (once(ImGuiKey_Backspace) || once(ImGuiKey_GamepadFaceRight))) {
    // B in the results: back to the drives and places.
    StopSearch();
    searching_view_ = false;
    List({});
  } else if (once(ImGuiKey_Backspace) || once(ImGuiKey_GamepadFaceRight)) {
    // B: one level up; above the drives it closes.
    if (folder_.empty()) {
      open_ = false;
    } else {
      Choose({Entry::Kind::kUp, {}, {}});
    }
  } else if (once(ImGuiKey_Escape)) {
    StopSearch();
    searching_view_ = false;
    open_ = false;
  }
  float hx = left;
  const float hy = height - 52 * u;
  if (folders_only_) {
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 170, 70), german_ ? "\xC3\x96" "ffnen / nehmen" : "Open / use");
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "LB/RB", Rgba(80, 90, 140), german_ ? "Seite" : "Page");
    Text(dl, regular_, 18 * u, {hx, hy + 2 * u}, kDim,
         german_ ? "Spiele in diesem Ordner und seinen Unterordnern werden gefunden, mit ihrer Disc; neue kommen hierher."
                 : "Games in this folder and its subfolders are found, with their disc; new ones go here.");
  } else {
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "A", Rgba(70, 170, 70), german_ ? "\xC3\x96" "ffnen / einlesen" : "Open / add");
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "Y", Rgba(220, 180, 40), german_ ? "Discs suchen" : "Find my discs");
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "B", Rgba(200, 60, 60), german_ ? "Zur\xC3\xBC" "ck" : "Back");
    hx = Hint(dl, bold_, 22 * u, {hx, hy}, "LB/RB", Rgba(80, 90, 140), german_ ? "Seite" : "Page");
    Text(dl, regular_, 18 * u, {hx, hy + 2 * u}, kDim,
         german_ ? "Die ISO wird in den rom-Ordner des Spiels entpackt; sie selbst bleibt, wo sie ist."
                 : "The ISO is unpacked into the game's rom folder; the file itself stays where it is.");
  }
  ImGui::End();
}

}  // namespace coreoflight
