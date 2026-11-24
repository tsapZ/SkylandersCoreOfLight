#include "platform.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#else
#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

namespace coreoflight {
namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
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
#else
struct PosixProcess {
  pid_t pid = -1;
  bool exited = false;
  int exit_code = -1;
};

std::mutex g_orphan_mutex;
std::vector<pid_t> g_orphan_pids;

void SweepOrphanPids() {
  std::lock_guard lock(g_orphan_mutex);
  for (auto it = g_orphan_pids.begin(); it != g_orphan_pids.end();) {
    int status = 0;
    const pid_t r = waitpid(*it, &status, WNOHANG);
    if (r == *it || (r < 0 && errno == ECHILD)) {
      it = g_orphan_pids.erase(it);
    } else {
      ++it;
    }
  }
}

std::string XdgConfigDir() {
  if (const char* p = std::getenv("XDG_CONFIG_HOME"); p && *p) return p;
  if (const char* h = std::getenv("HOME"); h && *h) return std::string(h) + "/.config";
  return ".";
}

std::string XdgDataHome() {
  if (const char* p = std::getenv("XDG_DATA_HOME"); p && *p) return p;
  if (const char* h = std::getenv("HOME"); h && *h) return std::string(h) + "/.local/share";
  return ".";
}

fs::path ReadXdgUserDir(const char* key, const char* fallback_subdir) {
  if (const char* env = std::getenv(key); env && *env) return PathFromUtf8(env);
  const std::string home = [] {
    const char* h = std::getenv("HOME");
    return h && *h ? std::string(h) : std::string(".");
  }();
  std::ifstream in(XdgConfigDir() + "/user-dirs.dirs");
  const std::string prefix = std::string(key) + "=";
  for (std::string line; std::getline(in, line);) {
    while (!line.empty() && (line[0] == ' ' || line[0] == '\t')) line.erase(line.begin());
    if (!line.starts_with(prefix)) continue;
    std::string val = line.substr(prefix.size());
    while (!val.empty() && (val.back() == '\r' || val.back() == ' ' || val.back() == '\t')) val.pop_back();
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
      val = val.substr(1, val.size() - 2);
    }
    if (val == "$HOME") return PathFromUtf8(home);
    if (val.starts_with("$HOME/")) return PathFromUtf8(home + val.substr(5));
    if (!val.empty() && val[0] == '/') return PathFromUtf8(val);
  }
  return PathFromUtf8(home) / fallback_subdir;
}

std::string UrlEncodePath(const std::string& path) {
  std::string out;
  out.reserve(path.size() + 16);
  for (unsigned char c : path) {
    const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                      c == '/' || c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      out.push_back(char(c));
    } else {
      char hex[4];
      std::snprintf(hex, sizeof(hex), "%%%02X", unsigned(c));
      out += hex;
    }
  }
  return out;
}

std::string NormalizeArchiveRel(const char* raw) {
  if (!raw) return {};
  std::string rel = raw;
  std::replace(rel.begin(), rel.end(), '\\', '/');
  while (rel.starts_with("./") || rel.starts_with("/")) {
    rel.erase(0, rel[0] == '.' ? 2 : 1);
  }
  while (!rel.empty() && rel.back() == '/') rel.pop_back();
  if (rel.empty() || rel.find('\0') != std::string::npos) return {};
  // Reject path traversal segments.
  for (size_t i = 0; i <= rel.size();) {
    const size_t slash = std::min(rel.find('/', i), rel.size());
    const std::string_view part(rel.data() + i, slash - i);
    if (part == "..") return {};
    i = slash + 1;
  }
  return rel;
}
#endif

constexpr uint64_t kWindowsEpochDiffSeconds = 11644473600ULL;

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
#ifdef _WIN32
  return fs::path(path.native() + Utf8ToWide(suffix));
#else
  return PathFromUtf8(PathUtf8(path) + std::string(suffix));
#endif
}

bool IsRealToySubpath(const fs::path& path) {
  return GenericPathUtf8(path).find("/real/") != std::string::npos;
}

fs::path SelfExePath() {
#ifdef _WIN32
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
#else
  std::error_code ec;
  return fs::read_symlink("/proc/self/exe", ec);
#endif
}

fs::path LocalAppDataDir() {
#ifdef _WIN32
  return KnownWinFolder(FOLDERID_LocalAppData);
#else
  return PathFromUtf8(XdgDataHome());
#endif
}

