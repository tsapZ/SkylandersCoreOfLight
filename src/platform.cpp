#include "platform.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>

#include <array>
#include <fstream>
#include <vector>

namespace coreoflight {
namespace fs = std::filesystem;

namespace {

std::string WideToUtf8(std::wstring_view w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string out(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring Utf8ToWide(std::string_view s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
  return out;
}

fs::path KnownWinFolder(REFKNOWNFOLDERID id) {
  PWSTR raw = nullptr;
  fs::path out;
  if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw) out = raw;
  CoTaskMemFree(raw);
  return out;
}

}  // namespace

std::string PathUtf8(const fs::path& path) {
  const std::u8string u8 = path.u8string();
  return std::string(u8.begin(), u8.end());
}

fs::path PathFromUtf8(std::string_view utf8) {
  return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string GenericPathUtf8(const fs::path& path) {
  const std::u8string u8 = path.generic_u8string();
  return std::string(u8.begin(), u8.end());
}

fs::path WithPathSuffix(const fs::path& path, std::string_view suffix) {
  return fs::path(path.native() + Utf8ToWide(suffix));
}

bool IsRealToySubpath(const fs::path& path) {
  return GenericPathUtf8(path).find("/real/") != std::string::npos;
}

fs::path SelfExePath() {
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
    if (n == 0) return {};
    if (n < buf.size()) {
      buf.resize(n);
      return fs::path(buf);
    }
    buf.resize(buf.size() * 2);
  }
}

fs::path LocalAppDataDir() { return KnownWinFolder(FOLDERID_LocalAppData); }

fs::path DesktopDir() { return KnownWinFolder(FOLDERID_Desktop); }

fs::path UserDownloadsDir() { return KnownWinFolder(FOLDERID_Downloads); }

fs::path TempDir() {
  wchar_t temp[MAX_PATH] = L"";
  GetTempPathW(MAX_PATH, temp);
  return fs::path(temp);
}

std::string EnvVarUtf8(const char* name) {
  if (!name) return {};
  const std::wstring wname = Utf8ToWide(name);
  const DWORD needed = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
  if (needed == 0) return {};
  std::wstring buf(needed, L'\0');
  const DWORD written = GetEnvironmentVariableW(wname.c_str(), buf.data(), needed);
  if (written == 0 || written >= needed) return {};
  return WideToUtf8(std::wstring_view(buf.data(), written));
}

std::string CurrentUserName() {
  wchar_t buf[256] = L"";
  DWORD n = DWORD(std::size(buf));
  if (GetUserNameW(buf, &n) && n > 1) return WideToUtf8(std::wstring_view(buf, n - 1));
  return EnvVarUtf8("USERNAME");
}

std::string CurrentComputerName() {
  wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1] = L"";
  DWORD n = DWORD(std::size(buf));
  if (GetComputerNameW(buf, &n) && n > 0) return WideToUtf8(std::wstring_view(buf, n));
  return EnvVarUtf8("COMPUTERNAME");
}

std::string CpuModelName() {
  wchar_t name[256] = L"";
  DWORD size = sizeof(name);
  if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                   L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, name, &size) != ERROR_SUCCESS) {
    return {};
  }
  std::string out = WideToUtf8(name);
  while (!out.empty() && out.front() == ' ') out.erase(out.begin());
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

bool IsGermanOsLocale() { return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN; }

uint64_t MonotonicMs() { return GetTickCount64(); }

