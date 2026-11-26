#include "games.h"

#ifdef _WIN32
#include <windows.h>
#include <cwctype>
#else
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iterator>
#include <fstream>
#include <optional>
#include <thread>
#include <unordered_map>

#include "portal/data_folder.h"
#include "portal/figure_catalog.h"
#include "portal/figure_file.h"
#include "gpu_tier.h"
#include "handheld.h"
#include "hub_log.h"  // WineVersion
#include "platform.h"
#include "profiles.h"
#include "updates.h"  // Offline

namespace coreoflight {
namespace {

namespace fs = std::filesystem;

constexpr char kConfigFile[] = "coreoflight.ini";
// "figures = <folder>" in coreoflight.ini, as a game's portal_figures_dir: empty for the shared one.
fs::path g_figures_dir;
#ifdef _WIN32
#define COL_EXE_EXT ".exe"
constexpr char kBuildDir[] = "out/build/win-amd64-release";
constexpr char kRuntimeLibrary[] = "rexruntime.dll";
#else
#define COL_EXE_EXT ""
constexpr char kBuildDir[] = "out/build/linux-" COREOFLIGHT_LINUX_ARCH "-release";
constexpr char kRuntimeLibrary[] = "librexruntime.so";
#endif

// Each repository pins its disc's default.xex in docs/game/default.xex.sha256 and its code in
// docs/game/default.xex.code: another pressing with the same code (another language set) runs too.
const std::vector<Game> kGames = {
    {"ssa", "Spyro's Adventure", "Spyro's Adventure", 2011, 0x415608AB, "ssarecompiled" COL_EXE_EXT, "SSARecomp",
     "Spyro's Adventure Recompiled",
     "e195f5130c1e23fb4654baf7c383ef97d02c89aeb7d4389705ae31235646ef7b,"
     "82110000:0a6b070c95f730a77a8662afdbbb297a15ce8a465c344951ab5ff1bb94bd802d", 16, 0,
     "{2B356600-5297-425A-A646-8498A7AAC307}",
     // Its text and menus are in a folder per language; an extract tool that left those out gave a
     // game that froze on its title screen (CoL #64).
     "text/*/global.lxb,ui/*/hudmenu.str"},
    {"giants", "Giants", "Giants", 2012, 0x415608DA, "giantsrecompiled" COL_EXE_EXT, "GiantsRecomp",
     "Giants Recompiled",
     "12d1e29e4f07fbc559bc1a4988dd6bf346c3d16e30cbf862e2683e862f9fc09e,"
     "82150000:9fb7c356cbba689b46370ce2c1d5228e8e41b47d019ddfbdf49fd8a96c6584fe", 112, 4614,
     "{1F12B332-CD84-471A-BE9F-7F3BFA614A6D}"},
    {"swapforce", "Swap Force", "Swap Force", 2013, 0x415608E9, "swapforcerecompiled" COL_EXE_EXT, "SwapForceRecomp",
     "Swap Force Recompiled",
     "dc58d3851bbd89935e9ecf2ee84b158ec400f049da46c937a4713477a50c79d7,"
     "823E0000:0dcab6aa8cfb5c82f42e603d165a1966615073bf1f4372bd1733cd5fcca3e31b", 2015, 8192,
     "{C3E00C3F-6CE0-4100-BD9B-13F9B4A8CCBA}"},
    {"trapteam", "Trap Team", "Trap Team", 2014, 0x41560911, "trapteamrecompiled" COL_EXE_EXT, "TrapTeamRecomp",
     "Trap Team Recompiled",
     "f1d2373e4507e21cc371e6f22d02e909602a1b5bc4704896c3c099cc64002a35,"
     "821A0000:08c372fa42acfd04eb2d3ec047dfe92552e67c597d46f4f174673d2ee8c65687", 462, 12288,
     "{9A774956-7256-4E2C-A3D6-418903FC71B6}"},
    {"superchargers", "SuperChargers", "SuperChargers", 2015, 0x4156091F, "superchargersrecompiled" COL_EXE_EXT,
     "SuperChargersRecomp", "SuperChargers Recompiled",
     // The European English / German and Spanish / Italian discs: the code the downloads are recompiled from.
     // Not the French / Dutch one (c6b1d7a3..., pin 82450000:d379ae25..., also the Scandinavian disc's): another
     // build whose code points at read-only data elsewhere; the download crashed at start with it (#135, #118) and
     // refuses it since skylanders-portal fix/own-code-pin. Only a port built from source with that disc runs it.
     "02ff6672d460c9aaa54e69d584af67a7138c6c93cde5e04b0da4284207be7c61,"
     "02846647633e93c627e74b92d3ad63a90e1504546958fc8d81e28ac65de1f9bc,"
     "82450000:3140330d024070a570409f8ebb651593ffdf5e038495721f2ca3538dbecd4671", 3412, 16640,
     "{44F04CDA-C049-4558-AAA6-25A6E7932D71}"},
    {"imaginators", "Imaginators", "Imaginators", 2016, 0x41560930, "imaginatorsrecompiled" COL_EXE_EXT,
     "ImaginatorsRecomp", "Imaginators Recompiled",
     "4298c4f6fb1684bed34dd8d2292b508782b37e3709ba1ed58b08ec334a9f7029,"
     "82480000:91c6d66ea09dff54b1650fb9b446081dcb180095e9d29b60a0432ea284e8ea7f", 601, 20480,
     "{3D80E212-E302-4422-9B7B-6AFAF712400E}"},
};

std::string Trim(std::string s) {
  const auto first = s.find_first_not_of(" \t\r\n\"");
  const auto last = s.find_last_not_of(" \t\r\n\"");
  return first == std::string::npos ? std::string() : s.substr(first, last - first + 1);
}

fs::path DefaultGamesRoot() {
#ifdef _WIN32
  return LocalAppDataDir() / "Programs";
#else
  return LocalAppDataDir() / "CoreOfLight" / "Games";
#endif
}

}  // namespace

std::unordered_map<std::string, std::string> ReadIni(const fs::path& path) {
  std::unordered_map<std::string, std::string> out;
  std::ifstream in(path);
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    // Notepad saves UTF-8 with a byte order mark in front of the first key.
    if (first && line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
    first = false;
    const auto comment = line.find_first_of(";#");
    if (comment != std::string::npos) line.resize(comment);
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    out[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
  }
  return out;
}

namespace {
fs::path g_documents_dir;  // --documents=<folder>
}  // namespace

fs::path DocumentsDir() { return g_documents_dir.empty() ? skylanders::DocumentsFolder() : g_documents_dir; }

void SetDocumentsDir(const fs::path& dir) { g_documents_dir = dir; }

bool DocumentsMoved() { return !g_documents_dir.empty(); }

std::string UserDataName(const fs::path& exe) {
  std::string stem = exe.stem().string();
  if (stem.ends_with("recompiled")) stem.resize(stem.size() - 4);
  return stem;
}

namespace {
fs::path g_skylanders_override;  // SetSkylandersDirOverride
}  // namespace

fs::path SkylandersDir() {
  if (!g_skylanders_override.empty()) return g_skylanders_override;
  // Portable data (#47): next to Core of Light, on a --documents copy too (portable-data.txt is on
  // purpose).
  if (const fs::path portable = skylanders::PortableFolder(); !portable.empty()) return portable / "Skylanders";
  return g_documents_dir.empty() ? skylanders::DataFolder() : g_documents_dir / "Skylanders";
}

std::string StoredFolder(const fs::path& dir) {
  const fs::path portable = skylanders::PortableFolder();
  const fs::path::string_type inside = portable.native() + fs::path::preferred_separator;
  const fs::path keep = !portable.empty() && dir.native().starts_with(inside) ? dir.lexically_relative(portable) : dir;
  return PathUtf8(keep);
}

fs::path FolderFromStored(const std::string& text) {
  fs::path dir = PathFromUtf8(text);
  if (dir.is_relative()) {
    const fs::path portable = skylanders::PortableFolder();
    if (!portable.empty()) dir = portable / dir;
  }
  return dir;
}

void SetSkylandersDirOverride(const fs::path& dir) { g_skylanders_override = dir; }

fs::path GameFolderName(const fs::path& exe) {
  const std::string_view folder = skylanders::GameFolderForExe(UserDataName(exe));
  return PathFromUtf8(folder.empty() ? UserDataName(exe) : std::string(folder));
}

namespace {

bool IsFile(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

// The folder a game runs in and its rom folder. The games look for rom\ in their working folder
// (game_data_root, default "rom"): an installed game has it next to its exe, a developer's build
// in the repository three folders up (<repo>\out\build\win-amd64-release\<exe>).
void FindRom(const fs::path& exe, fs::path& workdir, fs::path& rom) {
  fs::path dir = exe.parent_path();
  for (int up = 0; up < 4 && !dir.empty(); ++up, dir = dir.parent_path()) {
    if (IsFile(dir / "rom" / "default.xex")) {
      workdir = dir;
      rom = dir / "rom";
      return;
    }
    if (dir == dir.parent_path()) break;
  }
  workdir = exe.parent_path();
  rom = workdir / "rom";
}

// Where the game's installer put it: InstallLocation of its uninstall entry (per user, else for
// everyone), wherever that is (a library folder, a folder chosen in the setup).
fs::path InstalledByEntry(const Game& game) {
  // Not in a test run on stand-in data (--documents with --offline, as maintainer/test/hub_keys.ps1 starts
  // it): then only the games folder counts. Before, a test found the PC's installed Trap Team by its entry
  // and wrote a key page's keys into that real settings file (2026-11-26). --documents alone (a copy to
  // try) still finds the installed games (code review t19b: a game installed in a folder of its own was
  // gone then).
  if (DocumentsMoved() && Offline()) return {};
#ifdef _WIN32
  const std::wstring key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" +
                           PathFromUtf8(game.app_id).wstring() + L"_is1";
  for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
    wchar_t value[MAX_PATH * 2] = L"";
    DWORD size = sizeof(value);
    if (RegGetValueW(root, key.c_str(), L"InstallLocation", RRF_RT_REG_SZ, nullptr, value, &size) ==
        ERROR_SUCCESS) {
      const fs::path exe = fs::path(value) / game.exe;
      if (IsFile(exe)) return exe;
    }
  }
#else
  const fs::path exe = DefaultGamesRoot() / PathFromUtf8(game.app_name) / game.exe;
  if (IsFile(exe)) return exe;
#endif
  return {};
}

// The game in the games folder: <folder>\<app name>, <folder>\<repo>, the folder itself, then any
// folder up to two levels down that holds its exe (folders named as the owner likes).
fs::path FindInGamesFolder(const Game& game, const fs::path& folder) {
  for (const fs::path& p :
       {folder / PathFromUtf8(game.app_name) / game.exe, folder / game.repo / game.exe, folder / game.exe}) {
    if (IsFile(p)) return p;
  }
  std::error_code ec;
  for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; it != end;
       it.increment(ec)) {
    if (ec) break;
    std::error_code type_ec;
    if (!it->is_directory(type_ec)) continue;
    if (IsFile(it->path() / game.exe)) return it->path() / game.exe;
    for (fs::directory_iterator inner(it->path(), fs::directory_options::skip_permission_denied, type_ec), stop;
         inner != stop; inner.increment(type_ec)) {
      if (type_ec) break;
      std::error_code inner_ec;
      if (inner->is_directory(inner_ec) && IsFile(inner->path() / game.exe)) return inner->path() / game.exe;
    }
  }
  return {};
}

fs::path FindExe(const Game& game, const fs::path& launcher_dir,
                 const std::unordered_map<std::string, std::string>& ini, const fs::path& games_folder,
                 bool& copied) {
  copied = false;
  if (auto it = ini.find(game.key); it != ini.end() && !it->second.empty()) {
    fs::path p = PathFromUtf8(it->second);
    if (fs::is_directory(p)) p /= game.exe;
    if (IsFile(p)) return p;
  }
  // Portable (#47): the stick's own copy before one this PC has set up.
  const bool portable = !skylanders::PortableFolder().empty();
  if (!portable) {
    if (fs::path p = InstalledByEntry(game); !p.empty()) return p;
  }
  if (!games_folder.empty()) {
    if (fs::path p = FindInGamesFolder(game, games_folder); !p.empty()) {
      // No uninstall entry points there (InstalledByEntry came first), or one of another PC.
      copied = portable ? InstalledByEntry(game) != p : true;
      return p;
    }
  }
  if (portable) {
    if (fs::path p = InstalledByEntry(game); !p.empty()) return p;
  }
  const fs::path installed = DefaultGamesRoot() / PathFromUtf8(game.app_name) / game.exe;
  if (IsFile(installed)) return installed;
  for (const fs::path& p : {launcher_dir / game.exe, launcher_dir / game.repo / game.exe,
                            launcher_dir / game.app_name / game.exe}) {
    if (IsFile(p)) return p;
  }
  // A developer's layout: the repositories side by side, <repo>/<repo>/out/build/...
  for (fs::path dir = launcher_dir; !dir.empty(); dir = dir.parent_path()) {
    for (const fs::path& p : {dir / game.repo / game.repo / kBuildDir / game.exe,
                              dir / game.repo / kBuildDir / game.exe,
                              dir / "tsapZ" / game.repo / kBuildDir / game.exe}) {
      if (IsFile(p)) return p;
    }
    if (dir == dir.parent_path()) break;
  }
  return {};
}

}  // namespace

const std::vector<Game>& AllGames() { return kGames; }

int GameForDisc(const XexId& xex) { return GameForDiscIn(AllGames(), xex); }

int GameForTitleId(uint32_t title_id) { return GameForTitleIdIn(AllGames(), title_id); }

fs::path g_install_dir;  // --install_dir, or "library" in coreoflight.ini

void FindGames(std::vector<Found>& found, const fs::path& launcher_dir) {
  const auto ini = ReadIni(launcher_dir / kConfigFile);
  if (auto it = ini.find("figures"); it != ini.end()) g_figures_dir = PathFromUtf8(it->second);
  if (auto it = ini.find("library"); it != ini.end() && g_install_dir.empty()) g_install_dir = FolderFromStored(it->second);
  const fs::path games_folder = g_install_dir.empty() && !skylanders::PortableFolder().empty() ? GamesFolder() : g_install_dir;
  found = std::vector<Found>(kGames.size());
  for (size_t i = 0; i < kGames.size(); ++i) {
    found[i].exe = FindExe(kGames[i], launcher_dir, ini, games_folder, found[i].copied);
    if (found[i].exe.empty()) continue;
    FindRom(found[i].exe, found[i].workdir, found[i].rom);
    // ssarecompiled.exe reads ssarecomp.toml next to it.
    found[i].settings = ReadIni(found[i].exe.parent_path() / (UserDataName(found[i].exe) + ".toml"));
  }
  for (size_t i = 0; i < kGames.size(); ++i) {
    if (found[i].exe.empty()) continue;
    found[i].disc = DiscState::kChecking;
    // The thread keeps its own copy of what it needs and of the result (the vector may be
    // replaced meanwhile).
    std::thread([result = found[i].disc.shared(), xex = found[i].rom / "default.xex", i] {
      if (!IsFile(xex)) {
        result->store(DiscState::kMissing);
        return;
      }
      std::ifstream in(xex, std::ios::binary);
      const std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      result->store(IsGameDisc(kGames[i], IdentifyXex(data.data(), data.size())) ? DiscState::kMatches
                                                                                 : DiscState::kOther);
    }).detach();
  }
}

bool StartGame(const Found& game, std::string& error, void** process) {
  // The active profile's own user data (profiles.h); nothing for the main profile. Started from
  // here, the game's F4 menu leaves out the graphics and start-up settings: they are on its card
  // (skylanders-portal settings_menu.h, #10).
  std::vector<std::string> args = ProfileArgs(game.exe);
  args.emplace_back("--started_by_launcher=true");
  // The profile's name as the player's gamertag, where a game shows one (the SDK's user_name, up to
  // 15 characters; quotes left out so the command line stays whole).
  std::string name = ActiveProfile().name;
  std::erase(name, '"');
  size_t keep = std::min<size_t>(name.size(), 15);
  while (keep > 0 && keep < name.size() && (static_cast<unsigned char>(name[keep]) & 0xC0) == 0x80) --keep;
  name.resize(keep);  // whole UTF-8 characters only
  if (!name.empty()) args.push_back("--user_name=" + name);
  void* spawned = SpawnProcess(game.exe, args, game.workdir);
  if (!spawned) {
    error = LastSystemError();
    return false;
  }
  *process = spawned;
  return true;
}

void SetInstallDir(const fs::path& dir) { g_install_dir = dir; }

namespace {

#ifdef _WIN32
// Volume and file id of a folder, to tell whether two paths are the same folder.
bool FolderId(const fs::path& dir, BY_HANDLE_FILE_INFORMATION& info) {
  const HANDLE h = CreateFileW(dir.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  const bool ok = GetFileInformationByHandle(h, &info) != 0;
  CloseHandle(h);
  return ok;
}
#endif

}  // namespace

fs::path RealFolder(const fs::path& dir) {
#ifdef _WIN32
  if (WineVersion().empty()) return dir;
  const std::wstring text = dir.wstring();
  std::wstring lower = text;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
  if (!lower.starts_with(L"z:\\home\\")) return dir;
  const fs::path real = fs::path(L"Z:\\var\\home\\" + text.substr(8));
  BY_HANDLE_FILE_INFORMATION a = {}, b = {};
  if (!FolderId(dir, a) || !FolderId(real, b)) return dir;
  const bool same = a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
                    a.nFileIndexLow == b.nFileIndexLow;
  return same ? real : dir;
#else
  return dir;
#endif
}

fs::path GamesFolder() {
  if (!g_install_dir.empty()) return g_install_dir;
  // Portable (#47): the games next to Core of Light as well.
  if (const fs::path portable = skylanders::PortableFolder(); !portable.empty()) return portable / "Games";
  return DefaultGamesRoot();
}

bool GamesFolderChosen() { return !g_install_dir.empty(); }

fs::path InstallFolder(const Game& game) { return GamesFolder() / PathFromUtf8(game.app_name); }

std::string SetupVersion(const Game& game, const fs::path& setup) {
  const std::string stem = setup.stem().string();
  const std::string prefix = std::string(game.repo) + "Setup-";
  if (stem.starts_with(prefix)) return stem.substr(prefix.size());

  constexpr std::string_view kTarSuffix = "-linux-" COREOFLIGHT_LINUX_ARCH ".tar.gz";
  const std::string filename = PathUtf8(setup.filename());
  const std::string tar_prefix = std::string(game.repo) + "-";
  if (filename.ends_with(kTarSuffix) && filename.starts_with(tar_prefix) &&
      filename.size() > tar_prefix.size() + kTarSuffix.size()) {
    return filename.substr(tar_prefix.size(), filename.size() - tar_prefix.size() - kTarSuffix.size());
  }
  return {};
}

bool VersionLess(const std::string& a, const std::string& b) {
  // "0.1.0-beta.1": the release numbers, then the pre-release, which comes before the release.
  auto split = [](const std::string& v, char sep) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
      const size_t at = v.find(sep, start);
      parts.push_back(v.substr(start, at == std::string::npos ? std::string::npos : at - start));
      if (at == std::string::npos) break;
      start = at + 1;
    }
    return parts;
  };
  auto number = [](const std::string& s) -> std::optional<unsigned long> {
    if (s.empty() || !std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); })) {
      return std::nullopt;
    }
    return std::stoul(s);
  };
  // Compares two dot-separated lists: numbers as numbers (below words), words as text, a longer
  // list after a shorter one it starts with. -1, 0 or 1.
  auto compare = [&](const std::vector<std::string>& x, const std::vector<std::string>& y) {
    for (size_t i = 0; i < std::min(x.size(), y.size()); ++i) {
      const auto nx = number(x[i]), ny = number(y[i]);
      if (nx && ny) {
        if (*nx != *ny) return *nx < *ny ? -1 : 1;
      } else if (nx || ny) {
        return nx ? -1 : 1;
      } else if (x[i] != y[i]) {
        return x[i] < y[i] ? -1 : 1;
      }
    }
    return x.size() == y.size() ? 0 : (x.size() < y.size() ? -1 : 1);
  };
  const size_t da = a.find('-'), db = b.find('-');
  const int release = compare(split(a.substr(0, da), '.'), split(b.substr(0, db), '.'));
  if (release) return release < 0;
  const bool pa = da != std::string::npos, pb = db != std::string::npos;
  if (pa != pb) return pa;  // a pre-release before its release
  return pa && compare(split(a.substr(da + 1), '.'), split(b.substr(db + 1), '.')) < 0;
}

