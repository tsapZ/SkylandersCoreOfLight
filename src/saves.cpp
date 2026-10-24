#include "saves.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <fstream>

#include "games.h"
#include "profiles.h"

namespace coreoflight {
namespace fs = std::filesystem;
namespace {

constexpr wchar_t kProfileFolder[] = L"B13EBABEBABEBABE";  // the profile the games save as

fs::path Utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

uint64_t FileTime(const fs::path& path) {
  WIN32_FILE_ATTRIBUTE_DATA data;
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
  return uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32 | data.ftLastWriteTime.dwLowDateTime;
}

// The newest file in a folder and below (copies keep their files' times).
uint64_t NewestFile(const fs::path& dir) {
  std::error_code ec;
  uint64_t newest = 0;
  for (fs::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    if (it->is_regular_file(ec)) newest = std::max(newest, FileTime(it->path()));
  }
  return newest;
}

fs::path UserData(const Profile& p, const fs::path& exe) { return p.root / GameFolderName(exe); }

// The save folder and its header as the SDK's manifest names them, relative to the user data.
std::wstring RelativeSave(const SaveSlot& slot) {
  const fs::path title = slot.folder.parent_path().parent_path();
  return std::wstring(kProfileFolder) + L"\\" + title.filename().wstring() + L"\\00000001\\" +
         slot.folder.filename().wstring();
}

std::string g_error;  // why the last copy failed (LastSaveError)

bool Failed(const std::error_code& ec, const fs::path& path) {
  const std::u8string u8 = path.filename().u8string();
  g_error = ec.message() + " (" + std::string(u8.begin(), u8.end()) + ")";
  return false;
}

// Every file and folder below `from` into `to` (made if missing), files overwritten.
bool CopyTree(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  fs::create_directories(to, ec);
  if (ec) return Failed(ec, to);
  for (fs::recursive_directory_iterator it(from, ec), end; it != end; it.increment(ec)) {
    if (ec) return Failed(ec, from);
    const fs::path target = to / fs::relative(it->path(), from, ec);
    if (ec) return Failed(ec, it->path());
    if (it->is_directory(ec)) {
      fs::create_directories(target, ec);
    } else {
      fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
    }
    if (ec) return Failed(ec, it->path());
  }
  if (ec) return Failed(ec, from);
  return true;
}

std::string NowName() {
  SYSTEMTIME t{};
  GetLocalTime(&t);
  char text[32];
  std::snprintf(text, sizeof(text), "%04u-%02u-%02u %02u-%02u-%02u", t.wYear, t.wMonth, t.wDay, t.wHour,
                t.wMinute, t.wSecond);
  return text;
}

}  // namespace

std::string LastSaveError() { return g_error; }

fs::path BackupsDir(const Profile& p, const Game& game) {
  return SkylandersDir() / "Backups" / game.key / (p.id.empty() ? fs::path("main") : Utf8Path(p.id));
}

std::vector<SaveSlot> ListSaves(const Profile& p, const fs::path& exe) {
  std::vector<SaveSlot> slots;
  std::error_code ec;
  for (fs::directory_iterator title(UserData(p, exe) / kProfileFolder, ec), end; title != end; title.increment(ec)) {
    if (ec) break;
    if (!title->is_directory(ec)) continue;
    std::error_code ec2;
    for (fs::directory_iterator save(title->path() / "00000001", ec2), end2; save != end2; save.increment(ec2)) {
      if (ec2) break;
      if (!save->is_directory(ec2)) continue;
      SaveSlot s;
      s.folder = save->path();
      const std::u8string u8 = s.folder.filename().u8string();
      s.name.assign(u8.begin(), u8.end());
      s.header = title->path() / "Headers" / "00000001" / (s.folder.filename().wstring() + L".header");
      s.written = NewestFile(s.folder);
      s.options = s.name.size() >= 7 && s.name.compare(s.name.size() - 7, 7, "OPTIONS") == 0;
      const size_t at = s.name.rfind('_');
      if (at != std::string::npos && at + 1 < s.name.size() &&
          std::all_of(s.name.begin() + at + 1, s.name.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        s.number = std::atoi(s.name.c_str() + at + 1);
      }
      if (s.written) slots.push_back(std::move(s));
    }
  }
  std::sort(slots.begin(), slots.end(), [](const SaveSlot& a, const SaveSlot& b) {
    if (a.options != b.options) return b.options;
    if (a.number != b.number) return a.number < b.number;
    return a.name < b.name;
  });
  return slots;
}

std::vector<SaveCopy> ListCopies(const Profile& p, const Game& game, const fs::path& exe, const SaveSlot& slot) {
  std::vector<SaveCopy> copies;
  std::error_code ec;
  const fs::path title = slot.folder.parent_path().parent_path();
  const fs::path previous = UserData(p, exe) / "save-backup" / "previous" /
                            (std::wstring(kProfileFolder) + L"_" + title.filename().wstring() + L"_00000001_" +
                             slot.folder.filename().wstring());
  if (fs::is_directory(previous / "package", ec)) {
    copies.push_back({previous, NewestFile(previous / "package"), true, false, false});
  }
  std::vector<SaveCopy> own;
  for (fs::directory_iterator it(BackupsDir(p, game) / slot.folder.filename(), ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    if (!fs::is_directory(it->path() / "package", ec)) continue;
    own.push_back({it->path(), NewestFile(it->path() / "package"), false,
                   fs::exists(it->path() / "before-restore", ec), fs::exists(it->path() / "auto", ec)});
  }
  // Newest first: by the copy's name, its date and time.
  std::sort(own.begin(), own.end(), [](const SaveCopy& a, const SaveCopy& b) { return a.folder > b.folder; });
  copies.insert(copies.end(), own.begin(), own.end());
  return copies;
}

bool BackupSave(const Profile& p, const Game& game, const SaveSlot& slot, const char* mark) {
  std::error_code ec;
  const fs::path to = BackupsDir(p, game) / slot.folder.filename() / Utf8Path(NowName());
  if (!CopyTree(slot.folder, to / "package")) return false;
  if (fs::exists(slot.header, ec)) fs::copy_file(slot.header, to / "header", fs::copy_options::overwrite_existing, ec);
  // The SDK's manifest: the save folder and its header, relative to the user data.
  const std::wstring save = RelativeSave(slot);
  const fs::path title = slot.folder.parent_path().parent_path();
  const std::wstring header = std::wstring(kProfileFolder) + L"\\" + title.filename().wstring() +
                              L"\\Headers\\00000001\\" + slot.folder.filename().wstring() + L".header";
  std::ofstream manifest(to / "manifest.txt", std::ios::binary);
  const std::u8string a = fs::path(save).u8string(), b = fs::path(header).u8string();
  manifest << std::string(a.begin(), a.end()) << "\n" << std::string(b.begin(), b.end()) << "\n";
  if (mark) std::ofstream(to / mark, std::ios::binary) << "1\n";
  return manifest.good();
}

bool RestoreSave(const Profile& p, const Game& game, const SaveSlot& slot, const SaveCopy& copy) {
  std::error_code ec;
  if (!fs::is_directory(copy.folder / "package", ec)) return false;
  if (fs::exists(slot.folder, ec) && !BackupSave(p, game, slot, "before-restore")) return false;
  fs::remove_all(slot.folder, ec);
  if (!CopyTree(copy.folder / "package", slot.folder)) return false;
  if (fs::exists(copy.folder / "header", ec)) {
    fs::create_directories(slot.header.parent_path(), ec);
    fs::copy_file(copy.folder / "header", slot.header, fs::copy_options::overwrite_existing, ec);
  }
  return !ec;
}

int AutoBackup(const Profile& p, const Game& game, const fs::path& exe, size_t keep) {
  int made = 0;
  for (const SaveSlot& slot : ListSaves(p, exe)) {
    auto automatic = [&] {
      std::vector<SaveCopy> autos;
      for (const SaveCopy& c : ListCopies(p, game, exe, slot)) {
        if (c.automatic) autos.push_back(c);  // newest first
      }
      return autos;
    };
    std::vector<SaveCopy> autos = automatic();
    if (!autos.empty() && autos.front().written >= slot.written) continue;  // unchanged since
    if (!BackupSave(p, game, slot, "auto")) continue;
    ++made;
    autos = automatic();
    std::error_code ec;
    for (size_t i = keep; i < autos.size(); ++i) fs::remove_all(autos[i].folder, ec);
  }
  return made;
}

}  // namespace coreoflight
