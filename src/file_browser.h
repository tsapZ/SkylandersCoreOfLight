#pragma once

// Choosing a disc inside the launcher, with the controller as well as the keyboard and mouse
// (file_browser.cpp; the owner's wish, 2026-10-22, instead of the Windows file dialog, which a pad
// can't drive): folders and .iso files, a folder holding an unpacked disc (default.xex in it), and
// the drives and the usual folders (Downloads, Desktop, Documents) one level above a drive.

#include <filesystem>
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
    enum class Kind { kUp, kThisFolder, kFolder, kIso, kPlace } kind;
    std::string label;
    std::filesystem::path path;
  };
  void List(const std::filesystem::path& folder);  // an empty folder: the drives and places
  void Choose(const Entry& e);

  ImFont* regular_ = nullptr;
  ImFont* bold_ = nullptr;
  bool german_ = false;
  bool open_ = false;
  std::filesystem::path folder_, last_folder_, chosen_;
  std::vector<Entry> entries_;
  int selected_ = 0;
  std::string message_;
};

}  // namespace coreoflight
