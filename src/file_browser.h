#pragma once

// Choosing a disc inside the launcher, with the controller as well as the keyboard and mouse
// (file_browser.cpp; the owner's wish, 2026-10-22, instead of the Windows file dialog, which a pad
// can't drive): folders and .iso files, a folder holding an unpacked disc (default.xex in it), and
// the drives and the usual folders (Downloads, Desktop, Documents) one level above a drive.
// "Discs suchen" (Y, or the first entry above the drives) looks for the owner's discs by itself:
// the ISOs in the usual folders and on the drives, each read and known by its default.xex's
// SHA-256 as when setting one up, listed by game (disc_search.h).

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "disc_search.h"

struct ImFont;

namespace coreoflight {

// The other use: choosing the games folder (a colleague's wish, 2026-10-23, playing on SteamOS with
// the games on another drive): folders only, "Diesen Ordner nehmen" in each, and under Proton the
// Linux home folder among the places (Wine's Z: drive is the Linux root).
class FileBrowser {
 public:
  void Init(ImFont* regular, ImFont* bold, bool german, bool folders_only = false);
  // Opens in `folder` (the last one used if empty, else Downloads, or Documents for folders).
  void Open(const std::filesystem::path& folder = {});
  // In folders-only mode: the data folder is being chosen (data_move.h), not the games folder; the
  // title and the "this folder" row say so. Set before Open.
  void SetForData(bool data) { for_data_ = data; }
  bool is_open() const { return open_; }
  void Draw(float width, float height);
  // The ISO or folder chosen since the last call, or empty.
  std::filesystem::path TakeChosen();

 private:
  struct Entry {
    enum class Kind { kUp, kThisFolder, kFolder, kIso, kPlace, kSearch, kFound } kind;
    std::string label;
    std::filesystem::path path;
  };
  void List(const std::filesystem::path& folder);  // an empty folder: the drives and places
  void Choose(Entry e);  // a copy (List clears the entries)
  void StartSearch();
  void ListSearch();  // the search's results as the entries
  void StopSearch();

  ImFont* regular_ = nullptr;
  ImFont* bold_ = nullptr;
  bool german_ = false;
  bool folders_only_ = false;  // choosing a folder (the games folder), not a disc
  bool for_data_ = false;      // the folder is the data folder's place (SetForData)
  bool open_ = false;
  std::filesystem::path folder_, last_folder_, chosen_;
  std::vector<Entry> entries_;
  int selected_ = 0;
  int scroll_ = 0;  // the first entry shown; follows the chosen one only when it leaves the view
  std::string message_;
  std::shared_ptr<DiscSearch> search_;  // the running or last search; the list shows it while set
  bool searching_view_ = false;
  size_t listed_found_ = 0;  // how many of the search's discs the entries show
};

}  // namespace coreoflight
