#include "wiiu_figures.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

#include "hub_log.h"
#include "language_import.h"
#include "platform.h"
#include "updates.h"
#include "wua_unpack.h"
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

// The package (maintainer/tools/wiiu_tools_package.py, CoreOfLight-WiiU-Tools-3: Python 3.14.10, numpy 2.5.5, scipy
// 1.18.1, ffmpeg n8.1 LGPL, superpatcher v0.2.0 (cd2aa50), imagipatcher v0.2.2 (20a7d11: sets game_heap_extra on
// Imaginators builds that read it, and finds the Windows builds by their "Mods: game heap" log line, since they keep
// the setting's name in the code; v0.2.1 missed them, SkyP4tcher 2026-11-20); 111 MB). A new tool
// version means a new package with a new name, URL and hash here (an unpacked package is never looked at again).
// COREOFLIGHT_WIIU_PACKAGE=<zip> takes a local copy instead of the download (tests); its hash must match all the same.
constexpr char kPackageName[] = "CoreOfLight-WiiU-Tools-3";
constexpr char kPackageUrl[] =
    "https://github.com/tsapZ/CoreOfLight-WiiU-Tools/releases/download/v3/CoreOfLight-WiiU-Tools-3.zip";
constexpr char kPackageSha256[] = "cddf3ae1e064ab6eab70f648cba4353b9f585f225cbdb4590741d9ca669ed836";

// The tools do the whole job in the game folder themselves (SkyP4tcher's v0.2.0, made for this button, CoL #56):
// `--wiiu <folder> --install <game folder>` converts, copies into rom\archives, sets the .igx files and the .toml
// settings (on SuperChargers the slot sizes, or the exe on builds without them) and keeps everything it replaces in
// <backup>\ with a manifest.json written before each change, so even a cut-short install can be undone; installing
// again undoes the last one first. `--uninstall <game folder>` puts back what is still as the install left it (a file
// an update replaced since stays) and removes the backup. Both print "PROGRESS n/total label" lines.
struct Tool {
  const char* script;  // below the package
  const char* backup;  // the tool's backup folder in the game folder; its manifest.json = installed
  const char* name_en;
  const char* name_de;
};

const Tool& ToolFor(WiiuFigures::Game game) {
  static const Tool kSuperChargers{"superpatcher/superpatcher.py", "superpatcher-backup", "SuperChargers",
                                   "SuperChargers"};
  static const Tool kImaginators{"imagipatcher/imagipatcher.py", "imagipatcher-backup", "Imaginators", "Imaginators"};
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
  // Suspended until it is in the job below, so nothing it starts escapes the job.
  const BOOL started =
      CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                     CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, env.data(), cwd.c_str(), &si, &pi);
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
  ResumeThread(pi.hThread);
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

// The tool's last line for an error message, short enough for the toast (a missing-files error lists dozens; the
// whole line is in coreoflight.log).
std::string LastLine(const ToolOutput& out) {
  if (out.last.empty()) return {};
  const std::string& line = out.last.back();
  return line.size() > 160 ? line.substr(0, 157) + "..." : line;
}

}  // namespace

bool WiiuFigures::LooksLikeWiiuGame(const fs::path& folder) {
  std::error_code ec;
  return fs::is_directory(folder / "content" / "archives", ec) || fs::is_directory(folder / "archives", ec) ||
         folder.filename() == "archives";
}