fs::path DesktopDir() {
#ifdef _WIN32
  return KnownWinFolder(FOLDERID_Desktop);
#else
  return ReadXdgUserDir("XDG_DESKTOP_DIR", "Desktop");
#endif
}

fs::path UserDownloadsDir() {
#ifdef _WIN32
  return KnownWinFolder(FOLDERID_Downloads);
#else
  fs::path d = ReadXdgUserDir("XDG_DOWNLOAD_DIR", "Downloads");
  std::error_code ec;
  return (!d.empty() && fs::is_directory(d, ec)) ? d : fs::path();
#endif
}

fs::path TempDir() {
#ifdef _WIN32
  wchar_t temp[MAX_PATH] = L"";
  GetTempPathW(MAX_PATH, temp);
  return fs::path(temp);
#else
  std::error_code ec;
  fs::path p = fs::temp_directory_path(ec);
  return p.empty() ? fs::path("/tmp") : p;
#endif
}

std::string EnvVarUtf8(const char* name) {
  if (!name) return {};
#ifdef _WIN32
  const std::wstring wname = Utf8ToWide(name);
  const DWORD needed = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
  if (needed == 0) return {};
  std::wstring buf(needed, L'\0');
  const DWORD written = GetEnvironmentVariableW(wname.c_str(), buf.data(), needed);
  if (written == 0 || written >= needed) return {};
  return WideToUtf8(std::wstring_view(buf.data(), written));
#else
  const char* v = std::getenv(name);
  return v ? std::string(v) : std::string();
#endif
}

std::string CurrentUserName() {
#ifdef _WIN32
  wchar_t buf[256] = L"";
  DWORD n = DWORD(std::size(buf));
  if (GetUserNameW(buf, &n) && n > 1) return WideToUtf8(std::wstring_view(buf, n - 1));
  return EnvVarUtf8("USERNAME");
#else
  if (std::string u = EnvVarUtf8("USER"); !u.empty()) return u;
  return EnvVarUtf8("LOGNAME");
#endif
}

std::string CurrentComputerName() {
#ifdef _WIN32
  wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1] = L"";
  DWORD n = DWORD(std::size(buf));
  if (GetComputerNameW(buf, &n) && n > 0) return WideToUtf8(std::wstring_view(buf, n));
  return EnvVarUtf8("COMPUTERNAME");
#else
  char buf[256] = {};
  if (gethostname(buf, sizeof(buf) - 1) == 0) return buf;
  return {};
#endif
}

std::string CpuModelName() {
#ifdef _WIN32
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
#else
  std::ifstream in("/proc/cpuinfo");
  for (std::string line; std::getline(in, line);) {
    if (!line.starts_with("model name")) continue;
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string out = line.substr(colon + 1);
    while (!out.empty() && (out.front() == ' ' || out.front() == '\t')) out.erase(out.begin());
    while (!out.empty() && (out.back() == ' ' || out.back() == '\r')) out.pop_back();
    return out;
  }
  return {};
#endif
}

std::string OsVersionSummary() {
#ifdef _WIN32
  return {};
#else
  std::string distro;
  {
    std::ifstream in("/etc/os-release");
    for (std::string line; std::getline(in, line);) {
      if (!line.starts_with("PRETTY_NAME=")) continue;
      distro = line.substr(12);
      if (distro.size() >= 2 && distro.front() == '"' && distro.back() == '"') {
        distro = distro.substr(1, distro.size() - 2);
      }
      break;
    }
  }
  struct utsname u {};
  if (uname(&u) == 0) {
    const std::string kernel = std::string(u.sysname) + " " + u.release + " (" + u.machine + ")";
    return distro.empty() ? kernel : distro + " — " + kernel;
  }
  return distro;
#endif
}

bool IsGermanOsLocale() {
#ifdef _WIN32
  return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN;
#else
  for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE"}) {
    const char* raw = std::getenv(var);
    if (!raw || !*raw) continue;
    std::string v(raw);
    for (char& c : v) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (v == "c" || v == "c.utf-8" || v == "posix") continue;
    return v == "de" || v.starts_with("de_") || v.starts_with("de-") || v.starts_with("de.");
  }
  return false;
#endif
}

uint64_t MonotonicMs() {
#ifdef _WIN32
  return GetTickCount64();
#else
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
#endif
}

