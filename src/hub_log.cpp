#include "hub_log.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

#include "platform.h"

namespace coreoflight {

namespace fs = std::filesystem;

fs::path HubLogFile() {
  const fs::path local = LocalAppDataDir();
  return local.empty() ? fs::path() : local / "CoreOfLight" / "coreoflight.log";
}

namespace {

// The player's data out of every line (2026-10-30: logs end up in public reports), as the games'
// SDK does it (rexglue-sdk core/logging.cpp RedactUserData): the user folder becomes %USERPROFILE%,
// any other user's folder after \Users\ <user>, the user's and the machine's names <user> and
// <computer>.
char FoldChar(char c) { return c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool MatchesAt(const std::string& text, size_t at, std::string_view what) {
  if (what.empty() || at + what.size() > text.size()) return false;
  for (size_t i = 0; i < what.size(); ++i) {
    if (FoldChar(text[at + i]) != FoldChar(what[i])) return false;
  }
  return true;
}

bool IsWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

void ReplaceAll(std::string& text, std::string_view what, std::string_view with, bool whole_word) {
  for (size_t at = 0; at + what.size() <= text.size();) {
    const bool word_ok = !whole_word || ((at == 0 || !IsWordChar(text[at - 1])) &&
                                         (at + what.size() == text.size() || !IsWordChar(text[at + what.size()])));
    if (MatchesAt(text, at, what) && word_ok) {
      text.replace(at, what.size(), with);
      at += with.size();
    } else {
      ++at;
    }
  }
}

std::string RedactUserData(std::string text) {
#ifdef _WIN32
  static const std::string profile = EnvVarUtf8("USERPROFILE");
#else
  static const std::string profile = EnvVarUtf8("HOME");
#endif
  static const std::string user = CurrentUserName();
  static const std::string computer = CurrentComputerName();
  if (profile.size() >= 4) ReplaceAll(text, profile, "%USERPROFILE%", false);
  constexpr std::string_view marker = "\\users\\";
  for (size_t at = 0; at + marker.size() <= text.size(); ++at) {
    if (!MatchesAt(text, at, marker)) continue;
    const size_t begin = at + marker.size();
    size_t end = begin;
    while (end < text.size() && text[end] != '\\' && text[end] != '/' && text[end] != '\'' && text[end] != '"') ++end;
    const std::string_view name(text.data() + begin, end - begin);
    if (name.empty() || name == "<user>" || name == "Public" || name == "Default" || name == "All Users") continue;
    text.replace(begin, end - begin, "<user>");
  }
  if (user.size() >= 3) ReplaceAll(text, user, "<user>", true);
  if (computer.size() >= 3 && computer != user) ReplaceAll(text, computer, "<computer>", true);
  return text;
}

}  // namespace

namespace {

void Write(const char* level, const std::string& line) {
  static std::mutex mutex;  // the disc import and the update check write from their own threads
  std::lock_guard lock(mutex);
  const fs::path file = HubLogFile();
  if (file.empty()) return;
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  // Full: kept as coreoflight.old.log (it was emptied before, and with it what led up to a problem).
  if (fs::file_size(file, ec) > 256 * 1024 && !ec) {
    fs::path old = file;
    old.replace_filename("coreoflight.old.log");
    fs::rename(file, old, ec);
    if (ec) fs::remove(file, ec);
  }
  std::ofstream out(file, std::ios::app);
  if (!out) return;
  const LocalDateTime t = NowLocalDateTime();
  char stamp[48];
  std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d.%03d [%s] ", t.year, t.month, t.day, t.hour,
                t.minute, t.second, t.millisecond, level);
  out << stamp << RedactUserData(line) << "\n";
}

}  // namespace

void HubLog(const std::string& line) { Write("info", line); }
void HubWarn(const std::string& line) { Write("warn", line); }
void HubError(const std::string& line) { Write("error", line); }

std::string WindowsVersion() {
#ifdef _WIN32
  using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
  OSVERSIONINFOW v{};
  v.dwOSVersionInfoSize = sizeof(v);
  if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
    if (auto get = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) get(&v);
  }
  char text[64];
  std::snprintf(text, sizeof(text), "%lu.%lu.%lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
  return text;
#else
  return OsVersionSummary();
#endif
}

std::string WineVersion() {
#ifdef _WIN32
  using GetVersion = const char*(__cdecl*)();
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto get = ntdll ? reinterpret_cast<GetVersion>(GetProcAddress(ntdll, "wine_get_version")) : nullptr;
  return get ? std::string(get()) : std::string();
#else
  return {};
#endif
}

}  // namespace coreoflight
