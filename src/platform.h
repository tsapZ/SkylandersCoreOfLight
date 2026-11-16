#pragma once

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coreoflight {

// UTF-8 / path conversions (avoids ANSI code page issues on Windows and char/wchar_t mismatches).
std::string PathUtf8(const std::filesystem::path& path);
std::filesystem::path PathFromUtf8(std::string_view utf8);
std::string GenericPathUtf8(const std::filesystem::path& path);
std::filesystem::path WithPathSuffix(const std::filesystem::path& path, std::string_view suffix);
// Whether `path` is inside the portal's "real" toy folder (/real/ or \real\).
bool IsRealToySubpath(const std::filesystem::path& path);

// Standard OS paths and environment info.
std::filesystem::path SelfExePath();
std::filesystem::path LocalAppDataDir();
std::filesystem::path DesktopDir();
std::filesystem::path UserDownloadsDir();
std::filesystem::path TempDir();
std::string EnvVarUtf8(const char* name);
std::string CurrentUserName();
std::string CurrentComputerName();
std::string CpuModelName();
std::string OsVersionSummary();
bool IsGermanOsLocale();

// Time helpers.
uint64_t MonotonicMs();
// Windows FILETIME ticks (100 ns since 1601-01-01 UTC), used by releases.txt and save timestamps.
uint64_t NowFileTimeTicks();
uint64_t PathFileTimeTicks(const std::filesystem::path& path);
struct LocalDateTime {
  uint16_t year = 0;
  uint16_t month = 0;
  uint16_t day = 0;
  uint16_t hour = 0;
  uint16_t minute = 0;
  uint16_t second = 0;
  uint16_t millisecond = 0;
};
LocalDateTime NowLocalDateTime();
std::tm ToLocalTm(std::time_t t);
bool FileTimeToLocalTm(uint64_t filetime_ticks, std::tm& out);

// Filesystem, shell, hashing, and archive helpers.
std::optional<uint64_t> FreeDiskBytes(std::filesystem::path path);
bool MoveReplaceFile(const std::filesystem::path& from, const std::filesystem::path& to);
bool MoveFileNewAside(const std::filesystem::path& from, const std::filesystem::path& to);
bool MovePathToTrash(const std::filesystem::path& path);
bool OpenInFileManager(const std::filesystem::path& path, bool select_file = false);
bool MakeExecutable(const std::filesystem::path& path);
std::string Sha256FileHex(const std::filesystem::path& path);
bool BinaryContainsString(const std::filesystem::path& file, std::string_view needle);
bool ExtractArchive(const std::filesystem::path& archive_path,
                    const std::function<std::filesystem::path(const std::string& rel_path)>& map_dest,
                    std::string& error);

// Subprocess handles: opaque void* matches games.h / launcher_ui.h across platforms
// (HANDLE on Windows, heap PosixProcess* on POSIX).
void* SpawnProcess(const std::filesystem::path& exe, const std::vector<std::string>& args,
                   const std::filesystem::path& cwd = {}, bool no_window = false);
bool IsProcessRunning(void* handle, uint32_t wait_ms = 0, int* exit_code = nullptr);
void TerminateAndCloseProcess(void*& handle);
void CloseProcessHandle(void*& handle);
uint64_t ProcessHandleId(void* handle);
void SetLaunchArgs(int argc, char* const* argv);
std::vector<std::string> LaunchArgs();
std::string LastSystemError();

}  // namespace coreoflight