uint64_t NowFileTimeTicks() {
#ifdef _WIN32
  FILETIME ft = {};
  GetSystemTimeAsFileTime(&ft);
  return (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
#else
  const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
  return uint64_t(ns / 100) + kWindowsEpochDiffSeconds * 10000000ULL;
#endif
}

uint64_t PathFileTimeTicks(const fs::path& path) {
#ifdef _WIN32
  WIN32_FILE_ATTRIBUTE_DATA data = {};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
  return (uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
#else
  struct stat st {};
  if (::stat(PathUtf8(path).c_str(), &st) != 0) return 0;
  const uint64_t secs = uint64_t(st.st_mtim.tv_sec) + kWindowsEpochDiffSeconds;
  const uint64_t nsec_100 = uint64_t(st.st_mtim.tv_nsec) / 100;
  return secs * 10000000ULL + nsec_100;
#endif
}

LocalDateTime NowLocalDateTime() {
  LocalDateTime out;
#ifdef _WIN32
  SYSTEMTIME t = {};
  GetLocalTime(&t);
  out = {t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds};
#else
  const auto now = std::chrono::system_clock::now();
  const std::time_t secs = std::chrono::system_clock::to_time_t(now);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
  const std::tm t = ToLocalTm(secs);
  out = {uint16_t(t.tm_year + 1900), uint16_t(t.tm_mon + 1), uint16_t(t.tm_mday),
         uint16_t(t.tm_hour),        uint16_t(t.tm_min),      uint16_t(t.tm_sec),
         uint16_t(ms >= 0 ? ms : 0)};
#endif
  return out;
}

std::tm ToLocalTm(std::time_t t) {
  std::tm out{};
#ifdef _WIN32
  localtime_s(&out, &t);
#else
  localtime_r(&t, &out);
#endif
  return out;
}

bool FileTimeToLocalTm(uint64_t filetime_ticks, std::tm& out) {
  out = {};
  if (filetime_ticks == 0) return false;
#ifdef _WIN32
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
#else
  constexpr uint64_t kEpochTicks = kWindowsEpochDiffSeconds * 10000000ULL;
  if (filetime_ticks < kEpochTicks) return false;
  const std::time_t t = static_cast<std::time_t>((filetime_ticks - kEpochTicks) / 10000000ULL);
  return localtime_r(&t, &out) != nullptr;
#endif
}

std::optional<uint64_t> FreeDiskBytes(fs::path path) {
  std::error_code ec;
  while (!path.empty() && !fs::exists(path, ec)) {
    const fs::path parent = path.parent_path();
    if (parent == path) break;
    path = parent;
  }
#ifdef _WIN32
  ULARGE_INTEGER free_bytes{};
  if (!GetDiskFreeSpaceExW(path.c_str(), &free_bytes, nullptr, nullptr)) return std::nullopt;
  return free_bytes.QuadPart;
#else
  if (path.empty()) path = ".";
  const fs::space_info sp = fs::space(path, ec);
  if (ec || sp.available == static_cast<uintmax_t>(-1)) return std::nullopt;
  return uint64_t(sp.available);
#endif
}

bool MoveReplaceFile(const fs::path& from, const fs::path& to) {
#ifdef _WIN32
  return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
  std::error_code ec;
  fs::rename(from, to, ec);
  if (!ec) return true;
  if (ec == std::errc::cross_device_link) {
    ec.clear();
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (!ec) {
      fs::remove(from, ec);
      return !ec;
    }
  }
  return false;
#endif
}

bool MoveFileNewAside(const fs::path& from, const fs::path& to) {
#ifdef _WIN32
  return MoveFileExW(from.c_str(), to.c_str(), 0) != 0;
#else
  std::error_code ec;
  if (fs::exists(to, ec) || ec) return false;
  fs::rename(from, to, ec);
  return !ec;
#endif
}

bool MovePathToTrash(const fs::path& path) {
#ifdef _WIN32
  // SHFileOperation wants the paths ended by two null characters.
  std::wstring from = path.wstring();
  from.push_back(L'\0');
  from.push_back(L'\0');
  SHFILEOPSTRUCTW op = {};
  op.wFunc = FO_DELETE;
  op.pFrom = from.c_str();
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
  return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
#else
  std::error_code ec;
  const fs::path abs_path = fs::weakly_canonical(path, ec);
  const fs::path src = abs_path.empty() ? fs::absolute(path, ec) : abs_path;
  if (!fs::exists(src, ec)) return false;

  const fs::path trash = PathFromUtf8(XdgDataHome()) / "Trash";
  const fs::path files_dir = trash / "files";
  const fs::path info_dir = trash / "info";
  fs::create_directories(files_dir, ec);
  fs::create_directories(info_dir, ec);
  if (ec) return false;

  std::string base = PathUtf8(path.filename());
  if (base.empty()) base = "item";
  fs::path dest_file;
  fs::path dest_info;
  for (int i = 0; i < 10000; ++i) {
    const std::string name = i == 0 ? base : base + "." + std::to_string(i + 1);
    const fs::path f = files_dir / PathFromUtf8(name);
    const fs::path info = info_dir / PathFromUtf8(name + ".trashinfo");
    if (!fs::exists(f, ec) && !fs::exists(info, ec)) {
      dest_file = f;
      dest_info = info;
      break;
    }
  }
  if (dest_file.empty()) return false;

  const LocalDateTime t = NowLocalDateTime();
  char stamp[32];
  std::snprintf(stamp, sizeof(stamp), "%04u-%02u-%02uT%02u:%02u:%02u", t.year, t.month, t.day, t.hour, t.minute,
                t.second);
  {
    std::ofstream info(dest_info, std::ios::binary | std::ios::trunc);
    info << "[Trash Info]\nPath=" << UrlEncodePath(PathUtf8(src)) << "\nDeletionDate=" << stamp << "\n";
    if (!info) {
      fs::remove(dest_info, ec);
      return false;
    }
  }

  fs::rename(path, dest_file, ec);
  if (!ec) return true;
  if (ec == std::errc::cross_device_link) {
    ec.clear();
    fs::copy(path, dest_file, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    if (!ec) {
      fs::remove_all(path, ec);
      return !ec;
    }
    fs::remove_all(dest_file, ec);
  }
  fs::remove(dest_info, ec);
  return false;
#endif
}

bool OpenInFileManager(const fs::path& path, bool select_file) {
#ifdef _WIN32
  if (select_file) {
    const std::wstring arg = L"/select,\"" + path.wstring() + L"\"";
    return INT_PTR(ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL)) > 32;
  }
  return INT_PTR(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
  std::error_code ec;
  const fs::path target = select_file && !fs::is_directory(path, ec) ? path.parent_path() : path;
  const std::string target_utf8 = PathUtf8(target);
  const pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    setsid();
    const int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      if (devnull > STDERR_FILENO) close(devnull);
    }
    const pid_t grand = fork();
    if (grand == 0) {
      char cmd[] = "xdg-open";
      std::string arg = target_utf8;
      char* argv[] = {cmd, arg.data(), nullptr};
      execvp(cmd, argv);
      _exit(127);
    }
    _exit(0);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
  return true;
#endif
}

bool MakeExecutable(const fs::path& path) {
#ifdef _WIN32
  (void)path;
  return true;
#else
  return ::chmod(PathUtf8(path).c_str(), 0755) == 0;
#endif
}

std::string Sha256FileHex(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  static constexpr char kHex[] = "0123456789abcdef";
#ifdef _WIN32
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
#else
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) return {};
  std::array<uint8_t, 32> digest{};
  unsigned int digest_len = 0;
  bool ok = EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1;
  std::vector<char> buffer(size_t(1) << 20);
  while (ok && in) {
    in.read(buffer.data(), std::streamsize(buffer.size()));
    const auto got = size_t(in.gcount());
    if (got > 0) ok = EVP_DigestUpdate(ctx, buffer.data(), got) == 1;
  }
  if (ok) ok = EVP_DigestFinal_ex(ctx, digest.data(), &digest_len) == 1 && digest_len == digest.size();
  EVP_MD_CTX_free(ctx);
#endif
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

bool ExtractArchive(const fs::path& archive_path,
                    const std::function<fs::path(const std::string& rel_path)>& map_dest,
                    std::string& error) {
#ifdef _WIN32
  (void)archive_path;
  (void)map_dest;
  error = "ExtractArchive helper is only implemented on POSIX";
  return false;
#else
  struct archive* a = archive_read_new();
  if (!a) {
    error = "Could not allocate archive reader";
    return false;
  }
  archive_read_support_format_all(a);
  archive_read_support_filter_all(a);
  if (archive_read_open_filename(a, PathUtf8(archive_path).c_str(), 16384) != ARCHIVE_OK) {
    error = archive_error_string(a) ? archive_error_string(a) : "Could not open archive";
    archive_read_free(a);
    return false;
  }

  bool ok = true;
  struct archive_entry* entry = nullptr;
  while (true) {
    const int r = archive_read_next_header(a, &entry);
    if (r == ARCHIVE_EOF) break;
    if (r < ARCHIVE_OK) {
      error = archive_error_string(a) ? archive_error_string(a) : "Corrupt archive header";
      ok = false;
      break;
    }
    const char* raw_name = archive_entry_pathname_utf8(entry);
    if (!raw_name) raw_name = archive_entry_pathname(entry);
    const std::string rel = NormalizeArchiveRel(raw_name);
    if (rel.empty()) {
      archive_read_data_skip(a);
      continue;
    }
    const fs::path dest = map_dest(rel);
    if (dest.empty()) {
      archive_read_data_skip(a);
      continue;
    }
    std::error_code ec;
    const mode_t type = archive_entry_filetype(entry);
    if (type == AE_IFDIR) {
      fs::create_directories(dest, ec);
      archive_read_data_skip(a);
      continue;
    }
    fs::create_directories(dest.parent_path(), ec);
    if (type == AE_IFLNK) {
      const char* target = archive_entry_symlink_utf8(entry);
      if (!target) target = archive_entry_symlink(entry);
      if (target && *target) {
        fs::remove(dest, ec);
        fs::create_symlink(PathFromUtf8(target), dest, ec);
      }
      archive_read_data_skip(a);
      continue;
    }
    if (type != AE_IFREG) {
      archive_read_data_skip(a);
      continue;
    }
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out) {
      error = "Could not create " + PathUtf8(dest);
      ok = false;
      break;
    }
    const void* buf = nullptr;
    size_t len = 0;
    la_int64_t offset = 0;
    while (true) {
      const int dr = archive_read_data_block(a, &buf, &len, &offset);
      if (dr == ARCHIVE_EOF) break;
      if (dr < ARCHIVE_OK) {
        error = archive_error_string(a) ? archive_error_string(a) : "Corrupt archive data";
        ok = false;
        break;
      }
      if (len > 0 && !out.write(static_cast<const char*>(buf), std::streamsize(len))) {
        error = "Could not write " + PathUtf8(dest);
        ok = false;
        break;
      }
    }
    out.close();
    if (!ok) break;
    if ((archive_entry_mode(entry) & 0111) != 0) MakeExecutable(dest);
  }
  archive_read_close(a);
  archive_read_free(a);
  return ok;
#endif
}

void* SpawnProcess(const fs::path& exe, const std::vector<std::string>& args, const fs::path& cwd,
                   bool no_window) {
#ifdef _WIN32
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
  // The child starts without Windows' own "... was not found" box (SEM_FAILCRITICALERRORS, inherited at
  // creation): a game whose DLL is gone (Core of Light #146: Windows Security took every game's
  // rexruntime.dll) then ends at once with 0xC0000135 and the launcher's box names the DLL and where to get
  // it back. With Windows' box the game hung on it until OK was pressed ("is starting ..." meanwhile), a
  // background run (the settings schema at the launcher's start) put it up unasked, and whether it came at
  // all depended on how the launcher was started (from its setup: never).
  const UINT error_mode = SetErrorMode(SEM_FAILCRITICALERRORS);
  SetErrorMode(error_mode | SEM_FAILCRITICALERRORS);
  const BOOL created = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, flags, nullptr,
                                      cwd_w.empty() ? nullptr : cwd_w.c_str(), &startup, &info);
  SetErrorMode(error_mode);
  if (!created) {
    return nullptr;
  }
  CloseHandle(info.hThread);
  return info.hProcess;
#else
  (void)no_window;
  SweepOrphanPids();
  std::error_code ec;
  if (!fs::is_regular_file(exe, ec)) return nullptr;

  std::string exe_utf8 = PathUtf8(exe);
  std::vector<std::string> owned;
  owned.reserve(1 + args.size());
  owned.push_back(exe_utf8);
  for (const std::string& a : args) owned.push_back(a);
  std::vector<char*> argv;
  argv.reserve(owned.size() + 1);
  for (std::string& s : owned) argv.push_back(s.data());
  argv.push_back(nullptr);

  const std::string cwd_utf8 = cwd.empty() ? std::string() : PathUtf8(cwd);
  const pid_t pid = fork();
  if (pid < 0) return nullptr;
  if (pid == 0) {
    if (!cwd_utf8.empty()) {
      if (chdir(cwd_utf8.c_str()) != 0) _exit(127);
    }
    const int devnull = open("/dev/null", O_RDONLY);
    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      if (devnull > STDERR_FILENO) close(devnull);
    }
    execv(exe_utf8.c_str(), argv.data());
    _exit(127);
  }
  auto* proc = new PosixProcess();
  proc->pid = pid;
  return proc;
