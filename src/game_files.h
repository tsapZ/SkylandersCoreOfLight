#pragma once

// "Check game files" in a game's settings (#41, game_files.cpp): whether the disc in its rom folder
// is all there. A disc import leaves the list of the files it wrote, with their sizes, in the rom
// folder ("Core of Light disc files.txt"); the check compares the folder with it. A disc set up
// before there was a list is checked by its default.xex (the SHA-256 its repository pins), and the
// list is made from the folder then, for the next check. Files Imaginators' language import
// changed on purpose (language_import.h, their backup in archives\german_text_backup) only have to
// be there, whatever their size.

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace coreoflight {

inline constexpr char kDiscFileList[] = "Core of Light disc files.txt";

// The list of a disc's files ('/' between folders) and sizes, into the rom folder.
bool WriteDiscFileList(const std::filesystem::path& rom, const std::vector<std::pair<std::string, uint64_t>>& files);

struct GameFilesCheck {
  bool had_list = false;   // the disc import's list was there (else the list was made now)
  bool xex_ok = false;     // default.xex is there and is the supported disc's
  size_t files = 0;        // files checked
  std::vector<std::string> missing, changed;  // paths in the rom folder
  bool ok() const { return xex_ok && missing.empty() && changed.empty(); }
};

// `xex_sha256`: the game's supported default.xex hashes (games.h Game::xex_sha256, comma separated).
GameFilesCheck CheckGameFiles(const std::filesystem::path& rom, const std::string& xex_sha256);

}  // namespace coreoflight