std::string InstalledVersion(const Game& game, const fs::path& exe) {
#ifdef _WIN32
  const std::wstring key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" +
                           PathFromUtf8(game.app_id).wstring() + L"_is1";
  for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
    wchar_t location[MAX_PATH * 2] = L"", version[128] = L"";
    DWORD size = sizeof(location), vsize = sizeof(version);
    if (RegGetValueW(root, key.c_str(), L"InstallLocation", RRF_RT_REG_SZ, nullptr, location, &size) !=
        ERROR_SUCCESS) {
      continue;
    }
    std::error_code ec;
    if (!fs::equivalent(fs::path(location) / game.exe, exe, ec)) continue;
    if (RegGetValueW(root, key.c_str(), L"DisplayVersion", RRF_RT_REG_SZ, nullptr, version, &vsize) ==
        ERROR_SUCCESS) {
      const std::u8string u8 = fs::path(version).u8string();
      return std::string(u8.begin(), u8.end());
    }
  }
#else
  (void)game;
  if (exe.empty()) return {};
  std::ifstream in(exe.parent_path() / ".coreoflight-version");
  std::string line;
  if (std::getline(in, line)) return Trim(line);
#endif
  return {};
}

fs::path FindSetup(const Game& game, const fs::path& launcher_dir) {
  // The setup/archive of a folder with the highest version (then the newest).
  auto newest = [&](const fs::path& dir) {
    fs::path best;
    std::string best_version;
    fs::file_time_type best_time{};
    std::error_code ec;
#ifdef _WIN32
    const std::string prefix = std::string(game.repo) + "Setup-";
#else
    const std::string prefix = std::string(game.repo) + "-";
#endif
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
      const std::string name = PathUtf8(entry.path().filename());
#ifdef _WIN32
      if (!name.starts_with(prefix) || entry.path().extension() != ".exe") continue;
#else
      if (!name.starts_with(prefix) || !name.ends_with("-linux-" COREOFLIGHT_LINUX_ARCH ".tar.gz")) continue;
#endif
      const std::string version = SetupVersion(game, entry.path());
      const auto t = entry.last_write_time(ec);
      const bool better = best.empty() ||
                          (version.empty() != best_version.empty()
                               ? !version.empty()
                               : (VersionLess(best_version, version) ||
                                  (!VersionLess(version, best_version) && t > best_time)));
      if (better) {
        best = entry.path();
        best_version = version;
        best_time = t;
      }
    }
    return best;
  };
  if (fs::path p = newest(launcher_dir / "games"); !p.empty()) return p;
  // A developer's layout: the repositories side by side.
  for (fs::path dir = launcher_dir; !dir.empty(); dir = dir.parent_path()) {
#ifdef _WIN32
    if (fs::path p = newest(dir / game.repo / game.repo / "installer" / "Output"); !p.empty()) return p;
#else
    for (const fs::path& d : {dir / game.repo / game.repo / "dist", dir / game.repo / "dist",
                              dir / "tsapZ" / game.repo / "dist"}) {
      if (fs::path p = newest(d); !p.empty()) return p;
    }
#endif
    if (dir == dir.parent_path()) break;
  }
  return {};
}