#endif
}

bool IsProcessRunning(void* handle, uint32_t wait_ms, int* exit_code) {
  if (!handle) return false;
#ifdef _WIN32
  const HANDLE h = static_cast<HANDLE>(handle);
  if (WaitForSingleObject(h, wait_ms) == WAIT_TIMEOUT) return true;
  if (exit_code) {
    DWORD code = 0;
    *exit_code = GetExitCodeProcess(h, &code) ? int(code) : -1;
  }
  return false;
#else
  SweepOrphanPids();
  auto* proc = static_cast<PosixProcess*>(handle);
  if (proc->exited) {
    if (exit_code) *exit_code = proc->exit_code;
    return false;
  }
  const auto deadline = wait_ms == 0xFFFFFFFFu
                            ? std::chrono::steady_clock::time_point::max()
                            : std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
  for (;;) {
    int status = 0;
    const int flags = wait_ms == 0xFFFFFFFFu ? 0 : WNOHANG;
    const pid_t r = waitpid(proc->pid, &status, flags);
    if (r == proc->pid) {
      proc->exited = true;
      proc->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1);
      if (exit_code) *exit_code = proc->exit_code;
      return false;
    }
    if (r < 0) {
      if (errno == EINTR) continue;
      proc->exited = true;
      proc->exit_code = -1;
      if (exit_code) *exit_code = -1;
      return false;
    }
    if (wait_ms == 0 || std::chrono::steady_clock::now() >= deadline) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
#endif
}

