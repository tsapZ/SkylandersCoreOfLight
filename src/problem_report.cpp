#include "problem_report.h"

#ifdef _WIN32
#include <windows.h>
#include <dxgi.h>
#else
#include <sys/stat.h>
#include <sys/statvfs.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

#include "games.h"
#include "gpu_tier.h"
#include "hub_log.h"
#include "hub_settings.h"
#include "platform.h"
#include "private_words.h"
#include "xiso.h"

namespace coreoflight {
namespace fs = std::filesystem;
namespace {

// The report as one text file, each part under a "===== <part> =====" line: plain text opens in
// any browser or editor, and an issue on GitHub takes it as it is, with nothing to unpack (the
// owner, 2026-10-23: no archives from strangers).
class TextReport {
 public:
  void Add(const std::string& part, const std::string& text) {
    text_ += "\n===== " + part + " =====\n" + text;
    if (!text.empty() && text.back() != '\n') text_ += "\n";
  }
  bool Write(const fs::path& path) const {
    std::ofstream out(path, std::ios::binary);
    out << text_;
    return out.good();
  }

 private:
  std::string text_;
};

// A log's end is enough (a long session's log runs to many MB).
constexpr size_t kLogTail = 512u << 10;

// The user's home/profile folder (both slash directions), the user name and the computer's name.
std::vector<std::pair<std::string, std::string>> PrivateWords() {
  std::vector<std::pair<std::string, std::string>> words;
#ifdef _WIN32
  if (std::string profile = EnvVarUtf8("USERPROFILE"); !profile.empty()) {
    words.emplace_back(profile, "%USERPROFILE%");
    std::replace(profile.begin(), profile.end(), '\\', '/');
    words.emplace_back(profile, "%USERPROFILE%");
  }
  if (const std::string user = CurrentUserName(); user.size() >= 3) {
    words.emplace_back(user, "%USERNAME%");
  }
  if (const std::string pc = CurrentComputerName(); pc.size() >= 3) {
    words.emplace_back(pc, "%COMPUTERNAME%");
  }
#else
  if (const std::string home = EnvVarUtf8("HOME"); !home.empty()) {
    words.emplace_back(home, "$HOME");
  }
  if (const std::string user = CurrentUserName(); user.size() >= 3) {
    words.emplace_back(user, "$USER");
  }
  if (const std::string pc = CurrentComputerName(); pc.size() >= 3) {
    words.emplace_back(pc, "$HOSTNAME");
  }
#endif
  return words;
}

// A file's text, at most the last `cap` bytes of it (a long log's end is what counts).
std::string ReadTail(const fs::path& path, size_t cap = 8u << 20) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  if (size <= 0) return {};
  const std::streamoff from = size > std::streamoff(cap) ? size - std::streamoff(cap) : 0;
  in.seekg(from);
  std::string data(size_t(size - from), '\0');
  in.read(data.data(), std::streamsize(data.size()));
  if (from) data = "[... the first " + std::to_string(from) + " bytes left out ...]\n" + data;
  return data;
}

std::string ProcessorName() {
  const std::string name = CpuModelName();
  return name.empty() ? "?" : name;
}

std::string GraphicsCards() {
  std::string out;
#ifdef _WIN32
  IDXGIFactory1* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return "?\n";
  IDXGIAdapter1* adapter = nullptr;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 d{};
    adapter->GetDesc1(&d);
    LARGE_INTEGER driver{};
    char text[96];
    std::snprintf(text, sizeof(text), " (vendor 0x%04X, device 0x%04X, %llu MB)", d.VendorId, d.DeviceId,
                  static_cast<unsigned long long>(d.DedicatedVideoMemory >> 20));
    std::string line = "  " + PathUtf8(fs::path(std::wstring(d.Description))) + text;
    if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver))) {
      std::snprintf(text, sizeof(text), ", driver %u.%u.%u.%u", unsigned(HIWORD(driver.HighPart)),
                    unsigned(LOWORD(driver.HighPart)), unsigned(HIWORD(driver.LowPart)),
                    unsigned(LOWORD(driver.LowPart)));
      line += text;
    }
    line += "\n";
    // The same card can come twice (once per display it drives).
    if (out.find(line) == std::string::npos) out += line;
    adapter->Release();
  }
  factory->Release();