uint64_t NowFileTimeTicks() {
  FILETIME ft = {};
  GetSystemTimeAsFileTime(&ft);
  return (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

uint64_t PathFileTimeTicks(const fs::path& path) {
  WIN32_FILE_ATTRIBUTE_DATA data = {};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
  return (uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
}

LocalDateTime NowLocalDateTime() {
  SYSTEMTIME t = {};
  GetLocalTime(&t);
  return {t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds};
}

std::tm ToLocalTm(std::time_t t) {
  std::tm out{};
  localtime_s(&out, &t);
  return out;
}

bool FileTimeToLocalTm(uint64_t filetime_ticks, std::tm& out) {
  out = {};
  if (filetime_ticks == 0) return false;
  FILETIME utc{DWORD(filetime_ticks & 0xFFFFFFFFULL), DWORD(filetime_ticks >> 32)};
  FILETIME local{};
  SYSTEMTIME st{};
  if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &st)) return false;
  out.tm_year = int(st.wYear) - 1900;
  out.tm_mon = int(st.wMonth) - 1;
  out.tm_mday = int(st.wDay);
  out.tm_hour = int(st.wHour);
  out.tm_min = int(st.wMinute);
  out.tm_sec = int(st.wSecond);
  out.tm_wday = int(st.wDayOfWeek);
  return true;
}

std::optional<uint64_t> FreeDiskBytes(fs::path path) {
  std::error_code ec;
  while (!path.empty() && !fs::exists(path, ec)) {
    const fs::path parent = path.parent_path();
    if (parent == path) break;
    path = parent;
  }
  ULARGE_INTEGER free_bytes{};
  if (!GetDiskFreeSpaceExW(path.c_str(), &free_bytes, nullptr, nullptr)) return std::nullopt;
  return free_bytes.QuadPart;
}

bool MoveReplaceFile(const fs::path& from, const fs::path& to) {
  return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

bool MoveFileNewAside(const fs::path& from, const fs::path& to) {
  return MoveFileExW(from.c_str(), to.c_str(), 0) != 0;
}

bool MovePathToTrash(const fs::path& path) {
  // SHFileOperation wants the paths ended by two null characters.
  std::wstring from = path.wstring();
  from.push_back(L'\0');
  from.push_back(L'\0');
  SHFILEOPSTRUCTW op = {};
  op.wFunc = FO_DELETE;
  op.pFrom = from.c_str();
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
  return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

bool OpenInFileManager(const fs::path& path, bool select_file) {
  if (select_file) {
    const std::wstring arg = L"/select,\"" + path.wstring() + L"\"";
    return INT_PTR(ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL)) > 32;
  }
  return INT_PTR(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

std::string Sha256FileHex(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  static constexpr char kHex[] = "0123456789abcdef";
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
  std::array<uint8_t, 32> digest{};
  bool ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0;
  std::vector<char> buffer(size_t(1) << 20);
  while (ok && in) {
    in.read(buffer.data(), std::streamsize(buffer.size()));
    const auto got = size_t(in.gcount());
    if (got > 0) ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), ULONG(got), 0) == 0;
  }
  if (ok) ok = BCryptFinishHash(hash, digest.data(), ULONG(digest.size()), 0) == 0;
  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  if (!ok) return {};
  std::string hex;
  hex.reserve(64);
  for (uint8_t b : digest) {
    hex.push_back(kHex[b >> 4]);
    hex.push_back(kHex[b & 0xF]);
  }
  return hex;
}

bool BinaryContainsString(const fs::path& file, std::string_view needle) {
  if (needle.empty()) return false;
  std::ifstream in(file, std::ios::binary);
  if (!in) return false;
  std::string chunk(size_t(4) << 20, '\0');
  const size_t overlap = needle.size() - 1;
  std::string tail;
  while (in) {
    in.read(chunk.data(), std::streamsize(chunk.size()));
    const size_t got = size_t(in.gcount());
    if (!got) break;
    std::string combined;
    combined.reserve(tail.size() + got);
    combined.append(tail);
    combined.append(chunk.data(), got);
    if (combined.find(needle) != std::string::npos) return true;
    tail = combined.size() > overlap ? combined.substr(combined.size() - overlap) : combined;
  }
  return false;
}

void* SpawnProcess(const fs::path& exe, const std::vector<std::string>& args, const fs::path& cwd, bool no_window) {
  auto quote = [](std::wstring_view a) -> std::wstring {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring_view::npos) return std::wstring(a);
    std::wstring out = L"\"";
    size_t bs = 0;
    for (wchar_t c : a) {
      if (c == L'\\') {
        ++bs;
      } else if (c == L'"') {
        out.append(bs * 2 + 1, L'\\');
        out.push_back(L'"');
        bs = 0;
      } else {
        out.append(bs, L'\\');
        bs = 0;
        out.push_back(c);
      }
    }
    out.append(bs * 2, L'\\');
    out.push_back(L'"');
    return out;
  };
  std::wstring command = quote(exe.wstring());
  for (const std::string& a : args) {
    command.push_back(L' ');
    command += quote(Utf8ToWide(a));
  }
  STARTUPINFOW startup = {sizeof(startup)};
  if (no_window) {
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
  }
  PROCESS_INFORMATION info = {};
  const DWORD flags = no_window ? CREATE_NO_WINDOW : 0;
  const std::wstring cwd_w = cwd.empty() ? std::wstring() : cwd.wstring();
  if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, flags, nullptr,
                      cwd_w.empty() ? nullptr : cwd_w.c_str(), &startup, &info)) {
    return nullptr;
  }
  CloseHandle(info.hThread);
  return info.hProcess;
}

bool IsProcessRunning(void* handle, uint32_t wait_ms, int* exit_code) {
  if (!handle) return false;
  const HANDLE h = static_cast<HANDLE>(handle);
  if (WaitForSingleObject(h, wait_ms) == WAIT_TIMEOUT) return true;
  if (exit_code) {
    DWORD code = 0;
    *exit_code = GetExitCodeProcess(h, &code) ? int(code) : -1;
  }
  return false;
}

void TerminateAndCloseProcess(void*& handle) {
  if (!handle) return;
  const HANDLE h = static_cast<HANDLE>(handle);
  TerminateProcess(h, 1);
  WaitForSingleObject(h, 5000);
  CloseHandle(h);
  handle = nullptr;
}

void CloseProcessHandle(void*& handle) {
  if (!handle) return;
  CloseHandle(static_cast<HANDLE>(handle));
  handle = nullptr;
}

std::vector<std::string> LaunchArgs() {
  std::vector<std::string> out;
  int argc = 0;
  if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
    for (int i = 1; i < argc; ++i) out.push_back(WideToUtf8(argv[i]));
    LocalFree(argv);
  }
  return out;
}

std::string LastSystemError() {
  wchar_t text[256] = L"";
  FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), 0, text, 255,
                 nullptr);
  std::string out = WideToUtf8(text);
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return out;
}

}  // namespace coreoflight