namespace {

fs::path SetupLog(size_t game) {
  return TempDir() / "CoreOfLight" / (std::string(kGames[game].key) + "-setup.log");
}

}  // namespace

bool StartGameInstall(size_t game, const fs::path& setup, bool german, void** process, bool update,
                      const fs::path& dir) {
  if (game >= kGames.size() || setup.empty()) return false;
  const fs::path log = SetupLog(game);
  std::error_code ec;
  fs::create_directories(log.parent_path(), ec);
  fs::remove(log, ec);
  // The screen's own size for the game's window, its settings at 2x (the "Quality" preset), or at
  // 1x in handheld mode (handheld.h, #39) and on a weak graphics card (gpu_tier.h), the portal on
  // Automatic (hybrid, the games' default). Only a new setup writes these: the setup never
  // overwrites a settings file.
  const bool light = g_handheld_active || DetectGpu().weak;
#ifdef _WIN32
  const int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
  // No desktop shortcut per game: the launcher is where they start (the setup's own wizard still
  // offers one when run by hand).
  std::vector<std::string> args = {
      "/VERYSILENT",
      "/SUPPRESSMSGBOXES",
      "/NORESTART",
      "/NOCANCEL",
      "/MERGETASKS=!desktopicon",
      "/NOROM=1",
      "/PORTAL=hybrid",
      light ? "/SCALE=1" : "/SCALE=2",
      german ? "/LANGUAGE=german" : "/LANGUAGE=english",
      "/RESOLUTION=" + std::to_string(w) + "x" + std::to_string(h),
      "/LOG=" + PathUtf8(log),
  };
  // An update goes where the game is (its exe's folder: the setup's entry would know it, a copied
  // game has none); a new install into the games folder when one is set.
  if (update && !dir.empty()) {
    args.push_back("/DIR=" + PathUtf8(dir));
  } else if (!g_install_dir.empty() && !update) {
    args.push_back("/DIR=" + PathUtf8(g_install_dir / PathFromUtf8(kGames[game].app_name)));
  }
  void* spawned = SpawnProcess(setup, args, setup.parent_path(), true);
  if (!spawned) return false;
  *process = spawned;
  return true;
#else
  int w = 1920, h = 1080;
  SDL_Rect bounds{};
  if (const SDL_DisplayID display = SDL_GetPrimaryDisplay(); display && SDL_GetDisplayBounds(display, &bounds)) {
    if (bounds.w > 0 && bounds.h > 0) {
      w = bounds.w;
      h = bounds.h;
    }
  }
  const fs::path dest_dir = update && !dir.empty() ? dir : InstallFolder(kGames[game]);
  const std::string toml_name = UserDataName(fs::path(kGames[game].exe)) + ".toml";
  const std::string version = SetupVersion(kGames[game], setup);
  const std::string resolution = std::to_string(w) + "x" + std::to_string(h);
  static constexpr const char kInstallScript[] =
      "set -e\n"
      "archive=\"$1\"; dest=\"$2\"; toml=\"$3\"; ver=\"$4\"; scale=\"$5\"; lang=\"$6\"; res=\"$7\"; log=\"$8\"\n"
      "mkdir -p \"$dest\"\n"
      "stage=\"$(mktemp -d \"${dest}/.extract.XXXXXX\")\"\n"
      "cleanup() { rm -rf \"$stage\"; }\n"
      "trap cleanup EXIT\n"
      "mkdir -p \"$stage/files\"\n"
      "had_toml=0\n"
      "if [ -f \"$dest/$toml\" ]; then\n"
      "  had_toml=1\n"
      "  cp \"$dest/$toml\" \"$stage/$toml.keep\"\n"
      "fi\n"
      "tar -xzf \"$archive\" -C \"$stage/files\" >>\"$log\" 2>&1\n"
      "src=\"$stage/files\"\n"
      "entries=$(find \"$stage/files\" -mindepth 1 -maxdepth 1 | wc -l)\n"
      "if [ \"$entries\" -eq 1 ]; then\n"
      "  only=$(find \"$stage/files\" -mindepth 1 -maxdepth 1)\n"
      "  if [ -d \"$only\" ]; then\n"
      "    src=\"$only\"\n"
      "  fi\n"
      "fi\n"
      "cp -a \"$src\"/. \"$dest\"/\n"
      "if [ \"$had_toml\" -eq 1 ]; then\n"
      "  mv -f \"$stage/$toml.keep\" \"$dest/$toml\"\n"
      "elif [ -f \"$dest/$toml\" ]; then\n"
      "  sed -i -E \"s|^portal_mode[[:space:]]*=.*|portal_mode = \\\"hybrid\\\"|\" \"$dest/$toml\"\n"
      "  sed -i -E \"s|^resolution[[:space:]]*=.*|resolution = \\\"$res\\\"|\" \"$dest/$toml\"\n"
      "  sed -i -E \"s|^resolution_scale[[:space:]]*=.*|resolution_scale = $scale|\" \"$dest/$toml\"\n"
      "  if grep -qE \"^#?[[:space:]]*language[[:space:]]*=\" \"$dest/$toml\"; then\n"
      "    sed -i -E \"s|^#?[[:space:]]*language[[:space:]]*=.*|language = \\\"$lang\\\"|\" \"$dest/$toml\"\n"
      "  else\n"
      "    printf \"\\n[\\\"Game\\\"]\\nlanguage = \\\"%s\\\"\\n\" \"$lang\" >> \"$dest/$toml\"\n"
      "  fi\n"
      "fi\n"
      "if [ -n \"$ver\" ]; then\n"
      "  printf \"%s\\n\" \"$ver\" > \"$dest/.coreoflight-version\"\n"
      "fi\n";
  std::vector<std::string> args = {
      "-c",
      kInstallScript,
      "coreoflight-install",
      PathUtf8(setup),
      PathUtf8(dest_dir),
      toml_name,
      version,
      light ? "1" : "2",
      german ? "german" : "english",
      resolution,
      PathUtf8(log),
  };
  void* spawned = SpawnProcess("/bin/sh", args, setup.parent_path(), true);
  if (!spawned) return false;
  *process = spawned;
  return true;
#endif
}

