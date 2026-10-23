#pragma once

// Reading an Xbox 360 disc image (xiso.cpp): the game partition's XDVDFS file system, as a full
// dump (the partition 0xFD90000 into the image, XGD2) or a trimmed image (at 0) has it.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace coreoflight {

class XisoImage {
 public:
  struct Entry {
    std::string path;  // "media/ui/eng/hud.str", '/' between folders
    uint64_t offset = 0;
    uint64_t size = 0;
    bool directory = false;
  };

  // nullptr if the file isn't an Xbox disc image (or can't be read).
  static std::unique_ptr<XisoImage> Open(const std::filesystem::path& path);

  const std::vector<Entry>& entries() const { return entries_; }
  // The files' bytes, without those in the top folder `skip_folder` (any case) if one is given.
  uint64_t total_size(const std::string& skip_folder = {}) const;
  const Entry* Find(const std::string& path) const;  // case-insensitive
  bool Read(const Entry& entry, std::vector<uint8_t>& out);

  // Writes every file under `to` (made if missing), default.xex last, without the top folder
  // `skip_folder` if one is given. `progress(done, total)` is called now and then with the bytes
  // written; returning false stops. False on an error or a stop.
  bool Extract(const std::filesystem::path& to, const std::function<bool(uint64_t, uint64_t)>& progress,
               std::string& error, const std::string& skip_folder = {});

  // Whether `path` ("$SystemUpdate/su20076000_00000000") is in the top folder `folder`, any case.
  static bool InFolder(const std::string& path, const std::string& folder);

 private:
  bool ReadAt(uint64_t offset, void* out, size_t size);
  bool ReadDirectory(uint32_t sector, uint32_t size, const std::string& prefix, int depth);

  std::ifstream file_;
  uint64_t partition_ = 0;
  std::vector<Entry> entries_;
};

// SHA-256 of a buffer, lower-case hex (Windows' CNG).
std::string Sha256Hex(const void* data, size_t size);

// The folders up to `dir` made one by one, an existing one (also a link) taken as it is; false
// when `dir` isn't a folder afterwards. In place of std::filesystem::create_directories, which
// checks each existing part: under Wine a Linux symlink such as Bazzite's /home -> /var/home may
// not pass that, and nothing could be unpacked below it (a colleague's games folder, 2026-10-24;
// C: worked, Z:\home\... didn't).
bool MakeDirs(const std::filesystem::path& dir);

// The text of the last Windows error, with its number, for a log.
std::string LastErrorText();

}  // namespace coreoflight
