#include "profiles.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>

#include "games.h"
#include "platform.h"
#include "portal/data_folder.h"
#include "portal/figure_catalog.h"
#include "portal/figure_file.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;
namespace sp = skylanders::portal;

constexpr char kListFile[] = "profiles.ini";
constexpr char kProfileFile[] = "profile.ini";
// The folder the SDK keeps the saves in under a game's user data: the profile's fixed XUID.
constexpr char kSaveFolder[] = "B13EBABEBABEBABE";

std::vector<Profile> g_profiles;
size_t g_active = 0;

fs::path ProfilesDir() { return SkylandersDir() / "Profiles"; }

std::string Utf8(const fs::path& p) { return PathUtf8(p); }

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
  const fs::path temp = WithPathSuffix(path, ".new");
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    for (const std::string& line : lines) out << line << "\r\n";
    if (!out) return false;
  }
  return MoveReplaceFile(temp, path);
}

// The lib's list (.dump, .sky, .bin, .dmp; any case), the same files the games' portal menu reads.
bool IsFigureFile(const fs::path& p) { return sp::IsFigureFileName(p); }

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
  main.root = SkylandersDir();
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
    main.root = SkylandersDir();
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
    fs::create_directories(dir / "Figures", ec);
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

bool DeleteProfile(const Profile& p, bool to_recycle_bin) {
  if (IsMainProfile(p) || p.root.empty() || p.root.parent_path() != ProfilesDir()) return false;
  if (!to_recycle_bin) {
    std::error_code ec;
    fs::remove_all(p.root, ec);
    if (ec) return false;
    if (ActiveProfile().id == p.id) WriteIniValues(ProfilesDir() / kListFile, {{"active", ""}});
    return true;
  }
  if (!MovePathToTrash(p.root)) return false;
  if (ActiveProfile().id == p.id) WriteIniValues(ProfilesDir() / kListFile, {{"active", ""}});
  return true;
}

fs::path ProfileFiguresDir(const Profile& p) {
  return IsMainProfile(p) ? MainFiguresDir() : p.root / "Figures";
}

fs::path ProfilePicturesDir(const Profile& p) {
  return IsMainProfile(p) ? MainFiguresDir().parent_path() / "Pictures" : p.root / "Pictures";
}

bool HasSave(const Profile& p, const fs::path& exe) {
  std::error_code ec;
  const fs::path saves = p.root / GameFolderName(exe) / kSaveFolder;
  return fs::is_directory(saves, ec) && !fs::is_empty(saves, ec);
}

uint64_t LastSaved(const Profile& p, const fs::path& exe) {
  std::error_code ec;
  const fs::path saves = p.root / GameFolderName(exe) / kSaveFolder;
  uint64_t newest = 0;
  for (fs::recursive_directory_iterator it(saves, ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file(ec)) continue;
    newest = std::max(newest, PathFileTimeTicks(it->path()));
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

namespace {

// Imaginators' toys are signed by their maker: an Ed25519 signature over blocks 0 and 1 (serial,
// kind), spread over blocks 2, 4, 34 and 62, the key's number in byte 63 (ImaginatorsRecomp
// src/game/figure_check.cpp). So one can't be made with a new serial, and the game turns a blank
// one (CreateBlankFigure) down ("unknown key"). "Made new" keeps those blocks and every sector
// trailer and empties the rest, as a new toy comes out of the box; the game's own check takes it
// (a sensei and a creation crystal, 2026-11-11).
bool IsImaginatorsToy(uint16_t id, uint16_t variant) {
  if (const sp::SkylanderInfo* info = sp::FindSkylander(id, variant)) return info->game == "Imaginators";
  for (const sp::SkylanderInfo& info : sp::AllSkylanders()) {
    if (info.id == id) return info.game == "Imaginators";
  }
  return false;
}

sp::FigureData EmptiedKeepingSignature(const sp::FigureData& data) {
  sp::FigureData out{};
  for (size_t block = 0; block < out.size() / 16; ++block) {
    const bool signed_block = block <= 2 || block == 4 || block == 34 || block == 62;
    if (signed_block || block % 4 == 3) std::copy_n(data.begin() + block * 16, 16, out.begin() + block * 16);
  }
  return out;
}

}  // namespace

sp::FigureData StartedOver(const sp::FigureData& data, bool new_serial) {
  const uint16_t id = sp::ReadFigureId(data), variant = sp::ReadFigureVariant(data);
  if (IsImaginatorsToy(id, variant)) return EmptiedKeepingSignature(data);
  if (new_serial) return sp::CreateBlankFigure(id, variant);
  return sp::CreateBlankFigure(id, variant, {data[0], data[1], data[2], data[3]});
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
    if (IsRealToySubpath(it->path())) continue;
    std::error_code rel_ec;
    const fs::path dest = target / fs::relative(it->path(), source, rel_ec);
    if (rel_ec || fs::exists(dest, rel_ec)) continue;
    const auto data = sp::LoadFigureFile(it->path());
    if (!data) continue;
    fs::create_directories(dest.parent_path(), rel_ec);
    const sp::FigureData out = as_new ? StartedOver(*data, true) : *data;
    if (sp::SaveFigureFileAtomic(dest, out)) ++copied;
  }
  return copied;
}

std::vector<std::string> ProfileArgs(const fs::path& exe) {
  const Profile& p = ActiveProfile();
  // The main profile is the games' own default, unless the launcher runs on a copy (--documents)
  // or keeps its data portable (#47): a game set up on this PC finds no portable-data.txt by itself.
  if (IsMainProfile(p) && !DocumentsMoved() && skylanders::PortableFolder().empty()) return {};
  const fs::path name = GameFolderName(exe);
  return {"--user_data_root=" + PathUtf8(p.root / name),
          "--cache_root=" + PathUtf8(SkylandersDir() / name / "cache")};
}

}  // namespace coreoflight
