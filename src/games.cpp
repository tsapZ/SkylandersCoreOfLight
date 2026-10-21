#include "games.h"

#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <cstdio>
#include <fstream>
#include <thread>
#include <unordered_map>

namespace coreoflight {
namespace {

namespace fs = std::filesystem;

constexpr char kConfigFile[] = "coreoflight.ini";
constexpr wchar_t kBuildDir[] = L"out\\build\\win-amd64-release";

// Each repository pins its disc's default.xex in docs/game/default.xex.sha256.
const std::vector<Game> kGames = {
    {"ssa", "Spyro's Adventure", "Spyro's Adventure", 2011, "ssarecompiled.exe", "SSARecomp",
     "Spyro's Adventure Recompiled", "e195f5130c1e23fb4654baf7c383ef97d02c89aeb7d4389705ae31235646ef7b"},
    {"giants", "Giants", "Giants", 2012, "giantsrecompiled.exe", "GiantsRecomp", "Giants Recompiled",
     "12d1e29e4f07fbc559bc1a4988dd6bf346c3d16e30cbf862e2683e862f9fc09e"},
    {"swapforce", "Swap Force", "Swap Force", 2013, "swapforcerecompiled.exe", "SwapForceRecomp",
     "Swap Force Recompiled", "dc58d3851bbd89935e9ecf2ee84b158ec400f049da46c937a4713477a50c79d7"},
    {"trapteam", "Trap Team", "Trap Team", 2014, "trapteamrecompiled.exe", "TrapTeamRecomp",
     "Trap Team Recompiled", "f1d2373e4507e21cc371e6f22d02e909602a1b5bc4704896c3c099cc64002a35"},
    {"superchargers", "SuperChargers", "SuperChargers", 2015, "superchargersrecompiled.exe",
     "SuperChargersRecomp", "SuperChargers Recompiled",
     "02846647633e93c627e74b92d3ad63a90e1504546958fc8d81e28ac65de1f9bc"},
    {"imaginators", "Imaginators", "Imaginators", 2016, "imaginatorsrecompiled.exe",
     "ImaginatorsRecomp", "Imaginators Recompiled",
     "4298c4f6fb1684bed34dd8d2292b508782b37e3709ba1ed58b08ec334a9f7029"},
};

std::string Trim(std::string s) {
  const auto first = s.find_first_not_of(" \t\r\n\"");
  const auto last = s.find_last_not_of(" \t\r\n\"");
  return first == std::string::npos ? std::string() : s.substr(first, last - first + 1);
}

fs::path Utf8Path(const std::string& s) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

// "key = value" lines, ";" and "#" start a comment.
std::unordered_map<std::string, std::string> ReadIni(const fs::path& path) {
  std::unordered_map<std::string, std::string> out;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    const auto comment = line.find_first_of(";#");
    if (comment != std::string::npos) line.resize(comment);
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    out[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
  }
  return out;
}

fs::path KnownFolder(REFKNOWNFOLDERID id) {
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

// The folder a game runs in and its rom folder. The games look for rom\ in their working folder
// (game_data_root, default "rom"): an installed game has it next to its exe, a developer's build
// in the repository three folders up (<repo>\outuild\win-amd64-release\<exe>).
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

// SHA-256 of a file with Windows' own CNG, as lower-case hex; empty if it can't be read.
std::string Sha256(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
  std::string out;
  if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
    std::vector<char> buffer(1 << 20);
    bool ok = true;
    while (in) {
      in.read(buffer.data(), std::streamsize(buffer.size()));
      const auto n = in.gcount();
      if (n > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), ULONG(n), 0) != 0) {
        ok = false;
        break;
      }
    }
    unsigned char digest[32];
    if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
      char hex[65];
      for (int i = 0; i < 32; ++i) std::snprintf(hex + 2 * i, 3, "%02x", digest[i]);
      out.assign(hex, 64);
    }
    BCryptDestroyHash(hash);
  }
  BCryptCloseAlgorithmProvider(alg, 0);
  return out;
}

fs::path FindExe(const Game& game, const fs::path& launcher_dir,
                 const std::unordered_map<std::string, std::string>& ini) {
  if (auto it = ini.find(game.key); it != ini.end() && !it->second.empty()) {
    fs::path p = Utf8Path(it->second);
    if (fs::is_directory(p)) p /= game.exe;
    if (IsFile(p)) return p;
  }
  const fs::path installed = KnownFolder(FOLDERID_LocalAppData) / "Programs" / game.app_name / game.exe;
  if (IsFile(installed)) return installed;
  for (const fs::path& p : {launcher_dir / game.exe, launcher_dir / game.repo / game.exe,
                            launcher_dir / game.app_name / game.exe}) {
    if (IsFile(p)) return p;
  }
  // A developer's layout: the repositories side by side, <repo>\<repo>\out\build\...
  for (fs::path dir = launcher_dir; !dir.empty(); dir = dir.parent_path()) {
    for (const fs::path& p : {dir / game.repo / game.repo / kBuildDir / game.exe,
                              dir / game.repo / kBuildDir / game.exe}) {
      if (IsFile(p)) return p;
    }
    if (dir == dir.parent_path()) break;
  }
  return {};
}

}  // namespace

const std::vector<Game>& AllGames() { return kGames; }

void FindGames(std::vector<Found>& found, const fs::path& launcher_dir) {
  const auto ini = ReadIni(launcher_dir / kConfigFile);
  found = std::vector<Found>(kGames.size());
  for (size_t i = 0; i < kGames.size(); ++i) {
    found[i].exe = FindExe(kGames[i], launcher_dir, ini);
    if (found[i].exe.empty()) continue;
    FindRom(found[i].exe, found[i].workdir, found[i].rom);
    // ssarecompiled.exe reads ssarecomp.toml next to it.
    std::string stem = found[i].exe.stem().string();
    if (stem.ends_with("recompiled")) stem.resize(stem.size() - 4);
    found[i].settings = ReadIni(found[i].exe.parent_path() / (stem + ".toml"));
  }
  for (size_t i = 0; i < kGames.size(); ++i) {
    if (found[i].exe.empty()) continue;
    found[i].disc = DiscState::kChecking;
    std::thread([&found, i] {
      const fs::path xex = found[i].rom / "default.xex";
      if (!IsFile(xex)) {
        found[i].disc = DiscState::kMissing;
        return;
      }
      found[i].disc = Sha256(xex) == kGames[i].xex_sha256 ? DiscState::kMatches : DiscState::kOther;
    }).detach();
  }
}

bool StartGame(const Found& game, std::wstring& error, void** process) {
  STARTUPINFOW startup = {sizeof(startup)};
  PROCESS_INFORMATION info = {};
  std::wstring command = L"\"" + game.exe.wstring() + L"\"";
  if (!CreateProcessW(game.exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      game.workdir.c_str(), &startup, &info)) {
    wchar_t text[256] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), 0,
                   text, 255, nullptr);
    error = text;
    return false;
  }
  CloseHandle(info.hThread);
  *process = info.hProcess;
  return true;
}

fs::path FiguresDir() { return KnownFolder(FOLDERID_Documents) / "Skylanders" / "Figures"; }

int CountFigures() {
  int count = 0;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(FiguresDir(), ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    const auto ext = it->path().extension();
    if (it->is_regular_file(ec) && (ext == ".dump" || ext == ".sky" || ext == ".bin")) ++count;
  }
  return count;
}

}  // namespace coreoflight
