// The disc check's own tests (disc_id.cpp, xiso.cpp): a synthetic XEX2 header and a synthetic
// XDVDFS image, so nothing here needs a disc or any game data.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "games.h"
#include "xiso.h"

namespace fs = std::filesystem;
using coreoflight::Game;
using coreoflight::IdentifyXex;
using coreoflight::IsGameDisc;
using coreoflight::MatchXex;
using coreoflight::ReadDiscXex;
using coreoflight::XexId;
using coreoflight::XexMatch;
using coreoflight::XisoImage;

static int g_failures = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

static void PutBe32(std::string& s, size_t at, uint32_t v) {
  s[at] = char(v >> 24);
  s[at + 1] = char(v >> 16);
  s[at + 2] = char(v >> 8);
  s[at + 3] = char(v);
}

// A small XEX2 header: the execution info and the security info with three page descriptors,
// read-only data (1 page), code (2 pages), data (1 page), 64 KB pages loaded at 0x82000000.
// `media` and `rdata_digest` stand for what differs between pressings, `code_digest` for the code.
static std::string FakeXex(uint32_t media, char rdata_digest, char code_digest) {
  std::string s(0x400, '\0');
  s.replace(0, 4, "XEX2");
  PutBe32(s, 0x08, 0x400);  // header size
  PutBe32(s, 0x10, 0x100);  // security info
  PutBe32(s, 0x14, 1);      // one optional header
  PutBe32(s, 0x18, 0x00040006);
  PutBe32(s, 0x1C, 0x40);   // the execution info
  PutBe32(s, 0x40, media);
  PutBe32(s, 0x44, 0x14);        // version 0.0.0.20
  PutBe32(s, 0x4C, 0x41560911);  // title
  s[0x52] = 1;                   // disc 1/1
  s[0x53] = 1;
  PutBe32(s, 0x100 + 0x110, 0x82000000);
  PutBe32(s, 0x100 + 0x178, 0x00FF0000);
  PutBe32(s, 0x100 + 0x180, 3);
  const size_t table = 0x100 + 0x184;
  PutBe32(s, table, (1 << 4) | 3);
  s.replace(table + 4, 20, 20, rdata_digest);
  PutBe32(s, table + 24, (2 << 4) | 1);
  s.replace(table + 28, 20, 20, code_digest);
  PutBe32(s, table + 48, (1 << 4) | 2);
  s.replace(table + 52, 20, 20, 'd');
  return s + "the image";
}

// Sets bytes at `offset` in `path`, making the file (and any gap) sparse: the full Xbox 360 image
// puts the game partition at 0xFD90000, and the test needn't write 260 MB of zeros.
static bool WriteAt(const fs::path& path, uint64_t offset, const void* data, size_t size) {
  // fstream, not ofstream: in|out on an existing image must not truncate it.
  std::fstream out(path, std::ios::binary | std::ios::in | std::ios::out);
  if (!out) out.open(path, std::ios::binary | std::ios::out);
  if (!out) return false;
  out.seekp(static_cast<std::streamoff>(offset));
  out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
  return bool(out);
}

static bool WriteAt(const fs::path& path, uint64_t offset, const std::string& data) {
  return WriteAt(path, offset, data.data(), data.size());
}

static void PutLe32(std::string& s, size_t at, uint32_t v) {
  s[at] = char(v);
  s[at + 1] = char(v >> 8);
  s[at + 2] = char(v >> 16);
  s[at + 3] = char(v >> 24);
}

// A directory table entry at `at` (4-byte units from the table's start): no subdirectories, the
// file/directory at `sector`, `size` bytes, `attributes`, `name`.
static void PutEntry(std::string& table, size_t at, uint32_t sector, uint32_t size, uint8_t attributes,
                     const char* name) {
  const size_t name_length = std::char_traits<char>::length(name);
  table[at + 0] = 0;  // left
  table[at + 1] = 0;
  table[at + 2] = 0;  // right
  table[at + 3] = 0;
  PutLe32(table, at + 4, sector);
  PutLe32(table, at + 8, size);
  table[at + 12] = char(attributes);
  table[at + 13] = char(name_length);
  table.replace(at + 14, name_length, name);
}

