#include "wiiu_figures.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

#include "game_settings.h"
#include "hub_log.h"
#include "platform.h"
#include "updates.h"
#include "zip_extract.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace coreoflight {
namespace fs = std::filesystem;

namespace {

// The package (maintainer/tools/wiiu_tools_package.py, CoreOfLight-WiiU-Tools-1: Python 3.14.10, numpy 2.5.5, scipy
// 1.18.1, ffmpeg n8.1 LGPL, superpatcher v0.1.2, imagipatcher v0.1.0-pre; 111 MB, 317 MB unpacked). A new tool
// version means a new package with a new name, URL and hash here. COREOFLIGHT_WIIU_PACKAGE=<zip> takes a local copy
// instead of the download (tests); its hash must match all the same.
constexpr char kPackageName[] = "CoreOfLight-WiiU-Tools-1";
constexpr char kPackageUrl[] =
    "https://github.com/tsapZ/CoreOfLight-WiiU-Tools/releases/download/v1/CoreOfLight-WiiU-Tools-1.zip";
constexpr char kPackageSha256[] = "68ab349edb5e72ea5f02c13b89306a3759c9617abda679ba1fc4b08f1424a907";

constexpr char kBackupDir[] = "wiiu_backup";      // in rom\archives
constexpr char kListFile[] = "wiiu_figures.txt";  // in rom\archives\wiiu_backup: "replaced <path>" / "added <path>"

struct Tool {
  const char* script;  // below the package
  const char* exe;     // the game's exe (superpatcher patched it on builds before the ["Mods"] slot sizes)
  const char* toml;    // the game's settings file next to it
  const char* name_en;
  const char* name_de;
};

const Tool& ToolFor(WiiuFigures::Game game) {
  static const Tool kSuperChargers{"superpatcher/superpatcher.py", "superchargersrecompiled.exe",
                                   "superchargersrecomp.toml", "SuperChargers", "SuperChargers"};
  static const Tool kImaginators{"imagipatcher/imagipatcher.py", "imaginatorsrecompiled.exe", "imaginatorsrecomp.toml",
                                 "Imaginators", "Imaginators"};
  return game == WiiuFigures::Game::kSuperChargers ? kSuperChargers : kImaginators;
}

void SetStep(WiiuFigures::Shared& s, std::string step) {
  std::lock_guard lock(s.mutex);
  s.step = std::move(step);
}

void Finish(WiiuFigures::Shared& s, std::string summary) {
  HubLog("wiiu figures: " + summary);
  {
    std::lock_guard lock(s.mutex);
    s.summary = std::move(summary);
  }
  s.progress = 1.0f;
  s.done = true;
}

fs::path PackageDir() { return LocalAppDataDir() / "CoreOfLight" / "wiiu-tools" / kPackageName; }

// The package unpacked in PackageDir(): there already (its manifest.json), else downloaded (or copied from
// COREOFLIGHT_WIIU_PACKAGE), checked against kPackageSha256 and unpacked. Progress 0..0.3.
bool EnsurePackage(WiiuFigures::Shared& s, bool german, std::string& error) {
  std::error_code ec;
  const fs::path dir = PackageDir();
  if (fs::exists(dir / "manifest.json", ec)) return true;
  const fs::path zip = DownloadsDir() / (std::string(kPackageName) + ".zip");
  fs::create_directories(zip.parent_path(), ec);
  if (FileSha256(zip) != kPackageSha256) {
    SetStep(s, german ? "Lade den Wii-U-Konverter (111 MB, nur einmal)" : "Getting the Wii U converter (111 MB, once)");
    const std::string local = EnvVarUtf8("COREOFLIGHT_WIIU_PACKAGE");
    if (!local.empty()) {
      fs::copy_file(PathFromUtf8(local), zip, fs::copy_options::overwrite_existing, ec);
    } else if (Offline()) {
      error = german ? "Offline: der Wii-U-Konverter wird nicht geladen." : "Offline: the Wii U converter isn't downloaded.";
      return false;
    } else {
      std::atomic<float> got{0.0f};
      std::atomic<bool> done{false};
      std::thread watch([&] {
        while (!done) {
          s.progress = 0.25f * got.load();
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
      });
      const bool ok = DownloadFile(kPackageUrl, zip, &got, nullptr, kPackageSha256);
      done = true;
      watch.join();
      if (!ok) {
        error = german ? "Der Wii-U-Konverter lie\xC3\x9F sich nicht laden (Internet?)."
                       : "Couldn't download the Wii U converter (internet?).";
        return false;
      }
    }
    if (FileSha256(zip) != kPackageSha256) {
      fs::remove(zip, ec);
      error = german ? "Der Wii-U-Konverter kam besch\xC3\xA4" "digt an." : "The Wii U converter arrived damaged.";
      return false;
    }
  }
  SetStep(s, german ? "Entpacke den Wii-U-Konverter" : "Unpacking the Wii U converter");
  const fs::path unpack = dir.parent_path() / ".unpack";
  fs::remove_all(unpack, ec);
  std::atomic<float> part{0.0f};
  std::string why;
  if (!ExtractZip(zip, unpack, &part, why) || !fs::exists(unpack / kPackageName / "manifest.json", ec)) {
    HubWarn("wiiu figures: unpacking failed: " + why);
    fs::remove_all(unpack, ec);
    error = german ? "Der Wii-U-Konverter lie\xC3\x9F sich nicht entpacken." : "Couldn't unpack the Wii U converter.";
    return false;
  }
  fs::remove_all(dir, ec);
  std::error_code moved;
  fs::rename(unpack / kPackageName, dir, moved);
  fs::remove_all(unpack, ec);
  if (moved) {
    HubWarn("wiiu figures: moving the unpacked converter failed: " + moved.message());
    error = german ? "Der Wii-U-Konverter lie\xC3\x9F sich nicht ablegen." : "Couldn't put the Wii U converter in place.";
    return false;
  }
  fs::remove(zip, ec);
  s.progress = 0.3f;
  return true;
}

#ifdef _WIN32
// A command line argument quoted the way CommandLineToArgvW reads it back.
std::wstring Quote(const std::wstring& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
  std::wstring out = L"\"";
  size_t slashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') {
      ++slashes;
      continue;
    }
    out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    slashes = 0;
    out += c;
  }
  out.append(slashes * 2, L'\\');
  return out + L"\"";
}