#else
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator("/sys/class/drm", ec)) {
    const std::string card = entry.path().filename().string();
    if (!card.starts_with("card") || card.find('-') != std::string::npos) continue;
    std::ifstream vf(entry.path() / "device" / "vendor"), df(entry.path() / "device" / "device");
    std::string vs, ds;
    if (!(vf >> vs) || !(df >> ds)) continue;
    uint64_t vram_mb = 0;
    std::ifstream vram_f(entry.path() / "device" / "mem_info_vram_total");
    uint64_t vram_bytes = 0;
    if (vram_f >> vram_bytes) vram_mb = vram_bytes >> 20;
    const unsigned long vendor = std::strtoul(vs.c_str(), nullptr, 0);
    const unsigned long device = std::strtoul(ds.c_str(), nullptr, 0);
    char text[128];
    if (vram_mb) {
      std::snprintf(text, sizeof(text), "  %s (vendor 0x%04lX, device 0x%04lX, %llu MB)\n", card.c_str(), vendor,
                    device, static_cast<unsigned long long>(vram_mb));
    } else {
      std::snprintf(text, sizeof(text), "  %s (vendor 0x%04lX, device 0x%04lX)\n", card.c_str(), vendor, device);
    }
    out += text;
  }
  const GpuInfo gpu = DetectGpu();
  if (!gpu.name.empty()) {
    char text[256];
    std::snprintf(text, sizeof(text), "  Active GL: %s (vendor 0x%04X, %llu MB)\n", gpu.name.c_str(), gpu.vendor,
                  static_cast<unsigned long long>(gpu.vram_mb));
    out += text;
  }
  if (out.empty()) out = "?\n";
#endif
  return out;
}

uint64_t TotalPhysicalMemoryMb() {
#ifdef _WIN32
  MEMORYSTATUSEX memory{};
  memory.dwLength = sizeof(memory);
  if (GlobalMemoryStatusEx(&memory)) return memory.ullTotalPhys >> 20;
  return 0;
#else
  std::ifstream in("/proc/meminfo");
  std::string label;
  uint64_t kb = 0;
  while (in >> label >> kb) {
    if (label == "MemTotal:") return kb >> 10;
    in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }
  return 0;
#endif
}

#ifndef _WIN32
// How full /dev/shm is and how much of it the games' guest memory holds: game versions before
// 2026-11-26 left their 4.5 GB object there after every run, and a full /dev/shm stopped the next
// start with a bus error (#135). "" where there is no /dev/shm (macOS).
std::string SharedMemoryLine() {
  struct statvfs vfs {};
  if (statvfs("/dev/shm", &vfs) != 0 || vfs.f_blocks == 0) return {};
  const uint64_t total = uint64_t(vfs.f_blocks) * vfs.f_frsize >> 20;
  const uint64_t used = uint64_t(vfs.f_blocks - vfs.f_bfree) * vfs.f_frsize >> 20;
  int files = 0;
  uint64_t held = 0;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator("/dev/shm", ec)) {
    if (entry.path().filename().string().rfind("xenia_memory_", 0) != 0) continue;
    struct stat st {};
    if (::stat(entry.path().c_str(), &st) == 0) held += uint64_t(st.st_blocks) * 512;
    ++files;
  }
  char line[160];
  std::snprintf(line, sizeof(line), "/dev/shm: %llu of %llu MB used, %d game memory file(s) holding %llu MB\n",
                static_cast<unsigned long long>(used), static_cast<unsigned long long>(total), files,
                static_cast<unsigned long long>(held >> 20));
  return line;
}
#endif

