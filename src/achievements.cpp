#include "achievements.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>

#include "games.h"
#include "profiles.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;

fs::path Utf8Path(const std::string& s) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::vector<std::string> Fields(const std::string& line) {
  std::vector<std::string> fields;
  for (size_t at = 0;;) {
    const size_t tab = line.find('\t', at);
    fields.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
    if (tab == std::string::npos) break;
    at = tab + 1;
  }
  return fields;
}

// The SDK's unlock file: "[unlocked.<id>]" followed by "filetime = <number>".
std::map<uint32_t, uint64_t> ReadUnlocked(const fs::path& path) {
  std::map<uint32_t, uint64_t> unlocked;
  std::ifstream in(path, std::ios::binary);
  std::string line;
  long long current = -1;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.starts_with("[unlocked.")) {
      current = std::strtoll(line.c_str() + 10, nullptr, 10);
      if (current >= 0) unlocked[uint32_t(current)] = 1;  // unlocked even without a time
      continue;
    }
    if (line.starts_with("[")) {
      current = -1;
      continue;
    }
    const auto eq = line.find('=');
    if (current < 0 || eq == std::string::npos) continue;
    std::string key = line.substr(0, eq);
    key.erase(key.find_last_not_of(" \t") + 1);
    key.erase(0, key.find_first_not_of(" \t"));
    if (key == "filetime") {
      const unsigned long long time = std::strtoull(line.c_str() + eq + 1, nullptr, 10);
      if (time) unlocked[uint32_t(current)] = time;
    }
  }
  return unlocked;
}

}  // namespace

GameAchievements LoadAchievements(const Game& game, const Profile& profile) {
  GameAchievements out;
  const fs::path game_dir = Utf8Path(game.name_en);
  // The list from the profile's picture folder, else the main profile's (the same disc).
  std::string title;
  for (const fs::path& root : {ProfilePicturesDir(profile), MainFiguresDir().parent_path() / "Pictures"}) {
    const fs::path dir = root / game_dir;
    std::ifstream in(dir / "achievements.txt", std::ios::binary);
    if (!in) continue;
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const auto f = Fields(line);
      if (f.size() == 2 && f[0] == "title") {
        title = f[1];
        continue;
      }
      if (f.size() < 6) continue;
      Achievement a;
      a.id = uint32_t(std::strtoul(f[0].c_str(), nullptr, 10));
      a.gamerscore = uint32_t(std::strtoul(f[1].c_str(), nullptr, 10));
      if (!f[2].empty()) a.picture = dir / Utf8Path(f[2]);
      a.name = f[3];
      a.description = f[4];
      a.locked_description = f[5];
      out.items.push_back(std::move(a));
    }
    if (!out.items.empty()) break;
  }
  out.known = !out.items.empty();
  if (!out.known || title.empty()) return out;
  const fs::path toml =
      profile.root / Utf8Path(UserDataName(fs::path(game.exe))) / "achievements" / Utf8Path(title + ".toml");
  const auto unlocked = ReadUnlocked(toml);
  for (Achievement& a : out.items) {
    out.gamerscore_total += a.gamerscore;
    if (auto it = unlocked.find(a.id); it != unlocked.end()) {
      a.unlocked = it->second;
      ++out.unlocked;
      out.gamerscore += a.gamerscore;
    }
  }
  out.platinum = !out.items.empty() && out.unlocked == int(out.items.size());
  for (const Achievement& a : out.items) {
    if (out.platinum) out.platinum_time = std::max(out.platinum_time, a.unlocked);
  }
  return out;
}

std::string FileTimeDate(uint64_t filetime, bool german) {
  if (filetime <= 1) return {};
  FILETIME utc{DWORD(filetime), DWORD(filetime >> 32)}, local{};
  SYSTEMTIME st{};
  if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &st)) return {};
  char text[32];
  if (german) {
    std::snprintf(text, sizeof(text), "%02u.%02u.%04u", unsigned(st.wDay), unsigned(st.wMonth), unsigned(st.wYear));
  } else {
    std::snprintf(text, sizeof(text), "%04u-%02u-%02u", unsigned(st.wYear), unsigned(st.wMonth), unsigned(st.wDay));
  }
  return text;
}

}  // namespace coreoflight
