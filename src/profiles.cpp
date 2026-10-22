#include "profiles.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>

#include "games.h"
#include "portal/figure_file.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;
namespace sp = skylanders::portal;

constexpr char kListFile[] = "profiles.ini";
constexpr char kProfileFile[] = "profile.ini";
// The folder the SDK keeps the saves in under a game's user data: the profile's fixed XUID.
constexpr wchar_t kSaveFolder[] = L"B13EBABEBABEBABE";

std::vector<Profile> g_profiles;
size_t g_active = 0;

fs::path ProfilesDir() { return DocumentsDir() / "Skylanders" / "Profiles"; }

fs::path Utf8Path(const std::string& s) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string Utf8(const fs::path& p) {
  const std::u8string u8 = p.u8string();
  return std::string(u8.begin(), u8.end());
}

// "112-4614": a figure's id and variant.
bool ParsePicture(const std::string& text, Profile& p) {
  char* end = nullptr;
  const unsigned long id = std::strtoul(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '-') return false;
  const char* rest = end + 1;
  const unsigned long variant = std::strtoul(rest, &end, 10);
  if (end == rest || *end != '\0' || id > 0xFFFF || variant > 0xFFFF) return false;
  p.has_picture = true;
  p.picture_id = uint16_t(id);
  p.picture_variant = uint16_t(variant);
  return true;
}

std::string PictureText(const Profile& p) {
  if (!p.has_picture) return {};
  return std::to_string(p.picture_id) + "-" + std::to_string(p.picture_variant);
}

// Rewrites `key = value` in an ini file, keeping its other lines.
bool WriteIniValues(const fs::path& path, const std::vector<std::pair<std::string, std::string>>& values) {
  std::vector<std::string> lines;
  {
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      lines.push_back(line);
    }
  }
  for (const auto& [key, value] : values) {
    bool found = false;
    for (std::string& line : lines) {
      const auto eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::string k = line.substr(0, eq);
      k.erase(0, k.find_first_not_of(" \t\xEF\xBB\xBF"));
      k.erase(k.find_last_not_of(" \t") + 1);
      if (k != key) continue;
      line = key + " = " + value;
      found = true;
      break;
    }
    if (!found) lines.push_back(key + " = " + value);
  }
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  const fs::path temp = path.parent_path() / (path.filename().wstring() + L".new");
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    for (const std::string& line : lines) out << line << "\r\n";
    if (!out) return false;
  }
  return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

bool IsFigureFile(const fs::path& p) {
  const auto ext = p.extension();
  return ext == ".dump" || ext == ".sky" || ext == ".bin";
}

}  // namespace

std::vector<Profile> LoadProfiles(bool german) {
  const auto list = ReadIni(ProfilesDir() / kListFile);
  auto get = [&](const char* key) {
    auto it = list.find(key);
    return it == list.end() ? std::string() : it->second;
  };
  std::vector<Profile> profiles;
  Profile main;
  main.name = get("main_name");
  if (main.name.empty()) main.name = german ? "Hauptprofil" : "Main profile";
  ParsePicture(get("main_picture"), main);
  main.root = DocumentsDir();
  profiles.push_back(main);
  std::error_code ec;
  std::vector<fs::path> dirs;
  for (fs::directory_iterator it(ProfilesDir(), ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    if (it->is_directory(ec) && fs::exists(it->path() / kProfileFile, ec)) dirs.push_back(it->path());
  }
  std::sort(dirs.begin(), dirs.end());
  for (const fs::path& dir : dirs) {
    const auto ini = ReadIni(dir / kProfileFile);
    Profile p;
    p.id = Utf8(dir.filename());
    p.root = dir;
    if (auto it = ini.find("name"); it != ini.end()) p.name = it->second;
    if (p.name.empty()) p.name = p.id;
    if (auto it = ini.find("picture"); it != ini.end()) ParsePicture(it->second, p);
    profiles.push_back(p);
  }
  g_profiles = profiles;
  g_active = 0;
  const std::string active = get("active");
  for (size_t i = 1; i < g_profiles.size(); ++i) {
    if (g_profiles[i].id == active) g_active = i;
  }
  return profiles;
}

const Profile& ActiveProfile() {
  if (g_profiles.empty()) {
    static Profile main;
    main.root = DocumentsDir();
    return main;
  }
  return g_profiles[g_active];
}

bool IsMainProfile(const Profile& p) { return p.id.empty(); }

void SetActiveProfile(const Profile& p) {
  for (size_t i = 0; i < g_profiles.size(); ++i) {
    if (g_profiles[i].id == p.id) g_active = i;
  }
  WriteIniValues(ProfilesDir() / kListFile, {{"active", p.id}});
}

std::string CreateProfile(const std::string& name) {
  std::error_code ec;
  for (int n = 2; n < 1000; ++n) {
    const std::string id = "p" + std::to_string(n);
    const fs::path dir = ProfilesDir() / id;
    if (fs::exists(dir, ec)) continue;
    fs::create_directories(dir / "Skylanders" / "Figures", ec);
    if (ec) return {};
    Profile p;
    p.id = id;
    p.name = name;
    p.root = dir;
    if (!SaveProfile(p)) return {};
    return id;
  }
  return {};
}

bool SaveProfile(const Profile& p) {
  if (IsMainProfile(p)) {
    return WriteIniValues(ProfilesDir() / kListFile, {{"main_name", p.name}, {"main_picture", PictureText(p)}});
  }
  return WriteIniValues(p.root / kProfileFile, {{"name", p.name}, {"picture", PictureText(p)}});
}

bool DeleteProfile(const Profile& p) {
  if (IsMainProfile(p) || p.root.empty() || p.root.parent_path() != ProfilesDir()) return false;
  // SHFileOperation wants the paths ended by two null characters.
  std::wstring from = p.root.wstring();
  from.push_back(L'\0');
  from.push_back(L'\0');
  SHFILEOPSTRUCTW op = {};
  op.wFunc = FO_DELETE;
  op.pFrom = from.c_str();
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
  if (SHFileOperationW(&op) != 0 || op.fAnyOperationsAborted) return false;
  if (ActiveProfile().id == p.id) WriteIniValues(ProfilesDir() / kListFile, {{"active", ""}});
  return true;
}

fs::path ProfileFiguresDir(const Profile& p) {
  return IsMainProfile(p) ? MainFiguresDir() : p.root / "Skylanders" / "Figures";
}

fs::path ProfilePicturesDir(const Profile& p) {
  return IsMainProfile(p) ? MainFiguresDir().parent_path() / "Pictures" : p.root / "Skylanders" / "Pictures";
}

bool HasSave(const Profile& p, const fs::path& exe) {
  std::error_code ec;
  const fs::path saves = p.root / Utf8Path(UserDataName(exe)) / kSaveFolder;
  return fs::is_directory(saves, ec) && !fs::is_empty(saves, ec);
}

uint64_t LastSaved(const Profile& p, const fs::path& exe) {
  std::error_code ec;
  const fs::path saves = p.root / Utf8Path(UserDataName(exe)) / kSaveFolder;
  uint64_t newest = 0;
  for (fs::recursive_directory_iterator it(saves, ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file(ec)) continue;
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(it->path().c_str(), GetFileExInfoStandard, &data)) continue;
    const FILETIME& t = data.ftLastWriteTime;
    newest = std::max(newest, uint64_t(t.dwHighDateTime) << 32 | t.dwLowDateTime);
  }
  return newest;
}

