// wua_unpack.cpp (#166): .wua archives written here with ZArchive's own writer, made-up titles and files only.
#include "wua_unpack.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "zarchive/zarchivewriter.h"

namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond); \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static void Write(const fs::path& wua, const std::vector<std::pair<std::string, std::string>>& files) {
  std::ofstream out(wua, std::ios::binary | std::ios::trunc);
  ZArchiveWriter writer([](int32_t, void*) {},
                        [](const void* data, size_t length, void* ctx) {
                          static_cast<std::ofstream*>(ctx)->write(static_cast<const char*>(data), std::streamsize(length));
                        },
                        &out);
  for (const auto& [path, content] : files) {
    const size_t slash = path.rfind('/');
    if (slash != std::string::npos) writer.MakeDir(path.substr(0, slash).c_str(), true);
    writer.StartNewFile(path.c_str());
    writer.AppendData(content.data(), content.size());
  }
  writer.Finalize();
}

static std::string Read(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int main() {
  const fs::path dir = fs::temp_directory_path() /
                       ("coreoflight_wua_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(dir);

  CHECK(coreoflight::IsWuaFile("games/SuperChargers.WUA"));
  CHECK(!coreoflight::IsWuaFile("games/SuperChargers.wud"));
  CHECK(!coreoflight::IsWuaFile("games/wua"));

  // A game without the marker first, the game with it, two updates of it, a DLC, the game's code.
  std::string big(300 * 1024, '\0');
  for (size_t i = 0; i < big.size(); ++i) big[i] = char(i * 7 + i / 4096);
  const fs::path full = dir / "full.wua";
  Write(full, {{"0005000010000000_v0/content/archives/Other.pak", "another game"},
               {"0005000010aabb00_v0/code/app.rpx", "code"},
               {"0005000010aabb00_v0/content/archives/Bowser.pak", "base bowser"},
               {"0005000010aabb00_v0/content/archives/Keep.pak", big},
               {"0005000010aabb00_v0/content/archives/sub/Inner.pak", "inner"},
               {"0005000e10aabb00_v16/content/archives/Bowser.pak", "update v16"},
               {"0005000E10AABB00_v32/content/archives/Bowser.pak", "update v32"},
               {"0005000c10aabb00_v0/content/archives/Dlc.pak", "dlc"}});
  std::atomic<float> progress{0.0f};
  std::atomic<uint64_t> bytes{0};
  std::string error;
  const fs::path out = dir / "out";
  CHECK(coreoflight::UnpackWuaArchives(full, out, "Bowser.pak", &progress, &bytes, error));
  if (!error.empty()) std::fprintf(stderr, "error: %s\n", error.c_str());
  const fs::path archives = out / "content" / "archives";
  CHECK(Read(archives / "Bowser.pak") == "update v32");
  CHECK(Read(archives / "Keep.pak") == big);
  CHECK(Read(archives / "sub" / "Inner.pak") == "inner");
  CHECK(!fs::exists(archives / "Other.pak"));
  CHECK(!fs::exists(archives / "Dlc.pak"));
  CHECK(!fs::exists(out / "code"));
  CHECK(progress.load() == 1.0f);
  CHECK(bytes.load() == std::string("base bowser").size() + big.size() + 5 + std::string("update v32").size());

  // Without the marker anywhere: the first game with content/archives all the same.
  const fs::path out2 = dir / "out2";
  error.clear();
  CHECK(coreoflight::UnpackWuaArchives(full, out2, "Missing.pak", nullptr, nullptr, error));
  CHECK(Read(out2 / "content" / "archives" / "Other.pak") == "another game");

  // Only an update: no game.
  const fs::path update_only = dir / "update.wua";
  Write(update_only, {{"0005000e10aabb00_v32/content/archives/Bowser.pak", "update"}});
  error.clear();
  CHECK(!coreoflight::UnpackWuaArchives(update_only, dir / "out3", "Bowser.pak", nullptr, nullptr, error));
  CHECK(error.find("no Wii U game") != std::string::npos);

  // Not an archive at all.
  const fs::path text = dir / "text.wua";
  std::ofstream(text) << "not an archive, just some text that is long enough to have a footer's size or more......";
  error.clear();
  CHECK(!coreoflight::UnpackWuaArchives(text, dir / "out4", "Bowser.pak", nullptr, nullptr, error));
  CHECK(!error.empty());

  std::error_code ec;
  fs::remove_all(dir, ec);
  std::printf("%s\n", failures ? "wua_unpack: FAILED" : "wua_unpack: ok");
  return failures ? 1 : 0;
}
