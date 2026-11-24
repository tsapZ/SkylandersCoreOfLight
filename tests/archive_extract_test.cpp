// Verify that the staged extractor accepts the layout shipped by the Linux packager.
#include <sys/stat.h>
#include <unistd.h>

#include <archive.h>
#include <archive_entry.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "platform.h"

namespace fs = std::filesystem;
using coreoflight::ExtractArchive;
using coreoflight::PathFromUtf8;

static int g_failures = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

namespace {

struct Member {
  std::string path;
  std::string data;
  std::string link;  // symlink target or hardlink source; empty for a plain file or folder
  bool dir = false;
  bool hardlink = false;
  int perm = 0644;
};

bool WriteTar(const fs::path& file, const std::vector<Member>& members) {
  struct archive* a = archive_write_new();
  if (!a) return false;
  archive_write_set_format_pax(a);
  if (archive_write_open_filename(a, file.c_str()) != ARCHIVE_OK) {
    archive_write_free(a);
    return false;
  }
  bool ok = true;
  for (const Member& m : members) {
    struct archive_entry* e = archive_entry_new();
    archive_entry_set_pathname(e, m.path.c_str());
    archive_entry_set_perm(e, m.perm);
    if (m.dir) {
      archive_entry_set_filetype(e, AE_IFDIR);
    } else if (!m.link.empty() && !m.hardlink) {
      archive_entry_set_filetype(e, AE_IFLNK);
      archive_entry_set_symlink(e, m.link.c_str());
    } else {
      archive_entry_set_filetype(e, AE_IFREG);
      archive_entry_set_size(e, la_int64_t(m.data.size()));
      if (m.hardlink) archive_entry_set_hardlink(e, m.link.c_str());
    }
    if (archive_write_header(a, e) != ARCHIVE_OK) ok = false;
    if (ok && !m.dir && m.link.empty() && !m.data.empty()) {
      if (archive_write_data(a, m.data.data(), m.data.size()) < 0) ok = false;
    }
    if (ok && !m.dir && m.hardlink) archive_entry_set_hardlink(e, nullptr);
    archive_entry_free(e);
  }
  if (archive_write_close(a) != ARCHIVE_OK) ok = false;
  archive_write_free(a);
  return ok;
}

std::string Read(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), {});
}

int Mode(const fs::path& p) {
  struct stat st {};
  if (::lstat(p.c_str(), &st) != 0) return -1;
  return int(st.st_mode & 0777);
}

bool IsSymlink(const fs::path& p) {
  std::error_code ec;
  return fs::is_symlink(fs::symlink_status(p, ec));
}

// The update's own mapper: every member goes below `root` at its relative name.
std::function<fs::path(const std::string&)> Below(const fs::path& root) {
  return [root](const std::string& rel) { return root / PathFromUtf8(rel); };
}

}  // namespace

int main() {
  const fs::path base = fs::temp_directory_path() / "coreoflight_archive_extract_test";
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base);

  // A package as tools/package_linux.sh lays it out: the exe, fonts, SDL3 and an internal alias.
  {
    const fs::path root = base / "safe_root", tar = base / "safe.tar";
    fs::create_directories(root);
    const std::vector<Member> members = {
        {"coreoflight-linux-amd64-release", "", "", true, false, 0755},
        {"coreoflight-linux-amd64-release/coreoflight", "#!/bin/sh\n", "", false, false, 0755},
        {"coreoflight-linux-amd64-release/fonts", "", "", true, false, 0755},
        {"coreoflight-linux-amd64-release/fonts/NotoSans-Regular.ttf", "font bytes", "", false, false, 0644},
        {"coreoflight-linux-amd64-release/libSDL3.so.0", "library bytes", "", false, false, 0644},
        {"coreoflight-linux-amd64-release/libSDL3.so.0.1", "", "libSDL3.so.0", false, false, 0777},
    };
    CHECK(WriteTar(tar, members));
    std::string error;
    CHECK(ExtractArchive(tar, root, Below(root), error));
    CHECK(error.empty());
    CHECK(Read(root / "coreoflight-linux-amd64-release/coreoflight") == "#!/bin/sh\n");
    CHECK(Read(root / "coreoflight-linux-amd64-release/fonts/NotoSans-Regular.ttf") == "font bytes");
    CHECK(IsSymlink(root / "coreoflight-linux-amd64-release/libSDL3.so.0.1"));
    std::error_code link_ec;
    CHECK(fs::read_symlink(root / "coreoflight-linux-amd64-release/libSDL3.so.0.1", link_ec) ==
          fs::path("libSDL3.so.0"));
    // Private by default, except what the package marks executable.
    CHECK(Mode(root / "coreoflight-linux-amd64-release/fonts") == 0700);
    CHECK(Mode(root / "coreoflight-linux-amd64-release/fonts/NotoSans-Regular.ttf") == 0600);
    CHECK(Mode(root / "coreoflight-linux-amd64-release/coreoflight") == 0755);
  }

  fs::remove_all(base, ec);
  if (g_failures == 0) std::printf("archive_extract: all tests passed\n");
  return g_failures == 0 ? 0 : 1;
}
