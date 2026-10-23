#include "hub_log.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace coreoflight {

namespace fs = std::filesystem;

fs::path HubLogFile() {
  const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
  return local ? fs::path(local) / "CoreOfLight" / "coreoflight.log" : fs::path();
}

void HubLog(const std::string& line) {
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
  out << stamp << line << "\n";
}

std::string WineVersion() {
  using GetVersion = const char*(__cdecl*)();
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto get = ntdll ? reinterpret_cast<GetVersion>(GetProcAddress(ntdll, "wine_get_version")) : nullptr;
  return get ? std::string(get()) : std::string();
}

}  // namespace coreoflight
