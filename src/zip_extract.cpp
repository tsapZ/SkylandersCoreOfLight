#include "zip_extract.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

#include "crc32.h"
#include "inflate.h"

namespace coreoflight {
namespace fs = std::filesystem;

namespace {

uint32_t Get16(const unsigned char* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }
uint32_t Get32(const unsigned char* p) { return Get16(p) | Get16(p + 2) << 16; }

// An entry's name as a path below the target: parts split at / or \, no drive, no leading slash, no empty, "." or
// ".." part (the same rule as portable.cpp's).
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

bool ReadAt(std::ifstream& in, uint64_t at, size_t size, std::vector<char>& out) {
  out.resize(size);
  in.clear();
  in.seekg(std::streamoff(at));
  return size == 0 || bool(in.read(out.data(), std::streamsize(size)));
}

}  // namespace

bool ExtractZip(const fs::path& zip, const fs::path& target, std::atomic<float>* progress, std::string& error) {
  std::ifstream in(zip, std::ios::binary);
  std::error_code ec;
  const uint64_t total = fs::file_size(zip, ec);
  if (!in || ec || total < 22) {
    error = "can't read " + zip.filename().string();
    return false;
  }
  // The end of the central directory, searched from the end (a comment of up to 64 KB may follow it).
  const size_t tail_size = size_t(std::min<uint64_t>(total, 22 + 0xFFFF));
  std::vector<char> tail;
  if (!ReadAt(in, total - tail_size, tail_size, tail)) {
    error = "can't read the end of " + zip.filename().string();
    return false;
  }
  const auto* t = reinterpret_cast<const unsigned char*>(tail.data());
  size_t end = std::string::npos;
  for (size_t i = tail_size - 22;; --i) {
    if (Get32(t + i) == 0x06054B50) {
      end = i;
      break;
    }
    if (i == 0) break;
  }
  if (end == std::string::npos) {
    error = "no ZIP directory in " + zip.filename().string();
    return false;
  }
  const uint32_t count = Get16(t + end + 10), dir_size = Get32(t + end + 12), dir_at = Get32(t + end + 16);
  if (count == 0xFFFF || dir_at == 0xFFFFFFFFu || uint64_t(dir_at) + dir_size > total) {
    error = "unsupported or broken ZIP directory (ZIP64?)";
    return false;
  }
  std::vector<char> dir;
  if (!ReadAt(in, dir_at, dir_size, dir)) {
    error = "can't read the ZIP directory";
    return false;
  }
  const auto* d = reinterpret_cast<const unsigned char*>(dir.data());
  size_t at = 0;
  uint64_t done = 0;
  std::vector<char> header, packed, data;
  for (uint32_t i = 0; i < count; ++i) {
    if (at + 46 > dir.size() || Get32(d + at) != 0x02014B50) {
      error = "broken ZIP directory entry";
      return false;
    }
    const uint32_t method = Get16(d + at + 10), crc = Get32(d + at + 16), size = Get32(d + at + 20),
                   usize = Get32(d + at + 24), name_len = Get16(d + at + 28), extra_len = Get16(d + at + 30),
                   comment_len = Get16(d + at + 32), offset = Get32(d + at + 42);
    if (at + 46 + name_len > dir.size()) {
      error = "broken ZIP directory entry";
      return false;
    }
    const std::string name(dir.data() + at + 46, name_len);
    at += 46 + name_len + extra_len + comment_len;
    if (name.empty() || name.back() == '/' || name.back() == '\\') continue;  // a folder
    fs::path relative;
    if (!SafeRelative(name, relative)) {
      error = "unsafe name in the ZIP: " + name;
      return false;
    }
    if (!ReadAt(in, offset, 30, header) || Get32(reinterpret_cast<const unsigned char*>(header.data())) != 0x04034B50) {
      error = "broken ZIP entry: " + name;
      return false;
    }
    const auto* h = reinterpret_cast<const unsigned char*>(header.data());
    const uint64_t data_at = uint64_t(offset) + 30 + Get16(h + 26) + Get16(h + 28);
    if (data_at + size > total || !ReadAt(in, data_at, size, packed)) {
      error = "broken ZIP entry: " + name;
      return false;
    }
    if (method == 0 && size == usize) {
      data.swap(packed);
    } else if (method != 8 || !InflateRaw(packed.data(), packed.size(), usize, data)) {
      error = "can't unpack " + name;
      return false;
    }
    if (Crc32(data.data(), data.size()) != crc) {
      error = "damaged file in the ZIP: " + name;
      return false;
    }
    const fs::path out = target / relative;
    fs::create_directories(out.parent_path(), ec);
    std::ofstream file(out, std::ios::binary | std::ios::trunc);
    if (!file || !file.write(data.data(), std::streamsize(data.size())) || !file.flush()) {
      error = "can't write " + relative.generic_string();
      return false;
    }
    done += size;
    if (progress) progress->store(float(double(done) / double(total)));
  }
  return true;
}

}  // namespace coreoflight