namespace {

fs::path PlayTimeFile(const Profile& p) { return ProfileFiguresDir(p).parent_path() / "playtime.txt"; }

std::map<std::string, uint64_t> ReadPlayTimes(const Profile& p) {
  std::map<std::string, uint64_t> times;
  std::ifstream in(PlayTimeFile(p), std::ios::binary);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t tab = line.find('\t');
    if (line.empty() || line[0] == '#' || tab == std::string::npos) continue;
    times[line.substr(0, tab)] = std::strtoull(line.c_str() + tab + 1, nullptr, 10);
  }
  return times;
}

}  // namespace

uint64_t PlaySeconds(const Profile& p, const std::string& game_key) {
  const auto times = ReadPlayTimes(p);
  const auto it = times.find(game_key);
  return it == times.end() ? 0 : it->second;
}

void AddPlaySeconds(const Profile& p, const std::string& game_key, uint64_t seconds) {
  if (!seconds) return;
  auto times = ReadPlayTimes(p);
  times[game_key] += seconds;
  std::string text = "# Skylanders: Core of Light, seconds played per game (started from the launcher)\n";
  for (const auto& [key, value] : times) text += key + "\t" + std::to_string(value) + "\n";
  std::error_code ec;
  fs::create_directories(PlayTimeFile(p).parent_path(), ec);
  const fs::path temp = fs::path(PlayTimeFile(p)) += ".new";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << text;
    if (!out) return;
  }
  fs::rename(temp, PlayTimeFile(p), ec);
}

int CopyFigures(const Profile& from, const Profile& to, bool as_new) {
  const fs::path source = ProfileFiguresDir(from), target = ProfileFiguresDir(to);
  if (source == target) return 0;
  int copied = 0;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(source, ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file(ec) || !IsFigureFile(it->path())) continue;
    // The games' own copies of a figure before writing to it are not figures of their own.
    if (it->path().wstring().find(L"\\real\\") != std::wstring::npos) continue;
    std::error_code rel_ec;
    const fs::path dest = target / fs::relative(it->path(), source, rel_ec);
    if (rel_ec || fs::exists(dest, rel_ec)) continue;
    const auto data = sp::LoadFigureFile(it->path());
    if (!data) continue;
    fs::create_directories(dest.parent_path(), rel_ec);
    const sp::FigureData out = as_new ? sp::CreateBlankFigure(sp::ReadFigureId(*data), sp::ReadFigureVariant(*data))
                                      : *data;
    if (sp::SaveFigureFileAtomic(dest, out)) ++copied;
  }
  return copied;
}

std::wstring ProfileArguments(const fs::path& exe) {
  const Profile& p = ActiveProfile();
  // The main profile is the games' own default, unless the launcher runs on a copy (--documents).
  if (IsMainProfile(p) && !DocumentsMoved()) return {};
  const fs::path name = Utf8Path(UserDataName(exe));
  return L" \"--user_data_root=" + (p.root / name).wstring() + L"\" \"--cache_root=" +
         (DocumentsDir() / name / "cache").wstring() + L"\"";
}

}  // namespace coreoflight
