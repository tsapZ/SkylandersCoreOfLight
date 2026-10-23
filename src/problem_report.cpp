#include "problem_report.h"

#include <windows.h>
#include <dxgi.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <utility>

#include "games.h"
#include "hub_settings.h"
#include "xiso.h"

namespace coreoflight {
namespace fs = std::filesystem;
namespace {

std::string Utf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(std::max(n, 0)), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::string Utf8(const fs::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}

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

// Replaces every `from` (ASCII letters in any case) with `to`.
void ReplaceAll(std::string& text, const std::string& from, const std::string& to) {
  if (from.empty()) return;
  auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
  std::string out;
  out.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    bool match = i + from.size() <= text.size();
    for (size_t k = 0; match && k < from.size(); ++k) match = lower(text[i + k]) == lower(from[k]);
    if (match) {
      out += to;
      i += from.size();
    } else {
      out += text[i++];
    }
  }
  text = std::move(out);
}

// The user's profile folder (both slash directions), the user name and the computer's name.
std::vector<std::pair<std::string, std::string>> PrivateWords() {
  std::vector<std::pair<std::string, std::string>> words;
  wchar_t buffer[MAX_PATH] = {};
  DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buffer, MAX_PATH);
  if (n && n < MAX_PATH) {
    std::string profile = Utf8(std::wstring(buffer, n));
    words.emplace_back(profile, "%USERPROFILE%");
    std::replace(profile.begin(), profile.end(), '\\', '/');
    words.emplace_back(profile, "%USERPROFILE%");
  }
  n = MAX_PATH;
  if (GetUserNameW(buffer, &n) && n > 1) {
    const std::string user = Utf8(std::wstring(buffer, n - 1));
    if (user.size() >= 3) words.emplace_back(user, "%USERNAME%");
  }
  n = MAX_PATH;
  if (GetComputerNameW(buffer, &n) && n) {
    const std::string pc = Utf8(std::wstring(buffer, n));
    if (pc.size() >= 3) words.emplace_back(pc, "%COMPUTERNAME%");
  }
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

std::string WindowsVersion() {
  using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
  OSVERSIONINFOW v{};
  v.dwOSVersionInfoSize = sizeof(v);
  if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
    if (auto get = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) get(&v);
  }
  char text[64];
  std::snprintf(text, sizeof(text), "%lu.%lu.%lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
  return text;
}

std::string ProcessorName() {
  wchar_t name[256] = {};
  DWORD size = sizeof(name);
  if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                   L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, name, &size) != ERROR_SUCCESS) {
    return "?";
  }
  std::string text = Utf8(std::wstring(name));
  while (!text.empty() && text.back() == ' ') text.pop_back();
  return text;
}

std::string GraphicsCards() {
  std::string out;
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
    std::string line = "  " + Utf8(std::wstring(d.Description)) + text;
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
  return out;
}

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
std::vector<fs::path> Newest(const fs::path& dir, const std::wstring& extension, size_t count) {
  std::vector<std::pair<fs::file_time_type, fs::path>> files;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (entry.is_regular_file(ec) && entry.path().extension().wstring() == extension) {
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
  PWSTR desktop_w = nullptr;
  fs::path desktop;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop_w))) desktop = desktop_w;
  CoTaskMemFree(desktop_w);
  if (desktop.empty()) desktop = DocumentsDir();
  SYSTEMTIME t{};
  GetLocalTime(&t);
  wchar_t name[96];
  std::swprintf(name, std::size(name), L"%ls-%04u-%02u-%02u-%02u%02u.txt",
                german ? L"Skylanders-Problembericht" : L"Skylanders-problem-report", t.wYear, t.wMonth, t.wDay,
                t.wHour, t.wMinute);
  const fs::path report_path = desktop / name;

  const auto words = PrivateWords();
  auto clean = [&](std::string text) {
    for (const auto& [from, to] : words) ReplaceAll(text, from, to);
    return text;
  };

  TextReport report;

  // What the PC is and which games the launcher found.
  std::string info;
  char line[256];
  std::snprintf(line, sizeof(line), "Core of Light problem report, %04u-%02u-%02u %02u:%02u\n\n", t.wYear,
                t.wMonth, t.wDay, t.wHour, t.wMinute);
  info += line;
  info += "Windows " + WindowsVersion() + "\n";
  info += "Processor: " + ProcessorName() + "\n";
  MEMORYSTATUSEX memory{};
  memory.dwLength = sizeof(memory);
  GlobalMemoryStatusEx(&memory);
  std::snprintf(line, sizeof(line), "Memory: %llu MB\n", static_cast<unsigned long long>(memory.ullTotalPhys >> 20));
  info += line;
  info += "Graphics cards:\n" + GraphicsCards();
  wchar_t exe[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  info += "Launcher: " + Utf8(fs::path(exe)) + "\n\nGames:\n";
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
    info += Utf8(f.exe) + (version.empty() ? " (own build)" : " (version " + version + ")") + ", " +
            DiscText(f.disc.load()) + "\n";
    // The disc's fingerprint: a pressing the port doesn't know yet (Imaginators' English / German
    // disc) can be added by it, as SuperChargers' second European disc was.
    std::ifstream xex(f.rom / "default.xex", std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(xex)), std::istreambuf_iterator<char>());
    if (!data.empty()) info += "    default.xex SHA-256 " + Sha256Hex(data.data(), data.size()) + "\n";
  }
  report.Add("info", clean(info));

  // Each game's settings file and its three newest logs.
  for (size_t i = 0; i < games.size() && i < found.size(); ++i) {
    const Found& f = found[i];
    if (f.exe.empty()) continue;
    const std::string folder = std::string(games[i].key) + "/";
    const fs::path toml = f.exe.parent_path() / (UserDataName(f.exe) + ".toml");
    if (std::string text = ReadTail(toml); !text.empty()) report.Add(folder + Utf8(toml.filename()), clean(text));
    std::vector<fs::path> dirs = {f.workdir / "logs"};
    if (f.exe.parent_path() / "logs" != dirs[0]) dirs.push_back(f.exe.parent_path() / "logs");
    // The newest log, then the newest three of real sessions: the launcher's own short runs of a
    // game (its settings list, its pictures) leave logs of a few hundred bytes.
    for (const fs::path& dir : dirs) {
      const std::vector<fs::path> logs = Newest(dir, L".log", 40);
      size_t taken = 0;
      for (size_t k = 0; k < logs.size() && taken < 4; ++k) {
        std::error_code ec;
        if (k > 0 && fs::file_size(logs[k], ec) < 4096) continue;
        report.Add(folder + "logs/" + Utf8(logs[k].filename()), clean(ReadTail(logs[k], kLogTail)));
        ++taken;
      }
    }
  }

  // The setups' logs (%TEMP%\CoreOfLight\<key>-setup.log) and the launcher's own settings.
  wchar_t temp[MAX_PATH] = {};
  if (GetTempPathW(MAX_PATH, temp)) {
    for (const fs::path& log : Newest(fs::path(temp) / "CoreOfLight", L".log", 12)) {
      report.Add("setup/" + Utf8(log.filename()), clean(ReadTail(log, kLogTail)));
    }
  }
  for (const fs::path& file : {HubSettingsFile(), launcher_dir / "coreoflight.ini"}) {
    if (std::string text = ReadTail(file); !text.empty()) report.Add("launcher/" + Utf8(file.filename()), clean(text));
  }

  if (!report.Write(report_path)) {
    error = german ? "Konnte den Bericht nicht auf den Desktop schreiben" : "Could not write the report to the desktop";
    return {};
  }
  return report_path;
}

}  // namespace coreoflight