// A minimal XDVDFS image: the volume descriptor at sector 32, the root directory at 33, the
// files from 100 on. `partition` is where the game partition starts (0xFD90000 in a full dump, 0
// in a trimmed image).
static fs::path WriteXiso(const fs::path& path, uint64_t partition, const std::string& xex) {
  static constexpr uint64_t kSector = 2048;
  const std::string volume(kSector, '\0');
  std::string root(kSector, '\0');
  std::string update(kSector, '\0');
  const std::string update_bytes = "sys!";
  PutEntry(root, 0, 100, uint32_t(xex.size()), 0, "default.xex");
  root[2] = 20;  // the root's second entry, in 4-byte units from the table's start
  root[3] = 0;
  PutEntry(root, 80, 101, kSector, 0x10, "$SystemUpdate");
  PutEntry(update, 0, 102, uint32_t(update_bytes.size()), 0, "x.bin");
  std::string descriptor = volume;
  descriptor.replace(0, 20, "MICROSOFT*XBOX*MEDIA");
  PutLe32(descriptor, 0x14, 33);
  PutLe32(descriptor, 0x18, kSector);
  bool ok = WriteAt(path, partition + 32 * kSector, descriptor) &&
            WriteAt(path, partition + 33 * kSector, root) &&
            WriteAt(path, partition + 100 * kSector, xex) &&
            WriteAt(path, partition + 101 * kSector, update) &&
            WriteAt(path, partition + 102 * kSector, update_bytes);
  return ok ? path : fs::path();
}

