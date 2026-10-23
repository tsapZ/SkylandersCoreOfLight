#pragma once

// A game's saves and copies of them (saves.cpp; the owner's wish, 2026-10-23: manage the saves in
// the launcher). A game keeps each save as a folder in the profile's user data,
// <user data>\B13EBABEBABEBABE\<title id>\00000001\<save> (one .xsg file in it), with its content
// header in ...\<title id>\Headers\00000001\<save>.header. On every save the SDK first keeps the
// version before it, <user data>\save-backup\previous\<profile>_<title>_00000001_<save>, as
// package\ (the save folder's files), header and manifest.txt (the two paths, relative to the user
// data). The launcher's own copies have the same layout, in
// Documents\Skylanders\Backups\<game key>\<profile id, "main" for the main profile>\<save>\<date>.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace coreoflight {

struct Game;
struct Profile;

struct SaveSlot {
  std::string name;              // the save folder's name, "SWAPFORCE_2"
  std::filesystem::path folder;  // the save folder
  std::filesystem::path header;  // its content header
  uint64_t written = 0;          // its newest file, as a Windows file time
  bool options = false;          // the game's options (..._OPTIONS), not a game
  int number = 0;                // the number at the end of the name (..._2), 0 if there is none
};

struct SaveCopy {
  std::filesystem::path folder;  // with package\, header and manifest.txt
  uint64_t written = 0;          // its save's newest file
  bool previous = false;         // the SDK's version before the last save
  bool before_restore = false;   // made by a restore, of the save it replaced
  bool automatic = false;        // made by AutoBackup
};

// The profile's saves of the game (exe: which game), games first by number, then the options.
std::vector<SaveSlot> ListSaves(const Profile& p, const std::filesystem::path& exe);
// The copies of a save: the SDK's previous version first, then the launcher's, newest first.
std::vector<SaveCopy> ListCopies(const Profile& p, const Game& game, const std::filesystem::path& exe,
                                 const SaveSlot& slot);
// Copies the save into the launcher's backups; `mark` names a marker file in the copy
// ("before-restore", "auto") that says how it was made.
bool BackupSave(const Profile& p, const Game& game, const SaveSlot& slot, const char* mark = nullptr);
// Puts the copy back as the save; the save as it is now is backed up first (before_restore).
bool RestoreSave(const Profile& p, const Game& game, const SaveSlot& slot, const SaveCopy& copy);
// The automatic copies (the owner's wish, 2026-10-23: in case a save breaks): each save that changed
// since its last automatic copy is copied (marked "auto"), and only the newest `keep` automatic
// copies of a save stay. A save is about 100 KB. How many were copied.
int AutoBackup(const Profile& p, const Game& game, const std::filesystem::path& exe, size_t keep = 10);
// Why the last copy or restore failed, for the message.
std::string LastSaveError();
// Where the launcher's copies of the profile's saves of the game are.
std::filesystem::path BackupsDir(const Profile& p, const Game& game);

}  // namespace coreoflight
