// pe_imports.cpp: see pe_imports.h. Reads only the headers, the section table and the import directory
// (PE/COFF specification: optional header data directory 1, 20-byte import descriptors ending in a zero one).

#include "pe_imports.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // the test target builds without the launcher's NOMINMAX; std::max below
#endif
#include <windows.h>
#endif

namespace coreoflight {
namespace {

struct Section {
  uint32_t va = 0, size = 0, raw = 0;
};

uint32_t Le32(const unsigned char* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

uint16_t Le16(const unsigned char* p) { return uint16_t(p[0] | p[1] << 8); }

bool ReadAt(std::ifstream& in, uint64_t offset, void* out, size_t size) {
  in.clear();
  in.seekg(std::streamoff(offset));
  in.read(static_cast<char*>(out), std::streamsize(size));
  return size_t(in.gcount()) == size;
}

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return s;
}

// A name as the loader takes it: printable ASCII without a path in it. A damaged file gives garbage, which could
// throw in std::filesystem's conversion (a UTF-8 code page) or point outside the game's folder.
bool PlainName(const std::string& name) {
  if (name.empty()) return false;
  for (const unsigned char c : name) {
    if (c < 0x20 || c > 0x7E || c == '/' || c == '\\' || c == ':') return false;
  }
  return true;
}

}  // namespace

std::vector<std::string> PeImports(const std::filesystem::path& file) {
  std::vector<std::string> names;
  std::ifstream in(file, std::ios::binary);
  if (!in) return names;
  unsigned char dos[64];
  if (!ReadAt(in, 0, dos, sizeof(dos)) || dos[0] != 'M' || dos[1] != 'Z') return names;
  const uint32_t pe = Le32(dos + 0x3C);
  unsigned char head[24];
  if (!ReadAt(in, pe, head, sizeof(head)) || head[0] != 'P' || head[1] != 'E' || head[2] != 0 || head[3] != 0) {
    return names;
  }
  const uint16_t section_count = Le16(head + 6);
  const uint16_t optional_size = Le16(head + 20);
  std::vector<unsigned char> optional(optional_size);
  if (optional_size < 2 || !ReadAt(in, pe + 24, optional.data(), optional.size())) return names;
  // The data directories start at 96 (PE32) or 112 (PE32+), after their count; the import directory is entry 1.
  const uint16_t magic = Le16(optional.data());
  const size_t dirs = magic == 0x20B ? 112 : magic == 0x10B ? 96 : 0;
  if (dirs == 0 || optional.size() < dirs + 16 || Le32(optional.data() + dirs - 4) < 2) return names;
  const uint32_t import_rva = Le32(optional.data() + dirs + 8);
  if (import_rva == 0) return names;

  std::vector<Section> sections;
  for (uint16_t i = 0; i < section_count && i < 96; ++i) {
    unsigned char s[40];
    if (!ReadAt(in, uint64_t(pe) + 24 + optional_size + 40ull * i, s, sizeof(s))) return names;
    sections.push_back({Le32(s + 12), std::max(Le32(s + 8), Le32(s + 16)), Le32(s + 20)});
  }
  auto offset_of = [&](uint32_t rva, uint64_t& offset) {
    for (const Section& s : sections) {
      if (rva >= s.va && rva - s.va < s.size) {
        offset = uint64_t(s.raw) + (rva - s.va);
        return true;
      }
    }
    return false;
  };
  uint64_t at = 0;
  if (!offset_of(import_rva, at)) return names;
  for (int i = 0; i < 1024; ++i) {
    unsigned char d[20];
    if (!ReadAt(in, at + 20ull * i, d, sizeof(d))) break;
    const uint32_t name_rva = Le32(d + 12);
    if (name_rva == 0 && Le32(d + 16) == 0) break;  // the closing zero descriptor
    uint64_t name_at = 0;
    if (!offset_of(name_rva, name_at)) continue;
    char buf[260] = {};
    in.clear();
    in.seekg(std::streamoff(name_at));
    in.read(buf, sizeof(buf) - 1);
    const std::string name(buf);  // up to its NUL
    if (PlainName(name)) names.push_back(name);
  }
  return names;
}

std::vector<std::string> MissingDlls(const std::filesystem::path& exe, const std::filesystem::path& system_dir) {
  namespace fs = std::filesystem;
  const fs::path folder = exe.parent_path();
  std::vector<std::string> missing;
  std::set<std::string> seen;
  std::vector<fs::path> todo{exe};
  std::error_code ec;
  while (!todo.empty() && seen.size() < 512) {
    const fs::path file = todo.back();
    todo.pop_back();
    for (const std::string& name : PeImports(file)) {
      const std::string key = Lower(name);
      if (!seen.insert(key).second) continue;
      if (key.rfind("api-ms-", 0) == 0 || key.rfind("ext-ms-", 0) == 0) continue;
      if (fs::is_regular_file(folder / name, ec)) {
        todo.push_back(folder / name);  // the game's own DLL: its imports count too
      } else if (system_dir.empty() || !fs::is_regular_file(system_dir / name, ec)) {
        missing.push_back(name);
      }
    }
  }
  return missing;
}

std::filesystem::path SystemDllDir() {
#ifdef _WIN32
  wchar_t dir[MAX_PATH];
  const UINT n = GetSystemDirectoryW(dir, MAX_PATH);
  if (n > 0 && n < MAX_PATH) return std::filesystem::path(std::wstring(dir, n));
#endif
  return {};
}

}  // namespace coreoflight