// This process's environment for the tool: PATH with the package's ffmpeg first, no PYTHON* settings of the
// player's own Python, its output unbuffered and in UTF-8.
std::wstring ToolEnvironment(const fs::path& ffmpeg_dir) {
  std::vector<std::wstring> vars;
  if (wchar_t* block = GetEnvironmentStringsW()) {
    for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
      std::wstring v = p;
      std::wstring upper = v;
      std::transform(upper.begin(), upper.end(), upper.begin(), ::towupper);
      if (upper.rfind(L"PYTHON", 0) == 0) continue;
      if (upper.rfind(L"PATH=", 0) == 0) v = L"PATH=" + ffmpeg_dir.wstring() + L";" + v.substr(5);
      vars.push_back(std::move(v));
    }
    FreeEnvironmentStringsW(block);
  }
  vars.push_back(L"PYTHONUNBUFFERED=1");
  vars.push_back(L"PYTHONIOENCODING=utf-8");
  std::sort(vars.begin(), vars.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
  std::wstring env;
  for (const std::wstring& v : vars) env += v + L'\0';
  return env + L'\0';
}

// Runs the package's Python on `args` in the package folder, without a window; every line it prints (output and
// errors) goes to `line`. Its exit code, -1 if it didn't start.
int RunPython(const std::vector<std::wstring>& args, const std::function<void(const std::string&)>& line) {
  const fs::path dir = PackageDir();
  std::wstring command = Quote((dir / "python" / "python.exe").wstring());
  for (const std::wstring& a : args) command += L" " + Quote(a);
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE read = nullptr, write = nullptr;
  if (!CreatePipe(&read, &write, &sa, 0)) return -1;
  SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
  HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = nul;
  si.hStdOutput = write;
  si.hStdError = write;
  PROCESS_INFORMATION pi{};
  std::wstring env = ToolEnvironment(dir / "ffmpeg");
  const std::wstring cwd = dir.wstring();
  const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, env.data(), cwd.c_str(), &si, &pi);
  CloseHandle(write);
  if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
  if (!started) {
    CloseHandle(read);
    return -1;
  }
  // The tool (and the ffmpeg it starts) ends with the launcher: a job that kills its processes when its last handle
  // closes, which this process holds until it exits.
  static HANDLE job = [] {
    HANDLE j = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (j) SetInformationJobObject(j, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    return j;
  }();
  if (job) AssignProcessToJobObject(job, pi.hProcess);
  char buffer[4096];
  DWORD got = 0;
  std::string pending;
  while (ReadFile(read, buffer, sizeof(buffer), &got, nullptr) && got) {
    pending.append(buffer, got);
    size_t at;
    while ((at = pending.find_first_of("\r\n")) != std::string::npos) {
      if (at) line(pending.substr(0, at));
      pending.erase(0, at + 1);
    }
  }
  if (!pending.empty()) line(pending);
  CloseHandle(read);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return int(code);
}
#endif