std::string GameInstallError(size_t game) {
  if (game >= kGames.size()) return {};
  std::ifstream in(SetupLog(game));
  std::string line, found;
  while (std::getline(in, line)) {
    if (const size_t at = line.find("ROM: "); at != std::string::npos) found = line.substr(at + 5);
  }
  return found;
}

fs::path PicturesDir(size_t game) {
  if (game >= kGames.size()) return {};
  return MainFiguresDir().parent_path() / "Pictures" / PathFromUtf8(kGames[game].name_en);
}

fs::path TitleMusicFile(size_t game) {
  if (game >= kGames.size()) return {};
  return MainFiguresDir().parent_path() / "Pictures" / PathFromUtf8(kGames[game].name_en) / "title_music.wav";
}

namespace {

// A game built with the SDK's window_hidden (rexglue-sdk 1ac58ab) never shows its window for an
// export (checked: no visible window in 1115 polls, the export complete); older builds don't know
// the setting, so it is only passed where the game's runtime mentions it (the SDK's settings are
// in rexruntime next to the exe, not in the exe).
void AppendHiddenWindowFlag(const Found& found, std::vector<std::string>& args) {
  if (BinaryContainsString(found.exe.parent_path() / kRuntimeLibrary, "window_hidden")) {
    args.emplace_back("--window_hidden=true");
  }
}

// For the older builds: SW_HIDE in the start-up info hides only the first window a program shows;
// the games show theirs themselves once the picture is set up, so an export could come up as a
// tiny window with the game's intro (seen on a colleague's PC after a disc import, and closed as a
// strange pop-up). So the launcher hides every window of the export's process as soon as it is
// visible, for as long as the process runs (polled every 20 ms; it may flash for a moment).
void KeepWindowsHidden(void* process) {
#ifdef _WIN32
  HANDLE own = nullptr;
  HANDLE h = static_cast<HANDLE>(process);
  if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &own, SYNCHRONIZE, FALSE, 0)) return;
  const DWORD pid = GetProcessId(h);
  std::thread([own, pid] {
    while (WaitForSingleObject(own, 20) == WAIT_TIMEOUT) {
      EnumWindows(
          [](HWND hwnd, LPARAM wanted) -> BOOL {
            DWORD owner = 0;
            GetWindowThreadProcessId(hwnd, &owner);
            if (owner == DWORD(wanted) && IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
            return TRUE;
          },
          LPARAM(pid));
    }
    CloseHandle(own);
  }).detach();
#else
  (void)process;
#endif
}

}  // namespace