int main() {
  std::error_code ec;
  const fs::path dir = fs::temp_directory_path() / "coreoflight_disc_id_test";
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);

  // ---- The XEX2 header, its SHA-256 and its code pin ----
  const std::string a = FakeXex(0x7A74A451, 'a', 'c');
  const XexId id = IdentifyXex(a.data(), a.size());
  CHECK(id.has_header);
  CHECK(id.title_id == 0x41560911);
  CHECK(id.version == 0x14);
  CHECK(id.region == 0x00FF0000);
  CHECK(id.header ==
        "title 41560911, version 0.0.0.20, Europe and Australia (00FF0000), media 7A74A451, disc 1/1");
  CHECK(id.sha256.size() == 64);
  CHECK(id.code_pin.rfind("82010000:", 0) == 0 && id.code_pin.size() == 73);
  CHECK(id.sha256 != coreoflight::Sha256Hex(a.data(), a.size() - 10));  // the whole file, not its header

  // Another pressing: other media ID and read-only data, same code -> the same pin.
  const std::string b_bytes = FakeXex(0x12345678, 'b', 'c');
  const XexId b = IdentifyXex(b_bytes.data(), b_bytes.size());
  CHECK(b.code_pin == id.code_pin);
  CHECK(b.sha256 != id.sha256);
  // Other code -> another pin.
  const std::string c = FakeXex(0x7A74A451, 'a', 'x');
  CHECK(IdentifyXex(c.data(), c.size()).code_pin != id.code_pin);

  // A file that is no XEX: still hashed, but no header and no pin.
  const std::string plain = "not an Xbox executable";
  const XexId none = IdentifyXex(plain.data(), plain.size());
  CHECK(!none.has_header);
  CHECK(none.title_id == 0);
  CHECK(none.code_pin.empty());
  CHECK(none.sha256 == coreoflight::Sha256Hex(plain.data(), plain.size()));
  // Cut short: the header is not trusted either.
  CHECK(!IdentifyXex(a.data(), 0x200).has_header);

  // ---- Matching a list: exact hash, same code, neither ----
  const std::string list = id.sha256 + "," + id.code_pin;
  CHECK(MatchXex(list, id) == XexMatch::kExact);
  CHECK(MatchXex(list, b) == XexMatch::kSameCode);
  CHECK(MatchXex(list, IdentifyXex(c.data(), c.size())) == XexMatch::kNone);
  CHECK(MatchXex(id.sha256, b) == XexMatch::kNone);
  CHECK(MatchXex(id.code_pin, b) == XexMatch::kSameCode);
  CHECK(!coreoflight::XexInList(list, none));
  CHECK(coreoflight::XexInList(list, b));

  // The game's own list: a fake Game with this disc's pin list.
  const Game game{"test", "Test", "Test", 2011, 0x41560911, "test.exe", "TestRecomp", "Test Recompiled",
                  list.c_str(), 0, 0, "{00000000-0000-0000-0000-000000000000}", ""};
  CHECK(IsGameDisc(game, id));
  CHECK(IsGameDisc(game, b));
  CHECK(!IsGameDisc(game, IdentifyXex(c.data(), c.size())));

  // ---- XDVDFS: full dump and trimmed image ----
  for (const uint64_t partition : {uint64_t(0xFD90000), uint64_t(0)}) {
    const fs::path path = WriteXiso(dir / (partition ? "full.iso" : "trimmed.iso"), partition, a);
    CHECK(!path.empty());
    const auto image = XisoImage::Open(path);
    CHECK(image != nullptr);
    if (!image) continue;
    const XisoImage::Entry* entry = image->Find("DEFAULT.XEX");  // case-insensitive
    CHECK(entry != nullptr);
    if (entry) {
      std::vector<uint8_t> read;
      CHECK(image->Read(*entry, read));
      CHECK(std::string(read.begin(), read.end()) == a);
    }
    CHECK(image->Find("$systemupdate/x.bin") != nullptr);
    CHECK(image->total_size("$SystemUpdate") == a.size());  // the update's 4 bytes left out
    CHECK(image->total_size() == a.size() + 4);
    CHECK(XisoImage::InFolder("text/eng/global.lxb", "text"));
    CHECK(!XisoImage::InFolder("textual/eng/global.lxb", "text"));
  }

  // An unpacked disc: the same bytes from a folder with default.xex.
  {
    const fs::path folder = dir / "unpacked";
    fs::create_directories(folder, ec);
    std::ofstream(folder / "default.xex", std::ios::binary) << a;
    std::vector<uint8_t> read;
    uint64_t bytes = 0;
    CHECK(ReadDiscXex(folder, read, &bytes));
    CHECK(std::string(read.begin(), read.end()) == a);
    CHECK(bytes == a.size());  // no $SystemUpdate here
    CHECK(IdentifyXex(read.data(), read.size()).code_pin == id.code_pin);
  }

  // The case-aware disc root: no file, another case of the name, and the plain one.
  {
    const fs::path folder = dir / "case_disc";
    fs::create_directories(folder, ec);
    CHECK(coreoflight::FindDiscXex(folder).empty());
    std::vector<uint8_t> none_read;
    CHECK(!ReadDiscXex(folder, none_read, nullptr));  // a folder without default.xex
    std::ofstream(folder / "DEFAULT.XEX", std::ios::binary) << a;
    const fs::path found = coreoflight::FindDiscXex(folder);
    CHECK(!found.empty());
    CHECK(found.filename().u8string() == std::u8string(u8"DEFAULT.XEX"));
    std::vector<uint8_t> read;
    CHECK(ReadDiscXex(folder, read, nullptr));
    CHECK(std::string(read.begin(), read.end()) == a);
  }

  // A path that isn't there, or has no name: no disc, no hang, and the size left alone.
  {
    std::vector<uint8_t> read;
    uint64_t bytes = 123;
    CHECK(!ReadDiscXex(dir / "missing.iso", read, &bytes));
    CHECK(bytes == 123);
    CHECK(!ReadDiscXex(fs::path(), read, nullptr));
    CHECK(coreoflight::FindDiscXex(dir / "missing").empty());
  }

  // One rule for naming a disc: an exact pressing wins over a same-code one earlier in the list.
  {
    const Game same{"same", "Same", "Same", 2011, 0x41560911, "same.exe", "SameRecomp", "Same", id.code_pin.c_str()};
    const Game exact{"exact", "Exact", "Exact", 2011, 0x41560911, "exact.exe", "ExactRecomp", "Exact", id.sha256.c_str()};
    const std::vector<Game> games = {same, exact};
    CHECK(coreoflight::GameForDiscIn(games, id) == 1);  // exact, though same-code is first
    CHECK(coreoflight::GameForDiscIn({same}, id) == 0);  // only a same-code pressing
    CHECK(coreoflight::GameForDiscIn(games, IdentifyXex(c.data(), c.size())) == -1);
    // The title route names another build of a known game (0 is no title).
    CHECK(coreoflight::GameForTitleIdIn(games, 0x41560911) == 0);
    CHECK(coreoflight::GameForTitleIdIn(games, 0) == -1);
    CHECK(coreoflight::GameForTitleIdIn(games, 0x12345678) == -1);
  }

  // A file that is no disc image.
  {
    const fs::path garbage = dir / "garbage.iso";
    std::ofstream(garbage, std::ios::binary) << "not a disc";
    CHECK(XisoImage::Open(garbage) == nullptr);
    std::vector<uint8_t> read;
    CHECK(!ReadDiscXex(garbage, read, nullptr));
  }

  if (g_failures == 0) fs::remove_all(dir, ec);  // kept on failure, to look at
  if (g_failures) {
    std::fprintf(stderr, "%d check(s) failed (files left in %s)\n", g_failures, dir.string().c_str());
    return 1;
  }
  std::printf("disc_id: all checks passed\n");
  return 0;
}