// The tool's lines in the log and the last one as the step ("PROGRESS 12/40 text" lines, if the tool prints them, set
// the progress between `from` and `to`); the last ones kept for an error message.
struct ToolOutput {
  WiiuFigures::Shared& s;
  float from, to;
  std::vector<std::string> last;
  void operator()(const std::string& text) {
    HubLog("wiiu tool: " + text);
    std::string shown = text;
    if (text.rfind("PROGRESS ", 0) == 0) {
      const int done = std::atoi(text.c_str() + 9);
      const size_t slash = text.find('/');
      const int total = slash == std::string::npos ? 0 : std::atoi(text.c_str() + slash + 1);
      if (total > 0) s.progress = from + (to - from) * std::clamp(float(done) / float(total), 0.0f, 1.0f);
      const size_t space = text.find(' ', 9);
      shown = space == std::string::npos ? std::string() : text.substr(space + 1);
    }
    if (!shown.empty()) SetStep(s, shown.size() > 90 ? shown.substr(0, 87) + "..." : shown);
    last.push_back(text);
    if (last.size() > 4) last.erase(last.begin());
  }
};

fs::path RomOf(const fs::path& game_dir) { return game_dir / "rom"; }

// Puts the archives listed in wiiu_figures.txt back (originals from the backup, added files removed) and drops the
// backup. True if there was nothing to undo or all of it went back.
bool RestoreArchives(const fs::path& rom) {
  std::error_code ec;
  const fs::path archives = rom / "archives", backup = archives / kBackupDir;
  std::ifstream list(backup / kListFile);
  if (!list) return true;
  bool ok = true;
  std::string line;
  while (std::getline(list, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t space = line.find(' ');
    if (space == std::string::npos) continue;
    const std::string what = line.substr(0, space);
    const fs::path rel = PathFromUtf8(line.substr(space + 1));
    if (what == "added") {
      fs::remove(archives / rel, ec);
    } else if (what == "replaced") {
      fs::copy_file(backup / rel, archives / rel, fs::copy_options::overwrite_existing, ec);
      if (ec) {
        HubWarn("wiiu figures: couldn't put back " + PathUtf8(rel) + ": " + ec.message());
        ok = false;
      }
    }
  }
  list.close();
  if (ok) fs::remove_all(backup, ec);
  return ok;
}

// Copies the converted archives into the game, keeping each original once and listing what changed.
bool InstallArchives(const fs::path& converted, const fs::path& rom, std::string& error) {
  std::error_code ec;
  const fs::path archives = rom / "archives", backup = archives / kBackupDir;
  fs::create_directories(backup, ec);
  std::ofstream list(backup / kListFile, std::ios::app);
  for (auto it = fs::recursive_directory_iterator(converted, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file(ec)) continue;
    const fs::path rel = fs::relative(it->path(), converted, ec);
    const fs::path target = archives / rel;
    const bool had = fs::exists(target, ec);
    if (had && !fs::exists(backup / rel, ec)) {
      fs::create_directories((backup / rel).parent_path(), ec);
      fs::copy_file(target, backup / rel, ec);
      if (ec) {
        error = "backup of " + PathUtf8(rel) + ": " + ec.message();
        return false;
      }
    }
    fs::create_directories(target.parent_path(), ec);
    fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      error = "copy of " + PathUtf8(rel) + ": " + ec.message();
      return false;
    }
    list << (had ? "replaced " : "added ") << GenericPathUtf8(rel) << "\n";
  }
  return bool(list.flush());
}

