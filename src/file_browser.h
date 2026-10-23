#pragma once

// Choosing a disc inside the launcher, with the controller as well as the keyboard and mouse
// (file_browser.cpp; the owner's wish, 2026-10-22, instead of the Windows file dialog, which a pad
// can't drive): folders and .iso files, a folder holding an unpacked disc (default.xex in it), and
// the drives and the usual folders (Downloads, Desktop, Documents) one level above a drive.
// "Discs suchen" (Y, or the first entry above the drives) looks for the owner's discs by itself:
// the ISOs in the usual folders and on the drives, each read and known by its default.xex's
// SHA-256 as when setting one up, listed by game.

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct ImFont;

namespace coreoflight {

class FileBrowser {
 public:
  void Init(ImFont* regular, ImFont* bold, bool german);
  // Opens in `folder` (the last one used if empty, else Downloads).
  void Open(const std::filesystem::path& folder = {});
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
  // A disc the search found: the game (an index of AllGames(), -1 for another version of one
  // of them) and the ISO.
  struct FoundDisc {
    int game;
    std::filesystem::path iso;
  };
  struct Search {
    std::atomic<bool> stop{false}, done{false};
    std::atomic<int> folders{0}, isos{0};
    std::mutex mutex;
    std::vector<FoundDisc> found;
  };
  void List(const std::filesystem::path& folder);  // an empty folder: the drives and places
  void Choose(const Entry& e);
  void StartSearch();
  void ListSearch();  // the search's results as the entries
  void StopSearch();

  ImFont* regular_ = nullptr;
  ImFont* bold_ = nullptr;
  bool german_ = false;
  bool open_ = false;
  std::filesystem::path folder_, last_folder_, chosen_;
  std::vector<Entry> entries_;
  int selected_ = 0;
  std::string message_;
  std::shared_ptr<Search> search_;  // the running or last search; the list shows it while set
  bool searching_view_ = false;
  size_t listed_found_ = 0;  // how many of the search's discs the entries show
};

}  // namespace coreoflight
