#include "nxe_art.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include "imgui.h"
#include "textures.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;

uint32_t U32BE(const std::vector<uint8_t>& d, size_t o) {
  return uint32_t(d[o]) << 24 | uint32_t(d[o + 1]) << 16 | uint32_t(d[o + 2]) << 8 | d[o + 3];
}

uint32_t U24LE(const std::vector<uint8_t>& d, size_t o) { return d[o] | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16; }

// One file of an STFS package from a disc, whose blocks follow each other (a package written once
// and never changed), or nothing. The layout: the header's size at 0x340, the volume descriptor at
// 0x379 (block separation, the file table's block count and first block), 4 KB blocks after the
// header, a hash table every 170 blocks (and every 28,900), one per level when bit 0 of the block
// separation is set (a read-only package), else two; file table entries of 64 bytes (the name and
// its length at +0x28, the first block at +0x2F, the size at +0x34).
std::optional<std::vector<uint8_t>> ReadStfsFile(const std::vector<uint8_t>& d, const std::string& name) {
  if (d.size() < 0x1000) return std::nullopt;
  const std::string magic(d.begin(), d.begin() + 4);
  if (magic != "PIRS" && magic != "LIVE" && magic != "CON ") return std::nullopt;
  const uint64_t first_block = (uint64_t(U32BE(d, 0x340)) + 0xFFF) & ~uint64_t(0xFFF);
  const uint8_t block_sep = d[0x37B];
  const uint32_t table_blocks = d[0x37C] | uint32_t(d[0x37D]) << 8;
  const uint32_t table_first = U24LE(d, 0x37E);
  const uint64_t tables = (block_sep & 1) ? 1 : 2;
  auto offset_of = [&](uint64_t block) {
    uint64_t base = (block / 170 + 1) * tables;
    if (block >= 170) {
      base += (block / 28900 + 1) * tables;
      if (block >= 28900) base += tables;
    }
    return first_block + (block + base) * 0x1000;
  };
  for (uint32_t b = 0; b < table_blocks; ++b) {
    const uint64_t table = offset_of(table_first + b);
    for (uint32_t i = 0; i < 64; ++i) {
      const uint64_t e = table + uint64_t(i) * 0x40;
      if (e + 0x40 > d.size()) break;
      const uint8_t len = d[e + 0x28] & 0x3F;
      if (!len || std::string(d.begin() + e, d.begin() + e + len) != name) continue;
      const uint32_t start = U24LE(d, e + 0x2F);
      const uint32_t size = U32BE(d, e + 0x34);
      std::vector<uint8_t> out;
      out.reserve(size);
      for (uint64_t block = start; out.size() < size; ++block) {
        const uint64_t at = offset_of(block);
        const size_t n = std::min<size_t>(0x1000, size - out.size());
        if (at + n > d.size()) return std::nullopt;
        out.insert(out.end(), d.begin() + at, d.begin() + at + n);
      }
      return out;
    }
  }
  return std::nullopt;
}

}  // namespace

fs::path NxeArt(const fs::path& rom_dir, const fs::path& cache_dir, const std::string& name) {
  std::error_code ec;
  const fs::path package = rom_dir / "nxeart";
  const fs::path cached = cache_dir / name;
  if (!fs::is_regular_file(package, ec)) return {};
  if (fs::is_regular_file(cached, ec) && fs::last_write_time(cached, ec) >= fs::last_write_time(package, ec)) {
    return cached;
  }
  std::ifstream in(package, std::ios::binary);
  const std::vector<uint8_t> data{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  const auto file = ReadStfsFile(data, name);
  if (!file) return {};
  fs::create_directories(cache_dir, ec);
  const fs::path temp = fs::path(cached) += ".new";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(file->data()), std::streamsize(file->size()));
    if (!out) return {};
  }
  fs::rename(temp, cached, ec);
  return ec ? fs::path() : cached;
}

namespace {
std::vector<fs::path> g_backgrounds;
}  // namespace

void DrawBackdrop(ImDrawList* dl, float width, float height, const fs::path& picture) {
  const ImTextureID texture = picture.empty() ? ImTextureID{} : LoadTexture(picture);
  if (texture) {
    const float scale = std::max(width / 1920.0f, height / 1080.0f);
    const float bw = 1920.0f * scale, bh = 1080.0f * scale;
    dl->AddImage(texture, {(width - bw) * 0.5f, (height - bh) * 0.5f}, {(width + bw) * 0.5f, (height + bh) * 0.5f});
    dl->AddRectFilledMultiColor({0, 0}, {width, height}, IM_COL32(10, 22, 60, 170), IM_COL32(10, 22, 60, 170),
                                IM_COL32(3, 7, 22, 225), IM_COL32(3, 7, 22, 225));
  } else {
    dl->AddRectFilledMultiColor({0, 0}, {width, height}, IM_COL32(10, 22, 60, 255), IM_COL32(10, 22, 60, 255),
                                IM_COL32(3, 7, 22, 255), IM_COL32(3, 7, 22, 255));
  }
}

void SetGameBackgrounds(std::vector<fs::path> pictures) { g_backgrounds = std::move(pictures); }

fs::path GameBackground(size_t game) { return game < g_backgrounds.size() ? g_backgrounds[game] : fs::path(); }

}  // namespace coreoflight