// Everything Start changed, back: the archives, the .igx files imagipatcher's --apply-config rewrote (their .orig
// copies; the disc's own data, which an update of the game doesn't replace), and the settings it turned on. Not the
// exe: superpatcher patched it only on builds before the ["Mods"] slot sizes, and an update may have replaced it
// since, so its .orig could be an older exe. Not the .toml's .orig either (newer settings of the player's own).
bool RestoreAll(WiiuFigures::Game game, const fs::path& game_dir) {
  const Tool& tool = ToolFor(game);
  const fs::path rom = RomOf(game_dir);
  bool ok = RestoreArchives(rom);
  std::error_code ec;
  for (const fs::path& changed : {rom / "MemoryPoolConfigs.igx", rom / "VramBudget.igx"}) {
    const fs::path orig = WithPathSuffix(changed, ".orig");
    if (!fs::exists(orig, ec)) continue;
    fs::copy_file(orig, changed, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      HubWarn("wiiu figures: couldn't put back " + PathUtf8(changed.filename()) + ": " + ec.message());
      ok = false;
    } else {
      fs::remove(orig, ec);
    }
  }
  const fs::path toml = game_dir / tool.toml;
  WriteGameSetting(toml, "Portal", "portal_menu_nintendo", "bool", "false");
  if (game == WiiuFigures::Game::kSuperChargers) {
    WriteGameSetting(toml, "Mods", "hero_data_slot_size", "int", "0");
    WriteGameSetting(toml, "Mods", "vehicle_slot_size", "int", "0");
  }
  return ok;
}

}  // namespace

bool WiiuFigures::LooksLikeWiiuGame(const fs::path& folder) {
  std::error_code ec;
  return fs::is_directory(folder / "content" / "archives", ec) || fs::is_directory(folder / "archives", ec) ||
         folder.filename() == "archives";
}

bool WiiuFigures::Installed(const fs::path& rom) {
  std::error_code ec;
  return fs::exists(rom / "archives" / kBackupDir / kListFile, ec);
}

std::string WiiuFigures::step() const {
  if (!shared_) return {};
  std::lock_guard lock(shared_->mutex);
  return shared_->step;
}

std::string WiiuFigures::TakeSummary() {
  if (!shared_ || !shared_->done) return {};
  std::lock_guard lock(shared_->mutex);
  if (shared_->taken) return {};
  shared_->taken = true;
  return shared_->summary;
}

