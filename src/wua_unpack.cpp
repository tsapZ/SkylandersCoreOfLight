#include "wua_unpack.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <memory>
#include <string_view>
#include <vector>

#include "zarchive/zarchivereader.h"

namespace coreoflight {
namespace fs = std::filesystem;

namespace {

// UTF-8 in and out of paths (C++20's u8string is std::u8string; platform.h's helpers would pull the window code into
// the test).
std::string Utf8(const std::u8string& s) { return std::string(s.begin(), s.end()); }
fs::path FromUtf8(std::string_view s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string Lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return out;
}

// "<16 hex digits>_v<version>": the title id's upper and lower halves (lower case) and the version, or false.
bool ParseTitleFolder(std::string_view name, std::string& high, std::string& low, unsigned& version) {
  if (name.size() < 19 || name[16] != '_' || (name[17] != 'v' && name[17] != 'V')) return false;
  for (size_t i = 0; i < 16; ++i) {
    if (!std::isxdigit(static_cast<unsigned char>(name[i]))) return false;
  }
  unsigned v = 0;
  for (size_t i = 18; i < name.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(name[i])) || v > 100000) return false;
    v = v * 10 + unsigned(name[i] - '0');
  }
  high = Lower(name.substr(0, 8));
  low = Lower(name.substr(8, 8));
  version = v;
  return true;
}

// A name from the archive that is safe as one path part below the target.
bool SafeName(std::string_view name) {
  return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\:") == std::string_view::npos;
}

struct File {
  ZArchiveNodeHandle node;
  fs::path relative;  // below content/archives
  uint64_t size;
};

// Every file below `dir` (recursive), with its path below it; false on a name that isn't safe.
bool Collect(ZArchiveReader& reader, ZArchiveNodeHandle dir, const std::string& archive_path, const fs::path& relative,
             std::vector<File>& out, std::string& error) {
  const uint32_t count = reader.GetDirEntryCount(dir);
  for (uint32_t i = 0; i < count; ++i) {
    ZArchiveReader::DirEntry entry;
    if (!reader.GetDirEntry(dir, i, entry)) continue;
    if (!SafeName(entry.name)) {
      error = "unsafe name in the archive: " + std::string(entry.name);
      return false;
    }
    const std::string path = archive_path + "/" + std::string(entry.name);
    const ZArchiveNodeHandle node = reader.LookUp(path);
    if (node == ZARCHIVE_INVALID_NODE) continue;
    const fs::path rel = relative / FromUtf8(entry.name);
    if (entry.isDirectory) {
      if (!Collect(reader, node, path, rel, out, error)) return false;
    } else if (entry.isFile) {
      out.push_back({node, rel, reader.GetFileSize(node)});
    }
  }
  return true;
}

}  // namespace

bool IsWuaFile(const fs::path& path) { return Lower(Utf8(path.extension().u8string())) == ".wua"; }

bool UnpackWuaArchives(const fs::path& wua, const fs::path& target, const std::string& marker,
                       std::atomic<float>* progress, std::atomic<uint64_t>* bytes, std::string& error) {
  try {
    std::unique_ptr<ZArchiveReader> reader(ZArchiveReader::OpenFromFile(wua));
    if (!reader) {
      error = "not a .wua archive (or a damaged one)";
      return false;
    }
    // The title folders at the top: the game (00050000) with content/archives, the one with the marker first; and the
    // updates (0005000e) by their lower half.
    struct Title {
      std::string name, high, low;
      unsigned version = 0;
      bool parsed = false;
    };
    std::vector<Title> titles;
    const ZArchiveNodeHandle root = reader->LookUp("");
    const uint32_t count = root == ZARCHIVE_INVALID_NODE ? 0 : reader->GetDirEntryCount(root);
    for (uint32_t i = 0; i < count; ++i) {
      ZArchiveReader::DirEntry entry;
      if (!reader->GetDirEntry(root, i, entry) || !entry.isDirectory || !SafeName(entry.name)) continue;
      Title t;
      t.name = std::string(entry.name);
      t.parsed = ParseTitleFolder(entry.name, t.high, t.low, t.version);
      titles.push_back(std::move(t));
    }
    auto has = [&](const Title& t, const std::string& below) {
      return reader->LookUp(t.name + "/content/archives" + below) != ZARCHIVE_INVALID_NODE;
    };
    const Title* game = nullptr;
    for (int pass = 0; pass < 2 && !game; ++pass) {
      for (const Title& t : titles) {
        const bool is_game = !t.parsed || t.high == "00050000";
        if (is_game && has(t, "") && (pass == 1 || has(t, "/" + marker))) {
          game = &t;
          break;
        }
      }
    }
    if (!game) {
      error = "no Wii U game with a content/archives folder in the archive";
      return false;
    }
    const Title* update = nullptr;
    if (game->parsed) {
      for (const Title& t : titles) {
        if (t.parsed && t.high == "0005000e" && t.low == game->low && has(t, "") &&
            (!update || t.version > update->version)) {
          update = &t;
        }
      }
    }

    // The files: the game's, then the update's (written later, so they replace the game's of the same name).
    std::vector<File> files;
    for (const Title* t : {game, update}) {
      if (!t) continue;
      const std::string path = t->name + "/content/archives";
      if (!Collect(*reader, reader->LookUp(path), path, {}, files, error)) return false;
    }
    uint64_t total = 0;
    for (const File& f : files) total += f.size;
    if (bytes) *bytes = total;
    const fs::path out_dir = target / "content" / "archives";
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
      error = "can't make " + Utf8(out_dir.u8string()) + ": " + ec.message();
      return false;
    }
    const fs::space_info room = fs::space(out_dir, ec);
    if (!ec && room.available < total + (uint64_t(1) << 30)) {
      error = "not enough room on the drive: " + std::to_string(total >> 20) + " MB to unpack, " +
              std::to_string(room.available >> 20) + " MB free";
      return false;
    }

    std::vector<char> buffer(4 << 20);
    uint64_t written = 0;
    for (const File& f : files) {
      const fs::path out = out_dir / f.relative;
      fs::create_directories(out.parent_path(), ec);
      std::ofstream file(out, std::ios::binary | std::ios::trunc);
      if (!file) {
        error = "can't write " + Utf8(out.u8string());
        return false;
      }
      for (uint64_t at = 0; at < f.size;) {
        const uint64_t want = std::min<uint64_t>(buffer.size(), f.size - at);
        const uint64_t got = reader->ReadFromFile(f.node, at, want, buffer.data());
        if (got != want) {
          error = "the archive is damaged at " + Utf8(f.relative.generic_u8string());
          return false;
        }
        file.write(buffer.data(), std::streamsize(got));
        at += got;
        written += got;
        if (progress && total) *progress = float(double(written) / double(total));
      }
      if (!file.flush()) {
        error = "can't write " + Utf8(out.u8string()) + " (drive full?)";
        return false;
      }
    }
    if (progress) *progress = 1.0f;
    return true;
  } catch (const std::exception& e) {
    // ZArchiveReader's tables are std::vector::at: a damaged archive throws.
    error = std::string("the archive is damaged (") + e.what() + ")";
    return false;
  }
}

}  // namespace coreoflight
