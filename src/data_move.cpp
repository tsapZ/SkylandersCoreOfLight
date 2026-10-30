#include "data_move.h"

#include <thread>

#include "games.h"
#include "hub_log.h"
#include "portal/data_folder.h"
#include "profiles.h"

namespace coreoflight {
namespace fs = std::filesystem;
namespace {

std::string U8(const fs::path& p) {
  const std::u8string u8 = p.u8string();
  return std::string(u8.begin(), u8.end());
}

bool HasFiles(const fs::path& dir) {
  std::error_code ec;
  return fs::is_directory(dir, ec) && !fs::is_empty(dir, ec);
}

// One old folder copied to its new place, if the new one isn't there yet.
void CopyOver(const fs::path& old_dir, const fs::path& new_dir) {
  std::error_code ec;
  if (!HasFiles(old_dir) || fs::exists(new_dir, ec) || fs::exists(old_dir / skylanders::kMovedNote, ec)) return;
  fs::create_directories(new_dir.parent_path(), ec);
  const skylanders::CopyResult copy = skylanders::CopyFolderVerified(old_dir, new_dir);
  if (!copy.ok) {
    HubLog("data folder: could not copy " + U8(old_dir) + ": " + copy.error);
    return;
  }
  skylanders::WriteMovedNote(old_dir, new_dir);
  HubLog("data folder: copied " + U8(old_dir) + " to " + U8(new_dir) + " (" + std::to_string(copy.files) + " files)");
}

// Every (old, new) pair: the main profile's game folders in Documents, and in each other profile
// its game folders and its Skylanders\Figures / Pictures.
std::vector<OldData> Pairs() {
  std::vector<OldData> pairs;
  const fs::path base = SkylandersDir();
  std::vector<fs::path> profiles;
  std::error_code ec;
  for (fs::directory_iterator it(base / "Profiles", ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    if (it->is_directory(ec)) profiles.push_back(it->path());
  }
  for (const Game& game : AllGames()) {
    const fs::path exe = game.exe;
    const fs::path old_name = UserDataName(exe), new_name = GameFolderName(exe);
    pairs.push_back({DocumentsDir() / old_name, base / new_name});
    for (const fs::path& p : profiles) pairs.push_back({p / old_name, p / new_name});
  }
  for (const fs::path& p : profiles) {
    pairs.push_back({p / "Skylanders" / "Figures", p / "Figures"});
    pairs.push_back({p / "Skylanders" / "Pictures", p / "Pictures"});
  }
  return pairs;
}

}  // namespace

void MoveOldData() {
  for (const OldData& d : Pairs()) CopyOver(d.old_dir, d.new_dir);
  // A profile's play time (profiles.cpp PlayTimeFile: next to its figure folder) goes along.
  std::error_code ec;
  for (fs::directory_iterator it(SkylandersDir() / "Profiles", ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    const fs::path old_file = it->path() / "Skylanders" / "playtime.txt", new_file = it->path() / "playtime.txt";
    std::error_code file_ec;
    if (fs::exists(old_file, file_ec) && !fs::exists(new_file, file_ec)) fs::copy_file(old_file, new_file, file_ec);
  }
}

std::vector<OldData> OldDataToDelete() {
  std::vector<OldData> list;
  std::error_code ec;
  for (const OldData& d : Pairs()) {
    if (!fs::exists(d.old_dir / skylanders::kMovedNote, ec)) continue;
    if (skylanders::AllFilesThere(d.old_dir, d.new_dir)) list.push_back(d);
  }
  // A moved data folder (DataFolderMove) that wasn't deleted yet: its note is in the old folder.
  // Not looked for here: the launcher keeps that one in its own list while it runs.
  return list;
}

int DeleteOldData(const std::vector<OldData>& list) {
  int deleted = 0;
  for (const OldData& d : list) {
    // Checked again: a game may have written to the old place since (an older version of it).
    if (!skylanders::AllFilesThere(d.old_dir, d.new_dir)) {
      HubLog("data folder: kept " + U8(d.old_dir) + " (not every file is in " + U8(d.new_dir) + ")");
      continue;
    }
    std::error_code ec;
    fs::remove_all(d.old_dir, ec);
    if (ec) {
      HubLog("data folder: could not delete " + U8(d.old_dir) + ": " + ec.message());
      continue;
    }
    ++deleted;
    HubLog("data folder: deleted " + U8(d.old_dir));
    // A profile's old Skylanders folder goes too once only its play time is left in it (copied
    // next to the profile's new figure folder by MoveOldData).
    const fs::path parent = d.old_dir.parent_path();
    if (parent.filename() == "Skylanders" && parent.parent_path().parent_path().filename() == "Profiles") {
      if (fs::exists(parent.parent_path() / "playtime.txt", ec)) fs::remove(parent / "playtime.txt", ec);
      if (fs::is_empty(parent, ec)) fs::remove(parent, ec);
    }
  }
  return deleted;
}

std::string OldDataNames(const std::vector<OldData>& list, size_t max, const std::string& documents) {
  std::string names;
  const fs::path docs = DocumentsDir();
  for (size_t i = 0; i < list.size() && i < max; ++i) {
    if (!names.empty()) names += ", ";
    std::error_code ec;
    const fs::path rel = fs::relative(list[i].old_dir, docs, ec);
    names += !ec && !rel.empty() && rel.native()[0] != L'.' ? documents + U8(rel) : U8(list[i].old_dir);
  }
  if (list.size() > max) names += ", ...";
  return names;
}

fs::path DataFolderFor(const fs::path& picked) {
  return picked.filename() == "Skylanders" ? picked : picked / "Skylanders";
}

fs::path SetAsideCopiedFolder(const fs::path& dir) {
  std::error_code ec;
  if (!fs::exists(dir / skylanders::kMovedNote, ec)) return {};
  for (int i = 1; i < 100; ++i) {
    const fs::path aside =
        dir.parent_path() / (dir.filename().wstring() + (i == 1 ? L" (old)" : L" (old " + std::to_wstring(i) + L")"));
    if (fs::exists(aside, ec)) continue;
    fs::rename(dir, aside, ec);
    if (ec) {
      HubLog("data folder: could not rename " + U8(dir) + ": " + ec.message());
      return {};
    }
    HubLog("data folder: " + U8(dir) + " (copied away before) is now " + U8(aside));
    return aside;
  }
  return {};
}

bool DataFolderMove::Start(const fs::path& from, const fs::path& to, std::string& error) {
  if (running()) {
    error = "a move is running";
    return false;
  }
  std::error_code ec;
  if (fs::exists(to, ec) && !fs::is_empty(to, ec)) {
    error = U8(to) + " is there already and not empty";
    return false;
  }
  if (fs::exists(to, ec)) fs::remove(to, ec);  // an empty folder: the copy makes it again
  auto state = std::make_shared<State>();
  state->from = from;
  state->to = to;
  state->running = true;
  state_ = state;
  std::thread([state] {
    std::error_code ec;
    if (!HasFiles(state->from)) {
      fs::create_directories(state->to, ec);
      if (ec) state->error = "cannot create " + U8(state->to) + ": " + ec.message();
    } else {
      fs::create_directories(state->to.parent_path(), ec);
      const skylanders::CopyResult copy =
          skylanders::CopyFolderVerified(state->from, state->to, [&state](const fs::path& file) {
            const size_t n = ++state->files;
            if (n % 500 == 1) HubLog("data folder: copying file " + std::to_string(n) + ", " + U8(file));
          });
      state->files = copy.files;
      HubLog("data folder: copy " + std::string(copy.ok ? "done" : "failed") + ", " + std::to_string(copy.files) +
             " files, " + std::to_string(copy.bytes / (1024 * 1024)) + " MB");
      if (!copy.ok) {
        state->error = copy.error;
      } else {
        // Nothing in it but the launcher's settings (a fresh setup): it goes at once.
        bool only_settings = true;
        for (fs::recursive_directory_iterator it(state->from, ec), end; it != end && !ec; it.increment(ec)) {
          std::error_code file_ec;
          if (it->is_regular_file(file_ec) && it->path().filename() != "coreoflight.toml") only_settings = false;
        }
        if (only_settings && !ec) {
          fs::remove_all(state->from, ec);
          state->old_deleted = !ec;
        }
        if (!state->old_deleted) skylanders::WriteMovedNote(state->from, state->to);
      }
    }
    state->running = false;
    state->finished = true;
  }).detach();
  return true;
}

bool DataFolderMove::TakeResult(std::string& error, fs::path& old_dir, fs::path& new_dir, bool& old_deleted) {
  if (!state_ || !state_->finished) return false;
  error = state_->error;
  old_dir = state_->from;
  new_dir = state_->to;
  old_deleted = state_->old_deleted;
  state_.reset();
  return true;
}

}  // namespace coreoflight