void TerminateAndCloseProcess(void*& handle) {
  if (!handle) return;
#ifdef _WIN32
  const HANDLE h = static_cast<HANDLE>(handle);
  TerminateProcess(h, 1);
  WaitForSingleObject(h, 5000);
  CloseHandle(h);
  handle = nullptr;
#else
  auto* proc = static_cast<PosixProcess*>(handle);
  if (!proc->exited && proc->pid > 0) {
    kill(proc->pid, SIGTERM);
    if (IsProcessRunning(handle, 200)) {
      kill(proc->pid, SIGKILL);
      IsProcessRunning(handle, 0xFFFFFFFFu);
    }
  }
  delete proc;
  handle = nullptr;
#endif
}

void CloseProcessHandle(void*& handle) {
  if (!handle) return;
#ifdef _WIN32
  CloseHandle(static_cast<HANDLE>(handle));
  handle = nullptr;
#else
  auto* proc = static_cast<PosixProcess*>(handle);
  if (!proc->exited && proc->pid > 0) {
    int status = 0;
    const pid_t r = waitpid(proc->pid, &status, WNOHANG);
    if (r == 0) {
      std::lock_guard lock(g_orphan_mutex);
      g_orphan_pids.push_back(proc->pid);
    }
  }
  delete proc;
  handle = nullptr;
#endif
}