bool StartTitleMusicExport(size_t game, const Found& found, void** process) {
  if (found.exe.empty() || !BinaryContainsString(found.exe, "export_title_music")) return false;
  const fs::path data = TempDir() / "CoreOfLight" / "title_music" / PathFromUtf8(UserDataName(found.exe));
  std::vector<std::string> args = {"--export_title_music=" + PathUtf8(TitleMusicFile(game)),
                                   "--user_data_root=" + PathUtf8(data), "--started_by_launcher=true"};
  AppendHiddenWindowFlag(found, args);
  void* spawned = SpawnProcess(found.exe, args, found.workdir, true);
  if (!spawned) return false;
  KeepWindowsHidden(spawned);
  *process = spawned;
  return true;
}

bool StartFigureCheck(const Found& found, const fs::path& list, void** process) {
  if (found.exe.empty() || !BinaryContainsString(found.exe, "check_figures")) return false;
  const fs::path data = TempDir() / "CoreOfLight" / "figure_check";
  std::vector<std::string> args = {"--check_figures=" + PathUtf8(list), "--user_data_root=" + PathUtf8(data),
                                   "--audio_mute=true", "--started_by_launcher=true"};
  AppendHiddenWindowFlag(found, args);
  void* spawned = SpawnProcess(found.exe, args, found.workdir, true);
  if (!spawned) return false;
  KeepWindowsHidden(spawned);
  *process = spawned;
  return true;
}