const char* DiscText(DiscState state) {
  switch (state) {
    case DiscState::kMatches:
      return "disc matches";
    case DiscState::kOther:
      return "another version or region of the disc";
    case DiscState::kMissing:
      return "no disc (default.xex missing)";
    case DiscState::kChecking:
      return "disc being checked";
    case DiscState::kUnknown:
      break;
  }
  return "disc not checked";
}

// The newest `count` files with `extension` in `dir`.
std::vector<fs::path> Newest(const fs::path& dir, const std::string& extension, size_t count) {
  std::vector<std::pair<fs::file_time_type, fs::path>> files;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (entry.is_regular_file(ec) && entry.path().extension() == extension) {
      files.emplace_back(entry.last_write_time(ec), entry.path());
    }
  }
  std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  std::vector<fs::path> out;
  for (size_t i = 0; i < files.size() && i < count; ++i) out.push_back(files[i].second);
  return out;
}

}  // namespace

fs::path WriteProblemReport(const std::vector<Found>& found, const fs::path& launcher_dir, bool german,
                            std::string& error) {
  const LocalDateTime t = NowLocalDateTime();
  char name[96];
  std::snprintf(name, sizeof(name), "%s-%04d-%02d-%02d-%02d%02d.txt", "Skylanders-problem-report", t.year, t.month,
                t.day, t.hour, t.minute);
  const fs::path filename = PathFromUtf8(name);

  const auto words = PrivateWords();
  auto clean = [&](std::string text) {
    for (const auto& [from, to] : words) ReplacePrivateWord(text, from, to);
    return text;
  };

  TextReport report;

  // What the PC is and which games the launcher found.
  std::string info;
  char line[256];
  std::snprintf(line, sizeof(line), "Core of Light " COREOFLIGHT_VERSION " problem report, %04d-%02d-%02d %02d:%02d\n\n",
                t.year, t.month, t.day, t.hour, t.minute);
  info += line;
#ifdef _WIN32
  info += "Windows " + WindowsVersion() + "\n";
#else
  info += OsVersionSummary() + "\n";
#endif
  info += "Processor: " + ProcessorName() + "\n";
  std::snprintf(line, sizeof(line), "Memory: %llu MB\n", static_cast<unsigned long long>(TotalPhysicalMemoryMb()));
  info += line;
#ifndef _WIN32
  info += SharedMemoryLine();
#endif
  info += "Graphics cards:\n" + GraphicsCards();
  info += "Launcher: " + PathUtf8(SelfExePath()) + "\n";
  // Where the saves and figures are (the one data folder since 0.2.0-beta.3, data_move.h).
  info += "Data folder: " + PathUtf8(SkylandersDir()) + "\n\nGames:\n";
  const auto& games = AllGames();
  for (size_t i = 0; i < games.size(); ++i) {
    const Game& game = games[i];
    info += std::string("  ") + game.name_en + ": ";
    if (i >= found.size() || found[i].exe.empty()) {
      info += "not found\n";
      continue;
    }
    const Found& f = found[i];
    const std::string version = InstalledVersion(game, f.exe);
    info += PathUtf8(f.exe) + (version.empty() ? " (own build)" : " (version " + version + ")") + ", " +
            DiscText(f.disc.load()) + "\n";
    // The disc's fingerprint: a pressing the port doesn't know yet (Imaginators' English / German
    // disc) can be added by it, as SuperChargers' second European disc was.
    std::ifstream xex(f.rom / "default.xex", std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(xex)), std::istreambuf_iterator<char>());
    if (!data.empty()) {
      const XexId id = IdentifyXex(data.data(), data.size());
      info += "    default.xex SHA-256 " + id.sha256 + "\n";
      if (!id.header.empty()) info += "    default.xex " + id.header + ", code " + id.code_pin + "\n";
    }
  }
  report.Add("info", clean(info));

  // Each game's settings file, its newest logs and problem notes.
  std::vector<fs::path> notes;
  for (size_t i = 0; i < games.size() && i < found.size(); ++i) {
    const Found& f = found[i];
    if (f.exe.empty()) continue;
    const std::string folder = std::string(games[i].key) + "/";
    const fs::path toml = f.exe.parent_path() / (UserDataName(f.exe) + ".toml");
    if (std::string text = ReadTail(toml); !text.empty()) report.Add(folder + PathUtf8(toml.filename()), clean(text));
    std::vector<fs::path> dirs = {f.workdir / "logs"};
    if (f.exe.parent_path() / "logs" != dirs[0]) dirs.push_back(f.exe.parent_path() / "logs");
    // The newest log, then the newest three of real sessions: the launcher's own short runs of a
    // game for its settings list leave logs of a few hundred bytes. Only those are left out: a
    // start that stopped at once (a refused disc, 2026-10-31) is short as well, and it is the log
    // that says why.
    size_t game_notes = 0;
    for (const fs::path& dir : dirs) {
      const std::vector<fs::path> logs = Newest(dir, ".log", 40);
      size_t taken = 0;
      for (size_t k = 0; k < logs.size() && taken < 4; ++k) {
        std::error_code ec;
        if (k > 0 && fs::file_size(logs[k], ec) < 4096 &&
            ReadTail(logs[k]).find("Settings schema written") != std::string::npos) {
          continue;
        }
        report.Add(folder + "logs/" + PathUtf8(logs[k].filename()), clean(ReadTail(logs[k], kLogTail)));
        ++taken;
      }
      // The game's own problem notes (its settings menu's Report a problem, skylanders-portal
      // problem_note.h): the newest three, their pictures next to the report.
      for (const fs::path& note : Newest(dir, ".txt", 40)) {
        if (PathUtf8(note.filename()).rfind("problem-", 0) != 0) continue;
        if (game_notes >= 3) break;
        report.Add(folder + "notes/" + PathUtf8(note.filename()), clean(ReadTail(note)));
        notes.push_back(note);
        ++game_notes;
      }
    }
  }

  // The setups' logs (%TEMP%\CoreOfLight\<key>-setup.log) and the launcher's own settings.
  if (const fs::path temp = TempDir(); !temp.empty()) {
    for (const fs::path& log : Newest(temp / "CoreOfLight", ".log", 12)) {
      report.Add("setup/" + PathUtf8(log.filename()), clean(ReadTail(log, kLogTail)));
    }
  }
  for (const fs::path& file : {HubSettingsFile(), launcher_dir / "coreoflight.ini"}) {
    if (std::string text = ReadTail(file); !text.empty()) report.Add("launcher/" + PathUtf8(file.filename()), clean(text));
  }
  // The launcher's own log (hub_log.h): discs, updates, games started and ended.
  if (std::string text = ReadTail(HubLogFile(), kLogTail); !text.empty()) report.Add("launcher/coreoflight.log", clean(text));

  const fs::path desktop = DesktopDir();
  const fs::path documents = DocumentsDir();
  fs::path report_path = desktop / filename;
  if (desktop.empty() || !report.Write(report_path)) {
    report_path = documents / filename;
    if (documents.empty() || documents == desktop || !report.Write(report_path)) {
      error = german ? "Konnte den Problembericht nicht schreiben" : "Could not write the problem report";
      return {};
    }
  }
  // The notes' pictures beside it, <report>-1.png, -2.png, ...
  int picture = 0;
  for (const fs::path& note : notes) {
    fs::path png = note;
    png.replace_extension(".png");
    std::error_code ec;
    if (!fs::exists(png, ec)) continue;
    fs::path to = report_path;
    to.replace_filename(PathFromUtf8(PathUtf8(report_path.stem()) + "-" + std::to_string(++picture) + ".png"));
    fs::copy_file(png, to, fs::copy_options::overwrite_existing, ec);
  }
  return report_path;
}

}  // namespace coreoflight
