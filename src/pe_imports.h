// pe_imports.h: which DLLs a game's exe needs and which of them are gone. A game that ends at once with
// 0xC0000135 (STATUS_DLL_NOT_FOUND) writes no log; Windows Security had taken every game's rexruntime.dll
// as a false "Trojan" (Core of Light #146), and the launcher only said "a DLL missing".
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace coreoflight {

// The DLL names in a Windows exe's or DLL's import directory (delay loads left out: those load on demand,
// not at start). Empty when the file can't be read or isn't a PE image.
std::vector<std::string> PeImports(const std::filesystem::path& file);

// The imports of `exe` and, following them, of every DLL found next to it, that are neither in the exe's
// folder nor in `system_dir` (Windows' System32); API sets (api-ms-*, ext-ms-*) are Windows' own and left
// out. Each name once, in the order met.
std::vector<std::string> MissingDlls(const std::filesystem::path& exe, const std::filesystem::path& system_dir);

// Windows' System32 folder (empty elsewhere).
std::filesystem::path SystemDllDir();

}  // namespace coreoflight