void WiiuFigures::Start(Game game, const fs::path& wiiu, const fs::path& game_dir, bool german) {
  shared_ = std::make_shared<Shared>();
  std::thread([s = shared_, game, wiiu, game_dir, german] {
#ifndef _WIN32
    (void)game, (void)wiiu, (void)game_dir;
    Finish(*s, german ? "Die Wii-U-Figuren gibt es bisher nur unter Windows." : "The Wii U figures are Windows only for now.");
#else
    const Tool& tool = ToolFor(game);
    std::string error;
    if (!EnsurePackage(*s, german, error)) return Finish(*s, error);
    const fs::path rom = RomOf(game_dir);
    // A second run starts from the game's own files (else the tool's .orig copies would become the patched ones).
    if (Installed(rom) && !RestoreAll(game, game_dir)) {
      return Finish(*s, german ? "Die Dateien vom letzten Mal lie\xC3\x9F" "en sich nicht zur\xC3\xBC" "cklegen."
                               : "Couldn't put back the files from last time.");
    }
    std::error_code ec;
    const fs::path work = TempDir() / "CoreOfLight" / (std::string("wiiu-") + tool.name_en);
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);
    SetStep(*s, german ? "Wandle deine Wii-U-Dateien um (ein paar Minuten)" : "Converting your Wii U files (a few minutes)");
    ToolOutput out{*s, 0.3f, 0.85f, {}};
    // No PROGRESS lines from the tool: creep towards 0.8 so the bar moves (most runs take 2-5 minutes).
    std::atomic<bool> converting{true};
    std::thread creep([&] {
      while (converting) {
        const float p = s->progress.load();
        if (p < 0.8f) s->progress = p + (0.8f - p) * 0.004f;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
    });
    const fs::path pkg = PackageDir();
    const int code = RunPython({(pkg / tool.script).wstring(), L"--wiiu", wiiu.wstring(), L"--x360", game_dir.wstring(),
                                L"--output", work.wstring()},
                               std::ref(out));
    converting = false;
    creep.join();
    if (code != 0) {
      std::string why = out.last.empty() ? std::string() : out.last.back();
      return Finish(*s, (german ? "Die Umwandlung ging nicht: " : "The conversion didn't work: ") +
                            (code < 0 ? std::string(german ? "Python startete nicht." : "Python didn't start.") : why));
    }
    SetStep(*s, german ? "Lege die Figuren ins Spiel" : "Putting the figures into the game");
    s->progress = 0.88f;
    if (!InstallArchives(work / "archives", rom, error)) {
      HubWarn("wiiu figures: " + error);
      RestoreArchives(rom);
      return Finish(*s, german ? "Die Dateien lie\xC3\x9F" "en sich nicht ins Spiel legen; es ist wieder wie vorher."
                               : "Couldn't put the files into the game; it is back as it was.");
    }
    // The tool's own settings step on the game folder (memory, the video memory budget where it sets one, the
    // portal menu's Nintendo row), then that row on here too (superpatcher sets it only in its test copies).
    SetStep(*s, german ? "Stelle den Speicher ein" : "Setting the memory");
    ToolOutput config{*s, 0.9f, 0.98f, {}};
    const wchar_t* config_flag = game == Game::kSuperChargers ? L"--patch-exe" : L"--apply-config";
    const int config_code = RunPython({(pkg / tool.script).wstring(), config_flag, game_dir.wstring()}, std::ref(config));
    WriteGameSetting(game_dir / tool.toml, "Portal", "portal_menu_nintendo", "bool", "true");
    fs::remove_all(work, ec);
    if (config_code != 0) {
      const std::string why = config.last.empty() ? std::string() : config.last.back();
      return Finish(*s, std::string(german ? "Die Figuren sind drin, aber der Speicher lie\xC3\x9F sich nicht einstellen: "
                                           : "The figures are in, but the memory couldn't be set: ") + why);
    }
    Finish(*s, german ? std::string("Die Wii-U-Figuren sind in ") + tool.name_de +
                            ": im Portalmen\xC3\xBC unter Nintendo."
                      : std::string("The Wii U figures are in ") + tool.name_en + ": in the portal menu under Nintendo.");
#endif
  }).detach();
}

void WiiuFigures::StartRestore(Game game, const fs::path& game_dir, bool german) {
  shared_ = std::make_shared<Shared>();
  std::thread([s = shared_, game, game_dir, german] {
    const Tool& tool = ToolFor(game);
    SetStep(*s, german ? "Lege die Originaldateien zur\xC3\xBC" "ck" : "Putting the original files back");
    const bool ok = RestoreAll(game, game_dir);
    Finish(*s, ok ? (german ? std::string(tool.name_de) + " ist wieder wie vorher." : std::string(tool.name_en) + " is back as it was.")
                  : (german ? "Nicht alles lie\xC3\x9F sich zur\xC3\xBC" "cklegen; siehe coreoflight.log."
                            : "Not everything could be put back; see coreoflight.log."));
  }).detach();
}

}  // namespace coreoflight
