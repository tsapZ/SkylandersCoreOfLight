#include "wua_unpack.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <map>
#include <memory>
#include <string_view>

#include "platform.h"
#include "zarchive/zarchivereader.h"

namespace coreoflight {
namespace fs = std::filesystem;

namespace {

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

// Folders deeper than this below content/archives mean a damaged archive (a folder that lists itself would recurse
// until the stack runs out, which no catch stops); the games' archives are flat.
constexpr int kMaxDepth = 32;

// Every file below `dir` into `out`, keyed by its lower-case path below content/archives: a later title's file of the
// same name (the update's) replaces the earlier one's. False on a damaged or unsafe entry. ZArchive finds a child only
// by its path, which it compares without case; two names that differ only in case (none in these games) end up as one
// file, as they would on Windows anyway.
bool Collect(ZArchiveReader& reader, ZArchiveNodeHandle dir, const std::string& archive_path, const fs::path& relative,
             int depth, std::map<std::string, File>& out, std::string& error) {
  if (depth > kMaxDepth) {
    error = "the archive is damaged (folders nested too deep at " + archive_path + ")";
    return false;
  }
  const uint32_t count = reader.GetDirEntryCount(dir);
  for (uint32_t i = 0; i < count; ++i) {
    ZArchiveReader::DirEntry entry;
    if (!reader.GetDirEntry(dir, i, entry)) {
      error = "the archive is damaged (an entry of " + archive_path + " can't be read)";
      return false;
    }
    if (!SafeName(entry.name)) {
      error = "unsafe name in the archive: " + std::string(entry.name);
      return false;
    }
    const std::string path = archive_path + "/" + std::string(entry.name);
    const ZArchiveNodeHandle node = reader.LookUp(path);
    if (node == ZARCHIVE_INVALID_NODE) {
      error = "the archive is damaged (" + path + " can't be found)";
      return false;
    }
    const fs::path rel = relative / PathFromUtf8(entry.name);
    if (entry.isDirectory) {
      if (!Collect(reader, node, path, rel, depth + 1, out, error)) return false;
    } else if (entry.isFile) {
      // A replaced file keeps the game's spelling of its name (what the converter asks for, on a file system that
      // minds case) and takes the update's content.
      const auto [it, added] = out.try_emplace(Lower(GenericPathUtf8(rel)), File{node, rel, reader.GetFileSize(node)});
      if (!added) {
        it->second.node = node;
        it->second.size = reader.GetFileSize(node);
      }
    }
  }
  return true;
}

}  // namespace

bool IsWuaFile(const fs::path& path) {
  std::error_code ec;
  return Lower(PathUtf8(path.extension())) == ".wua" && fs::is_regular_file(path, ec);
}

bool UnpackWuaArchives(const fs::path& wua, const fs::path& target, const std::vector<std::string>& markers,
                       std::atomic<float>* progress, std::atomic<uint64_t>* bytes, std::string& error) {
  try {
    std::unique_ptr<ZArchiveReader> reader(ZArchiveReader::OpenFromFile(wua));
    if (!reader) {
      error = "not a .wua archive (or a damaged one)";
      return false;
    }
    // The title folders at the top: the game (00050000, or a name that isn't a title id) whose content/archives has
    // the markers, and the updates (0005000e) by their lower half.
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
    auto has = [&](const Title& t, const std::string& below, bool directory) {
      return reader->LookUp(t.name + "/content/archives" + below, !directory, directory) != ZARCHIVE_INVALID_NODE;
    };
    const Title* game = nullptr;
    std::string missing = markers.empty() ? std::string() : markers.front();
    for (const Title& t : titles) {
      if ((t.parsed && t.high != "00050000") || !has(t, "", true)) continue;
      const auto lacks = std::find_if(markers.begin(), markers.end(), [&](const std::string& m) { return !has(t, "/" + m, false); });
      if (lacks == markers.end()) {
        game = &t;
        break;
      }
      if (lacks != markers.begin()) missing = *lacks;  // a game with the first marker: name what it lacks
    }
    if (!game) {
      error = missing.empty() ? "no Wii U game with a content/archives folder in the archive"
                              : "no game in the archive has " + missing;
      return false;
    }
    const Title* update = nullptr;
    if (game->parsed) {
      for (const Title& t : titles) {
        if (t.parsed && t.high == "0005000e" && t.low == game->low && has(t, "", true) &&
            (!update || t.version > update->version)) {
          update = &t;
        }
      }
    }

    // The files: the game's, then the update's over them (one entry per name, so each is written once).
    std::map<std::string, File> files;
    for (const Title* t : {game, update}) {
      if (!t) continue;
      const std::string path = t->name + "/content/archives";
      if (!Collect(*reader, reader->LookUp(path, false, true), path, {}, 0, files, error)) return false;
    }
    uint64_t total = 0;
    for (const auto& [key, f] : files) total += f.size;
    if (bytes) *bytes = total;
    const fs::path out_dir = target / "content" / "archives";
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
      error = "can't make " + PathUtf8(out_dir) + ": " + ec.message();
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
    for (const auto& [key, f] : files) {
      const fs::path out = out_dir / f.relative;
      fs::create_directories(out.parent_path(), ec);
      std::ofstream file(out, std::ios::binary | std::ios::trunc);
      if (!file) {
        error = "can't write " + PathUtf8(out);
        return false;
      }
      for (uint64_t at = 0; at < f.size;) {
        const uint64_t want = std::min<uint64_t>(buffer.size(), f.size - at);
        const uint64_t got = reader->ReadFromFile(f.node, at, want, buffer.data());
        if (got != want) {
          error = "the archive is damaged at " + GenericPathUtf8(f.relative);
          return false;
        }
        file.write(buffer.data(), std::streamsize(got));
        at += got;
        written += got;
        if (progress && total) *progress = float(double(written) / double(total));
      }
      if (!file.flush()) {
        error = "can't write " + PathUtf8(out) + " (drive full?)";
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
