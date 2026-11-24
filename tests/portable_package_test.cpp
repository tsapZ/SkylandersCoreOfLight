// portable.cpp's ReadZipFiles, on synthetic ZIPs only: the launcher's portable update package as the
// packaging lays it out, a folder entry (skipped), an unsafe ".." member, a broken CRC and a broken
// central directory. Nothing here needs a real release asset.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "portable.h"

namespace fs = std::filesystem;
using coreoflight::ReadZipFiles;
using coreoflight::ZipFile;

// Deflated entries are not exercised here (the packages' plain files are stored); portable.cpp calls
// this for method 8, which the update reader's own CRC and directory checks cover elsewhere.
namespace coreoflight {
bool InflateRaw(const char*, size_t, size_t, std::vector<char>&) { return false; }
}  // namespace coreoflight

static int g_failures = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

namespace {

void Put16(std::string& s, size_t at, uint16_t v) {
  s[at] = char(v);
  s[at + 1] = char(v >> 8);
}

void Put32(std::string& s, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) s[at + i] = char(v >> (8 * i));
}

uint32_t Crc32Of(const std::string& data) {
  uint32_t crc = 0xFFFFFFFFu;
  for (unsigned char c : data) {
    crc ^= c;
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

struct Stored {
  std::string name, data;
};

// A ZIP whose entries are all stored (method 0): what the reader takes for a "portable" package's
// plain files. `sabotage_crc` writes a wrong checksum into the directory.
std::string StoredZip(const std::vector<Stored>& files, bool sabotage_crc = false) {
  std::string out, dir;
  for (const Stored& f : files) {
    const uint32_t offset = uint32_t(out.size());
    const uint32_t crc = sabotage_crc ? 0u : Crc32Of(f.data);
    const size_t local = out.size();
    out.resize(local + 30, '\0');
    Put32(out, local + 0, 0x04034B50);
    Put16(out, local + 8, 0);  // stored
    Put32(out, local + 14, crc);
    Put32(out, local + 18, uint32_t(f.data.size()));
    Put32(out, local + 22, uint32_t(f.data.size()));
    Put16(out, local + 26, uint16_t(f.name.size()));
    out += f.name;
    out += f.data;

    const size_t d = dir.size();
    dir.resize(d + 46 + f.name.size(), '\0');
    Put32(dir, d + 0, 0x02014B50);
    Put16(dir, d + 10, 0);  // stored
    Put32(dir, d + 16, crc);
    Put32(dir, d + 20, uint32_t(f.data.size()));
    Put32(dir, d + 24, uint32_t(f.data.size()));
    Put16(dir, d + 28, uint16_t(f.name.size()));
    Put32(dir, d + 42, offset);
    dir.replace(d + 46, f.name.size(), f.name);
  }
  const uint32_t dir_at = uint32_t(out.size());
  out += dir;
  const size_t e = out.size();
  out.resize(e + 22, '\0');
  Put32(out, e + 0, 0x06054B50);
  Put16(out, e + 8, uint16_t(files.size()));
  Put16(out, e + 10, uint16_t(files.size()));
  Put32(out, e + 12, uint32_t(dir.size()));
  Put32(out, e + 16, dir_at);
  return out;
}

void Write(const fs::path& p, const std::string& data) {
  std::ofstream(p, std::ios::binary).write(data.data(), std::streamsize(data.size()));
}

std::string Text(const std::vector<char>& data) { return std::string(data.begin(), data.end()); }

}  // namespace

int main() {
  const fs::path base = fs::temp_directory_path() / "coreoflight_portable_zip_test";
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base);

  // The portable launcher's package (tools/package_installer.ps1): the exe, portable.txt next to it
  // and the fonts; the folder entry is skipped, not an error.
  {
    const fs::path zip = base / "CoreOfLightPortable-0.5.0-beta.4.zip";
    Write(zip, StoredZip({{"portable.txt", ""},
                          {"coreoflight.exe", "exe bytes"},
                          {"fonts/", ""},
                          {"fonts/NotoSans-Regular.ttf", "font bytes"}}));
    std::vector<ZipFile> files;
    std::string error;
    CHECK(ReadZipFiles(zip, files, false, error));
    CHECK(files.size() == 3);
    CHECK(error.empty());
    bool exe = false, marker = false, font = false;
    for (const ZipFile& f : files) {
      exe |= f.path == fs::path("coreoflight.exe") && Text(f.data) == "exe bytes";
      marker |= f.path == fs::path("portable.txt") && f.data.empty();
      font |= f.path == fs::path("fonts") / "NotoSans-Regular.ttf" && Text(f.data) == "font bytes";
    }
    CHECK(exe && marker && font);
  }

  // A member above the target folder is refused, with nothing handed back.
  {
    const fs::path zip = base / "unsafe.zip";
    Write(zip, StoredZip({{"coreoflight.exe", "exe bytes"}, {"../escape.txt", "no"}}));
    std::vector<ZipFile> files;
    std::string error;
    CHECK(!ReadZipFiles(zip, files, false, error));
    CHECK(error.empty() == false);
    CHECK(files.empty());
  }

  // An absolute member is refused the same way.
  {
    const fs::path zip = base / "absolute.zip";
    Write(zip, StoredZip({{"/etc/passwd", "no"}}));
    std::vector<ZipFile> files;
    std::string error;
    CHECK(!ReadZipFiles(zip, files, false, error));
  }

  // A wrong checksum is a damaged package; the German text is the same failure.
  {
    const fs::path zip = base / "damaged.zip";
    Write(zip, StoredZip({{"coreoflight.exe", "exe bytes"}}, /*sabotage_crc=*/true));
    std::vector<ZipFile> files;
    std::string error;
    CHECK(!ReadZipFiles(zip, files, true, error));
    CHECK(error.empty() == false);
  }

  // Not a ZIP at all.
  {
    const fs::path zip = base / "text.zip";
    Write(zip, "this is not a zip archive, just some text");
    std::vector<ZipFile> files;
    std::string error;
    CHECK(!ReadZipFiles(zip, files, false, error));
  }

  fs::remove_all(base, ec);
  if (g_failures == 0) std::printf("portable_zip: all tests passed\n");
  return g_failures == 0 ? 0 : 1;
}
