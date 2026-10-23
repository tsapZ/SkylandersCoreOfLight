// XDVDFS, the Xbox discs' file system (as extract-xiso and Xenia read it): 2048-byte sectors; at
// sector 32 of the game partition the volume descriptor, "MICROSOFT*XBOX*MEDIA" at +0, the root
// directory's sector (u32, little-endian) at +0x14 and its size at +0x18. A directory is a table
// of entries forming a binary tree: u16 left and u16 right (the subtrees' entries, in 4-byte units
// from the table's start, 0 for none, 0xFFFF for the padding at a sector's end), u32 sector, u32
// size, u8 attributes (0x10 a directory), u8 name length, the name. A full dump of an Xbox 360 disc
// (XGD2, 8,738,846,720 bytes) has the game partition 0xFD90000 into the image; XGD3 discs at
// 0x2080000, the first Xbox's at 0x18300000, a trimmed image at 0.

#include "xiso.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <system_error>

namespace coreoflight {
namespace {

namespace fs = std::filesystem;

constexpr uint64_t kSector = 2048;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr uint64_t kPartitions[] = {0xFD90000, 0x2080000, 0x18300000, 0};

uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t U32(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); }

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// A name that stays inside the folder it is extracted to.
bool SafeName(const std::string& name) {
  if (name.empty() || name == "." || name == "..") return false;
  return name.find_first_of("/\\:*?\"<>|") == std::string::npos &&
         std::none_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

}  // namespace

std::unique_ptr<XisoImage> XisoImage::Open(const fs::path& path) {
  auto image = std::unique_ptr<XisoImage>(new XisoImage());
  image->file_.open(path, std::ios::binary);
  if (!image->file_) return nullptr;
  for (const uint64_t partition : kPartitions) {
    uint8_t volume[kSector];
    if (!image->ReadAt(partition + 32 * kSector, volume, sizeof(volume))) continue;
    if (std::memcmp(volume, kMagic, sizeof(kMagic) - 1) != 0) continue;
    image->partition_ = partition;
    if (!image->ReadDirectory(U32(volume + 0x14), U32(volume + 0x18), "", 0)) return nullptr;
    return image->entries_.empty() ? nullptr : std::move(image);
  }
  return nullptr;
}

bool XisoImage::ReadAt(uint64_t offset, void* out, size_t size) {
  file_.clear();
  file_.seekg(std::streamoff(offset));
  file_.read(static_cast<char*>(out), std::streamsize(size));
  return size_t(file_.gcount()) == size;
}

bool XisoImage::ReadDirectory(uint32_t sector, uint32_t size, const std::string& prefix, int depth) {
  if (depth > 32 || size == 0 || size > (64u << 20)) return depth == 0 ? false : true;
  std::vector<uint8_t> table(size);
  if (!ReadAt(partition_ + uint64_t(sector) * kSector, table.data(), size)) return false;
  std::vector<uint8_t> seen(size / 4 + 1);
  std::vector<uint32_t> stack = {0};
  while (!stack.empty()) {
    const uint32_t at = stack.back() * 4;
    stack.pop_back();
    if (at + 14 > size || seen[at / 4]) continue;
    seen[at / 4] = 1;
    const uint8_t* e = table.data() + at;
    const uint16_t left = U16(e), right = U16(e + 2);
    if (left == 0xFFFF) continue;  // padding
    const uint32_t start = U32(e + 4), length = U32(e + 8);
    const uint8_t attributes = e[12], name_length = e[13];
    if (at + 14 + name_length > size) continue;
    const std::string name(reinterpret_cast<const char*>(e + 14), name_length);
    if (left) stack.push_back(left);
    if (right) stack.push_back(right);
    if (!SafeName(name)) continue;
    Entry entry;
    entry.path = prefix + name;
    entry.offset = partition_ + uint64_t(start) * kSector;
    entry.size = length;
    entry.directory = (attributes & 0x10) != 0;
    entries_.push_back(entry);
    if (entry.directory && !ReadDirectory(start, length, entry.path + "/", depth + 1)) return false;
  }
  return true;
}

bool XisoImage::InFolder(const std::string& path, const std::string& folder) {
  if (folder.empty()) return false;
  const std::string p = Lower(path), f = Lower(folder);
  return p == f || (p.size() > f.size() && p.compare(0, f.size(), f) == 0 && p[f.size()] == '/');
}

uint64_t XisoImage::total_size(const std::string& skip_folder) const {
  uint64_t total = 0;
  for (const Entry& e : entries_) total += e.directory || InFolder(e.path, skip_folder) ? 0 : e.size;
  return total;
}

const XisoImage::Entry* XisoImage::Find(const std::string& path) const {
  const std::string want = Lower(path);
  for (const Entry& e : entries_) {
    if (Lower(e.path) == want) return &e;
  }
  return nullptr;
}

bool XisoImage::Read(const Entry& entry, std::vector<uint8_t>& out) {
  if (entry.directory || entry.size > (512ull << 20)) return false;
  out.resize(size_t(entry.size));
  return ReadAt(entry.offset, out.data(), out.size());
}

bool XisoImage::Extract(const fs::path& to, const std::function<bool(uint64_t, uint64_t)>& progress,
                        std::string& error, const std::string& skip_folder) {
  const uint64_t total = total_size(skip_folder);
  uint64_t done = 0;
  std::error_code ec;
  fs::create_directories(to, ec);
  std::vector<char> buffer(size_t(4) << 20);
  // default.xex last: the launcher and the game take a folder with it as a complete disc, so one
  // left half done (the launcher closed, the disk full) never looks ready.
  std::vector<const Entry*> order;
  for (const Entry& e : entries_) {
    if (!InFolder(e.path, skip_folder)) order.push_back(&e);
  }
  std::stable_partition(order.begin(), order.end(), [](const Entry* e) { return Lower(e->path) != "default.xex"; });
  for (const Entry* entry : order) {
    const Entry& e = *entry;
    const fs::path target = to / fs::path(std::u8string(reinterpret_cast<const char8_t*>(e.path.data()), e.path.size()));
    if (e.directory) {
      fs::create_directories(target, ec);
      continue;
    }
    fs::create_directories(target.parent_path(), ec);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) {
      error = "cannot write " + e.path;
      return false;
    }
    uint64_t left = e.size, at = e.offset;
    while (left) {
      const size_t n = size_t(std::min<uint64_t>(left, buffer.size()));
      if (!ReadAt(at, buffer.data(), n)) {
        error = "cannot read " + e.path + " from the image";
        return false;
      }
      out.write(buffer.data(), std::streamsize(n));
      if (!out) {
        error = "cannot write " + e.path + " (disk full?)";
        return false;
      }
      left -= n;
      at += n;
      done += n;
      if (progress && !progress(done, total)) {
        error = "stopped";
        return false;
      }
    }
  }
  if (progress) progress(total, total);
  return true;
}

std::string Sha256Hex(const void* data, size_t size) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
  std::string out;
  unsigned char digest[32];
  if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
    if (BCryptHashData(hash, static_cast<PUCHAR>(const_cast<void*>(data)), ULONG(size), 0) == 0 &&
        BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
      char hex[65];
      for (int i = 0; i < 32; ++i) std::snprintf(hex + 2 * i, 3, "%02x", digest[i]);
      out.assign(hex, 64);
    }
    BCryptDestroyHash(hash);
  }
  BCryptCloseAlgorithmProvider(alg, 0);
  return out;
}

}  // namespace coreoflight
