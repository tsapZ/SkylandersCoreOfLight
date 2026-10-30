#include "hub_log.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace coreoflight {

namespace fs = std::filesystem;

fs::path HubLogFile() {
  const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
  return local ? fs::path(local) / "CoreOfLight" / "coreoflight.log" : fs::path();
}

namespace {

// The player's data out of every line (2026-10-30: logs end up in public reports), as the games'
// SDK does it (rexglue-sdk core/logging.cpp RedactUserData): the user folder becomes %USERPROFILE%,
// any other user's folder after \Users\ <user>, the user's and the machine's names <user> and
// <computer>.
std::string EnvUtf8(const wchar_t* name) {
  const wchar_t* value = _wgetenv(name);
  if (!value || !*value) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  std::string out(n > 0 ? n - 1 : 0, '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), n, nullptr, nullptr);
  return out;
}

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
  static const std::string profile = EnvUtf8(L"USERPROFILE");
  static const std::string user = EnvUtf8(L"USERNAME");
  static const std::string computer = EnvUtf8(L"COMPUTERNAME");
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

void HubLog(const std::string& line) {
  static std::mutex mutex;  // the disc import writes from its own thread
  std::lock_guard lock(mutex);
  const fs::path file = HubLogFile();
  if (file.empty()) return;
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  const bool restart = fs::file_size(file, ec) > 256 * 1024 && !ec;
  std::ofstream out(file, restart ? std::ios::trunc : std::ios::app);
  if (!out) return;
  SYSTEMTIME t;
  GetLocalTime(&t);
  char stamp[32];
  std::snprintf(stamp, sizeof(stamp), "%04u-%02u-%02u %02u:%02u:%02u.%03u ", t.wYear, t.wMonth, t.wDay, t.wHour,
                t.wMinute, t.wSecond, t.wMilliseconds);
  out << stamp << RedactUserData(line) << "\n";
}

std::string WineVersion() {
  using GetVersion = const char*(__cdecl*)();
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto get = ntdll ? reinterpret_cast<GetVersion>(GetProcAddress(ntdll, "wine_get_version")) : nullptr;
  return get ? std::string(get()) : std::string();
}

}  // namespace coreoflight