bool WiiuFigures::Installed(const fs::path& rom) {
  std::error_code ec;
  const fs::path game = rom.parent_path();
  for (WiiuFigures::Game g : {Game::kSuperChargers, Game::kImaginators}) {
    if (fs::exists(game / ToolFor(g).backup / "manifest.json", ec)) return true;
  }
  return false;
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

void WiiuFigures::Start(Game game, const fs::path& wiiu, const fs::path& rom, const fs::path& exe_dir, bool german) {
  shared_ = std::make_shared<Shared>();
  std::thread([s = shared_, game, wiiu, rom, exe_dir, german] {
#ifndef _WIN32
    (void)game, (void)wiiu, (void)rom, (void)exe_dir;
    Finish(*s, german ? "Die Wii-U-Figuren gibt es bisher nur unter Windows." : "The Wii U figures are Windows only for now.");
#else
    const Tool& tool = ToolFor(game);
    std::string error;
    if (!EnsurePackage(*s, german, error)) return Finish(*s, error);
    // A .wua (Cemu's compressed Wii U game, CoL #166): the game's content/archives unpacked into a scratch folder first
    // (wua_unpack.h), which goes again before the summary shows, also when the tool failed; one an earlier run left
    // behind (the launcher closed half way) goes at the start.
    const fs::path scratch = PackageDir().parent_path() / ".wua-game";
    std::error_code scratch_ec;
    fs::remove_all(scratch, scratch_ec);
    auto end = [&](std::string summary) {
      std::error_code ec;
      fs::remove_all(scratch, ec);
      Finish(*s, std::move(summary));
    };
    fs::path source = wiiu;
    float tool_from = 0.3f;
    if (IsWuaFile(wiiu)) {
      SetStep(*s, german ? "Entpacke deine .wua (ein paar Minuten)" : "Unpacking your .wua (a few minutes)");
      std::atomic<float> part{0.0f};
      std::atomic<bool> unpacking{true};
      std::thread watch([&] {
        while (unpacking) {
          s->progress = 0.3f + 0.15f * part.load();
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
      });
      std::string why;
      const bool unpacked = UnpackWuaArchives(wiiu, scratch, "Bowser.pak", &part, nullptr, why);
      unpacking = false;
      watch.join();
      if (!unpacked) {
        HubWarn("wiiu figures: " + PathUtf8(wiiu.filename()) + ": " + why);
        return end((german ? "Die .wua lie\xC3\x9F sich nicht entpacken: " : "Couldn't unpack the .wua: ") + why);
      }
      HubLog("wiiu figures: " + PathUtf8(wiiu.filename()) + ": content/archives unpacked");
      source = scratch;
      tool_from = 0.45f;
    }
    // Imaginators with another language imported (CoL #124): the earlier pointing of Bowser's and Donkey Kong's texts
    // undone first (the tool leaves a file alone that changed since its install), and the Wii U files of that language
    // kept next to the disc's own, so their texts can show it too.
    std::vector<std::wstring> args = {(PackageDir() / tool.script).wstring(), L"--wiiu", source.wstring(), L"--install",
                                      exe_dir.wstring()};
    std::string imported;
    if (game == Game::kImaginators) {
      if (!UndoWiiuText(rom)) {
        return end(german ? "Imaginators l\xC3\xA4sst sich gerade nicht \xC3\xA4ndern (l\xC3\xA4uft es noch?)."
                          : "Imaginators can't be changed right now (is it still running?).");
      }
      imported = ImportedLanguage(rom);
      std::vector<std::string> languages = ImaginatorsInstallLanguages(rom);
      if (imported.size() == 2 && !languages.empty() &&
          std::find(languages.begin(), languages.end(), imported) == languages.end()) {
        std::wstring list(imported.begin(), imported.end());
        for (const std::string& l : languages) list += L"," + std::wstring(l.begin(), l.end());
        args.push_back(L"--languages");
        args.push_back(list);
        HubLog("Wii U figures: with the imported " + imported + " text");
      } else {
        imported.clear();
      }
    }
    SetStep(*s, german ? "Wandle deine Wii-U-Dateien um (ein paar Minuten)" : "Converting your Wii U files (a few minutes)");
    ToolOutput out{*s, tool_from, 0.99f, {}};
    int code = RunPython(args, std::ref(out));
    if (code > 0 && !imported.empty()) {
      // A Wii U game without that language (its files missing): converted as before, with the disc's languages only.
      HubWarn("Wii U figures: not with " + imported + " (" + LastLine(out) + "), again with the disc's languages");
      args.resize(args.size() - 2);
      imported.clear();
      code = RunPython(args, std::ref(out));
    }
    if (code != 0) {
      const std::string why = LastLine(out);
      // A stop half way is undone by the take-out button: the tool wrote its manifest before each change.
      return end((german ? "Das ging nicht: " : "That didn't work: ") +
                 (code < 0 ? std::string(german ? "Python startete nicht." : "Python didn't start.") : why));
    }
    if (!imported.empty()) {
      const int texts = PointWiiuTextAtImport(rom);
      HubLog("Wii U figures: " + std::to_string(texts) + " text(s) in " + imported);
    }
    end(german ? std::string("Die Wii-U-Figuren sind in ") + tool.name_de + ": im Portalmen\xC3\xBC unter Nintendo."
               : std::string("The Wii U figures are in ") + tool.name_en + ": in the portal menu under Nintendo.");
#endif
  }).detach();
}

void WiiuFigures::StartRestore(Game game, const fs::path& rom, const fs::path& exe_dir, bool german) {
  shared_ = std::make_shared<Shared>();
  std::thread([s = shared_, game, rom, exe_dir, german] {
#ifndef _WIN32
    (void)game, (void)rom, (void)exe_dir;
    Finish(*s, german ? "Die Wii-U-Figuren gibt es bisher nur unter Windows." : "The Wii U figures are Windows only for now.");
#else
    const Tool& tool = ToolFor(game);
    std::string error;
    if (!EnsurePackage(*s, german, error)) return Finish(*s, error);
    // Bowser's and Donkey Kong's texts pointed at the imported language (CoL #124) back first, or the tool would
    // leave their changed files in.
    if (game == Game::kImaginators && !UndoWiiuText(rom)) {
      return Finish(*s, german ? "Imaginators l\xC3\xA4sst sich gerade nicht \xC3\xA4ndern (l\xC3\xA4uft es noch?)."
                               : "Imaginators can't be changed right now (is it still running?).");
    }
    SetStep(*s, german ? "Lege die Originaldateien zur\xC3\xBC" "ck" : "Putting the original files back");
    ToolOutput out{*s, 0.3f, 0.99f, {}};
    bool kept = false;
    const int code = RunPython({(PackageDir() / tool.script).wstring(), L"--uninstall", exe_dir.wstring()},
                               [&](const std::string& text) {
                                 out(text);
                                 if (text.find("left as it is") != std::string::npos) kept = true;
                               });
    if (code != 0) {
      const std::string why = LastLine(out);
      return Finish(*s, (german ? "Nicht alles lie\xC3\x9F sich zur\xC3\xBC" "cklegen: " : "Not everything could be put back: ") + why);
    }
    // A file an update replaced since the install stays as it is (the tool's report names it).
    Finish(*s, german ? std::string(tool.name_de) + " ist wieder wie vorher." +
                            (kept ? " Was ein Update seitdem ersetzt hat, bleibt so (coreoflight.log)." : "")
                      : std::string(tool.name_en) + " is back as it was." +
                            (kept ? " What an update replaced since stays as it is (coreoflight.log)." : ""));
#endif
  }).detach();
}

}  // namespace coreoflight