uint64_t ProcessHandleId(void* handle) {
  if (!handle) return 0;
#ifdef _WIN32
  return GetProcessId(static_cast<HANDLE>(handle));
#else
  return uint64_t(static_cast<PosixProcess*>(handle)->pid);
#endif
}

namespace {
#ifndef _WIN32
std::vector<std::string> g_launch_args;
#endif
}  // namespace

void SetLaunchArgs(int argc, char* const* argv) {
#ifdef _WIN32
  (void)argc;
  (void)argv;
#else
  g_launch_args.clear();
  for (int i = 1; i < argc; ++i) {
    if (argv[i]) g_launch_args.emplace_back(argv[i]);
  }
#endif
}

std::vector<std::string> LaunchArgs() {
#ifdef _WIN32
  std::vector<std::string> out;
  int argc = 0;
  if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
    for (int i = 1; i < argc; ++i) out.push_back(WideToUtf8(argv[i]));
    LocalFree(argv);
  }
  return out;
#else
  return g_launch_args;
#endif
}

std::string LastSystemError() {
#ifdef _WIN32
  wchar_t text[256] = L"";
  FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), 0, text, 255,
                 nullptr);
  std::string out = WideToUtf8(text);
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return out;
#else
  return std::strerror(errno);
#endif
}

}  // namespace coreoflight