bool StartPictureExport(size_t game, const Found& found, void** process) {
  if (found.exe.empty() || !BinaryContainsString(found.exe, "export_pictures")) return false;
  const fs::path data = TempDir() / "CoreOfLight" / "pictures" / PathFromUtf8(UserDataName(found.exe));
  std::vector<std::string> args = {"--export_pictures=" + PathUtf8(PicturesDir(game)),
                                   "--user_data_root=" + PathUtf8(data),
                                   "--audio_mute=true",
                                   "--fullscreen=false",
                                   "--window_width=320",
                                   "--window_height=180",
                                   "--portal_mode=software",
                                   "--started_by_launcher=true"};
  AppendHiddenWindowFlag(found, args);
  void* spawned = SpawnProcess(found.exe, args, found.workdir, true);
  if (!spawned) return false;
  KeepWindowsHidden(spawned);
  *process = spawned;
  return true;
}

fs::path MainFiguresDir() {
  return g_figures_dir.empty() ? SkylandersDir() / "Figures" : g_figures_dir;
}

fs::path FiguresDir() { return ProfileFiguresDir(ActiveProfile()); }

fs::path FigurePicture(uint16_t id, uint16_t variant) {
  char name[32];
  std::snprintf(name, sizeof(name), "%u-%u.png", unsigned(id), unsigned(variant));
  // The figure's own game first (the owner's wish: each figure as its own game shows it; the games
  // themselves fill gaps from later games), then the newest game's. The pictures are the discs',
  // the same for every profile: the main profile's too.
  std::vector<std::string> games;
  if (const auto* info = skylanders::portal::FindSkylander(id, variant)) games.emplace_back(info->game);
  for (size_t i = kGames.size(); i-- > 0;) games.emplace_back(kGames[i].name_en);
  const fs::path roots[] = {ProfilePicturesDir(ActiveProfile()), MainFiguresDir().parent_path() / "Pictures"};
  for (const std::string& game : games) {
    for (const fs::path& root : roots) {
      const fs::path p = root / PathFromUtf8(game) / name;
      if (IsFile(p)) return p;
    }
  }
  return {};
}

int CountFigures() {
  int count = 0;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(FiguresDir(), ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    if (it->is_regular_file(ec) && skylanders::portal::IsFigureFileName(it->path())) ++count;
  }
  return count;
}

}  // namespace coreoflight
