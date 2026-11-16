#include "portable.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>

#include "crc32.h"
#include "hub_log.h"
#include "inflate.h"
#include "platform.h"

namespace coreoflight {
namespace fs = std::filesystem;

namespace {

constexpr char kMarker[] = "portable.txt";

uint32_t Get16(const unsigned char* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }
uint32_t Get32(const unsigned char* p) { return Get16(p) | Get16(p + 2) << 16; }

// A ZIP entry's name as a path below the launcher's folder: parts split at / or \, no drive, no
// leading slash, no empty, "." or ".." part.
bool SafeRelative(const std::string& name, fs::path& out) {
  if (name.empty() || name.front() == '/' || name.front() == '\\' || name.find(':') != std::string::npos) return false;
  fs::path p;
  size_t start = 0;
  while (start <= name.size()) {
    size_t end = name.find_first_of("/\\", start);
    if (end == std::string::npos) end = name.size();
    const std::string part = name.substr(start, end - start);
    if (part.empty() || part == "." || part == "..") return false;
    p /= fs::path(std::u8string(part.begin(), part.end()));
    start = end + 1;
  }
  out = p;
  return true;
}

struct Entry {
  fs::path path;  // below the launcher's folder
  std::vector<char> data;
};

// Every file of the ZIP, stored or deflated, each checked against its CRC.
bool ReadZip(const fs::path& file, std::vector<Entry>& entries, bool german, std::string& error) {
  const std::string broken = german ? "Das heruntergeladene Update ist besch\xC3\xA4" "digt." : "The downloaded update is damaged.";
  std::ifstream in(file, std::ios::binary);
  const std::vector<char> zip((std::istreambuf_iterator<char>(in)), {});
  if (zip.size() < 22) {
    error = broken;
    return false;
  }
  const auto* b = reinterpret_cast<const unsigned char*>(zip.data());
  // The end of the central directory, searched from the end (a comment may follow it).
  size_t end = std::string::npos;
  for (size_t i = zip.size() - 22;; --i) {
    if (Get32(b + i) == 0x06054B50) {
      end = i;
      break;
    }
    if (i == 0 || zip.size() - i > 22 + 0xFFFF) break;
  }
  if (end == std::string::npos) {
    error = broken;
    return false;
  }
  const uint32_t count = Get16(b + end + 10), dir_size = Get32(b + end + 12), dir_at = Get32(b + end + 16);
  if (uint64_t(dir_at) + dir_size > end) {
    error = broken;
    return false;
  }
  size_t at = dir_at;
  for (uint32_t i = 0; i < count; ++i) {
    if (at + 46 > end || Get32(b + at) != 0x02014B50) {
      error = broken;
      return false;
    }
    const uint32_t method = Get16(b + at + 10), crc = Get32(b + at + 16), size = Get32(b + at + 20),
                   usize = Get32(b + at + 24), name_len = Get16(b + at + 28), extra_len = Get16(b + at + 30),
                   comment_len = Get16(b + at + 32), offset = Get32(b + at + 42);
    if (at + 46 + name_len > end) {
      error = broken;
      return false;
    }
    const std::string name(zip.data() + at + 46, name_len);
    at += 46 + name_len + extra_len + comment_len;
    if (name.empty() || name.back() == '/' || name.back() == '\\') continue;  // a folder
    Entry entry;
    if (!SafeRelative(name, entry.path) || uint64_t(offset) + 30 > zip.size() || Get32(b + offset) != 0x04034B50) {
      error = broken;
      return false;
    }
    const uint64_t data_at = uint64_t(offset) + 30 + Get16(b + offset + 26) + Get16(b + offset + 28);
    if (data_at + size > zip.size()) {
      error = broken;
      return false;
    }
    if (method == 0 && size == usize) {
      entry.data.assign(zip.begin() + std::ptrdiff_t(data_at), zip.begin() + std::ptrdiff_t(data_at + size));
    } else if (method != 8 || !InflateRaw(zip.data() + data_at, size, usize, entry.data)) {
      error = broken;
      return false;
    }
    if (Crc32(entry.data.data(), entry.data.size()) != crc) {
      error = broken;
      return false;
    }
    entries.push_back(std::move(entry));
  }
  return true;
}

fs::path WithSuffix(const fs::path& p, std::string_view suffix) { return WithPathSuffix(p, suffix); }

std::string Utf8(const fs::path& p) { return PathUtf8(p); }

}  // namespace

bool IsPortable(const fs::path& launcher_dir) {
  std::error_code ec;
  return fs::is_regular_file(launcher_dir / kMarker, ec);
}

bool ApplyPortableUpdate(const fs::path& zip, const fs::path& launcher_dir, const fs::path& exe_name, bool german,
                         std::string& error) {
  std::vector<Entry> entries;
  if (!ReadZip(zip, entries, german, error)) return false;
  bool has_exe = false, has_marker = false;
  for (const Entry& e : entries) {
    has_exe |= e.path == exe_name;
    has_marker |= e.path == fs::path(kMarker);
  }
  if (!has_exe || !has_marker) {
    error = german ? "Das Update ist kein portabler Core of Light." : "The update isn't a portable Core of Light.";
    return false;
  }
  std::error_code ec;
  const std::string cant_write = german ? "In den Ordner von Core of Light l\xC3\xA4sst sich nicht schreiben."
                                        : "Can't write to the launcher's folder.";
  // 1. Every new file next to its place as <name>.new: a full disk or a read-only folder fails here,
  // before anything is replaced.
  std::vector<fs::path> fresh;
  auto drop_fresh = [&] {
    for (const fs::path& f : fresh) fs::remove(f, ec);
  };
  for (const Entry& e : entries) {
    const fs::path target = launcher_dir / e.path;
    fs::create_directories(target.parent_path(), ec);
    const fs::path f = WithSuffix(target, ".new");
    std::ofstream out(f, std::ios::binary | std::ios::trunc);
    out.write(e.data.data(), std::streamsize(e.data.size()));
    out.close();
    if (!out) {
      fs::remove(f, ec);
      drop_fresh();
      error = cant_write;
      return false;
    }
    fresh.push_back(f);
  }
  // 2. The old file out of the way (renamed, as a running exe or DLL can be), the new one in. If a
  // step fails, what was swapped goes back.
  struct Swap {
    fs::path target, old;
  };
  std::vector<Swap> swapped;
  auto roll_back = [&] {
    for (auto it = swapped.rbegin(); it != swapped.rend(); ++it) {
      if (!it->old.empty()) {
        fs::remove(it->target, ec);
        MoveReplaceFile(it->old, it->target);
      } else {
        fs::remove(it->target, ec);
      }
    }
    drop_fresh();
  };
  for (const Entry& e : entries) {
    const fs::path target = launcher_dir / e.path;
    Swap swap{target, {}};
    if (fs::exists(target, ec)) {
      // An .old one from an earlier update that is still in use can't go: then .old2, .old3 ...
      fs::path old = WithSuffix(target, ".old");
      for (int n = 2; fs::exists(old, ec) && !fs::remove(old, ec) && n < 100; ++n) {
        old = WithSuffix(target, ".old" + std::to_string(n));
      }
      if (!MoveFileNewAside(target, old)) {
        HubWarn("portable update: can't move " + Utf8(target) + " aside (" + LastSystemError() + ")");
        roll_back();
        error = cant_write;
        return false;
      }
      swap.old = old;
    }
    swapped.push_back(swap);
    if (!MoveReplaceFile(WithSuffix(target, ".new"), target)) {
      HubWarn("portable update: can't put " + Utf8(target) + " in place (" + LastSystemError() + ")");
      roll_back();
      error = cant_write;
      return false;
    }
  }
  HubLog("portable update: " + std::to_string(entries.size()) + " files replaced in " + Utf8(launcher_dir));
  return true;
}

bool StartLauncher(const fs::path& exe) {
  std::vector<std::string> args;
  for (const std::string& arg : LaunchArgs()) {
    if (arg.starts_with("--portable_update=")) continue;
    args.push_back(arg);
  }
  void* process = SpawnProcess(exe, args, exe.parent_path(), false);
  if (!process) return false;
  CloseProcessHandle(process);
  return true;
}

void RemoveUpdateLeftovers(const fs::path& launcher_dir) {
  std::error_code ec;
  std::vector<fs::path> leftovers;
  for (fs::recursive_directory_iterator it(launcher_dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const std::string ext = PathUtf8(it->path().extension());
    if (ext == ".new" || ext.starts_with(".old")) leftovers.push_back(it->path());
  }
  for (const fs::path& p : leftovers) {
    // A file whose name ends in .old<n> without its file next to it isn't one of ours: leave it.
    if (!fs::exists(p.parent_path() / p.stem(), ec)) continue;
    fs::remove(p, ec);
  }
}

}  // namespace coreoflight
