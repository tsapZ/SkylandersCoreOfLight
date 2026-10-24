#pragma once

// The move to one data folder for every game (skylanders-portal data_folder.h; the owner's call,
// 2026-10-24): Documents\Skylanders (or a folder picked here) with Figures, Pictures, Profiles and
// one folder per game, where each game had a folder of its own in Documents before.
//
// At its start the launcher copies what is still in the old places (a game copies its own folder
// too, the first time it runs): each game's Documents\<game>recomp, and in each profile its
// <game>recomp and Skylanders\Figures / Pictures. The old folders stay until the owner says they
// may go, and only folders whose every file is in the new place are offered.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace coreoflight {

struct OldData {
  std::filesystem::path old_dir, new_dir;
};

// Copies what is still in the old places (see above) and logs each; at the launcher's start.
void MoveOldData();
// The old folders whose files are all in their new place, to offer deleting (the game folders and
// the profiles' old Skylanders folders).
std::vector<OldData> OldDataToDelete();
// Deletes them, each checked again first; how many went.
int DeleteOldData(const std::vector<OldData>& list);
// "Dokumente\ssarecomp, Dokumente\giantsrecomp" for the question (`documents`: "Dokumente\"), at
// most `max` of them named.
std::string OldDataNames(const std::vector<OldData>& list, size_t max, const std::string& documents);

// The whole data folder to another place (the setup's or the settings' "Spieldaten-Ordner"): copied
// in the background, then the games told (the pointer file). The old folder is then offered for
// deleting like the old per-game folders, or goes at once when it held nothing but the launcher's
// settings (a fresh setup asks nothing).
class DataFolderMove {
 public:
  // `to` is the new data folder itself (...\Skylanders). False if one runs or `to` isn't usable
  // (`error` says why).
  bool Start(const std::filesystem::path& from, const std::filesystem::path& to, std::string& error);
  bool running() const { return state_ && state_->running; }
  // Once, when it has finished: true and what happened (an empty error when it worked).
  bool TakeResult(std::string& error, std::filesystem::path& old_dir, std::filesystem::path& new_dir,
                  bool& old_deleted);
  // Files copied so far, for the screen while it runs.
  size_t files() const { return state_ ? state_->files.load() : 0; }

 private:
  struct State {
    std::atomic<bool> running{false};
    std::atomic<bool> finished{false};
    std::atomic<size_t> files{0};
    std::string error;
    std::filesystem::path from, to;
    bool old_deleted = false;
  };
  std::shared_ptr<State> state_;
};

// The data folder for a folder picked in the browser: <picked>\Skylanders, or the picked folder when
// it is called Skylanders itself.
std::filesystem::path DataFolderFor(const std::filesystem::path& picked);

}  // namespace coreoflight
