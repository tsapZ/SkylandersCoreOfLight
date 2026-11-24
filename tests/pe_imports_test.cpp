// pe_imports.cpp's tests: tiny synthetic PE32+ images (headers, one section, an import directory) in a
// scratch folder, so nothing here needs Windows or a real game.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "pe_imports.h"

namespace fs = std::filesystem;
using coreoflight::MissingDlls;
using coreoflight::PeImports;

static int g_failures = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

static void Put16(std::string& s, size_t at, uint16_t v) {
  s[at] = char(v);
  s[at + 1] = char(v >> 8);
}

static void Put32(std::string& s, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) s[at + i] = char(v >> (8 * i));
}

// MZ, "PE\0\0" at 0x40, a PE32+ optional header with 16 data directories, one section (RVA 0x1000, file
// 0x200, 0x200 bytes) holding the import descriptors and, from RVA 0x1100, the names.
static std::string Image(const std::vector<std::string>& imports) {
  std::string s(0x400, '\0');
  s[0] = 'M';
  s[1] = 'Z';
  Put32(s, 0x3C, 0x40);
  s.replace(0x40, 4, std::string("PE\0\0", 4));
  Put16(s, 0x44, 0x8664);  // machine x64
  Put16(s, 0x46, 1);       // one section
  Put16(s, 0x54, 240);     // optional header size
  const size_t opt = 0x58;
  Put16(s, opt, 0x20B);
  Put32(s, opt + 108, 16);  // data directory count
  Put32(s, opt + 112 + 8, 0x1000);
  Put32(s, opt + 112 + 12, uint32_t(20 * (imports.size() + 1)));
  const size_t sec = opt + 240;
  s.replace(sec, 5, ".data");
  Put32(s, sec + 8, 0x200);
  Put32(s, sec + 12, 0x1000);
  Put32(s, sec + 16, 0x200);
  Put32(s, sec + 20, 0x200);
  uint32_t name_rva = 0x1100;
  for (size_t i = 0; i < imports.size(); ++i) {
    Put32(s, 0x200 + 20 * i + 12, name_rva);
    Put32(s, 0x200 + 20 * i + 16, 0x1180);
    s.replace(0x200 + (name_rva - 0x1000), imports[i].size(), imports[i]);
    name_rva += uint32_t(imports[i].size() + 1);
  }
  return s;
}

static void Write(const fs::path& p, const std::string& data) {
  std::ofstream(p, std::ios::binary).write(data.data(), std::streamsize(data.size()));
}

int main() {
  const fs::path root = fs::temp_directory_path() / "coreoflight_pe_imports_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  const fs::path game = root / "game", sys = root / "system32";
  fs::create_directories(game);
  fs::create_directories(sys);

  // The game: exe -> rexruntime.dll (next to it) -> amd_fidelityfx_dx12.dll; KERNEL32 in "System32"; an API
  // set that is in neither.
  Write(game / "game.exe", Image({"rexruntime.dll", "KERNEL32.dll", "api-ms-win-crt-runtime-l1-1-0.dll"}));
  Write(game / "rexruntime.dll", Image({"amd_fidelityfx_dx12.dll", "KERNEL32.dll", "MSVCP140.dll"}));
  Write(game / "amd_fidelityfx_dx12.dll", Image({"KERNEL32.dll"}));
  Write(game / "MSVCP140.dll", Image({"KERNEL32.dll"}));
  Write(sys / "KERNEL32.dll", Image({}));

  const std::vector<std::string> exe = PeImports(game / "game.exe");
  CHECK(exe.size() == 3);
  CHECK(exe.size() == 3 && exe[0] == "rexruntime.dll" && exe[1] == "KERNEL32.dll");
  CHECK(PeImports(sys / "KERNEL32.dll").empty());
  CHECK(PeImports(root / "nothing.dll").empty());
  Write(root / "text.txt", "not a PE image at all");
  CHECK(PeImports(root / "text.txt").empty());

  // A damaged file's garbage names (bytes outside ASCII, a path) are left out, the plain one stays.
  Write(root / "damaged.dll", Image({"bad\xff\xfe.dll", "..\\up.dll", "C:x.dll", "rexruntime.dll"}));
  const std::vector<std::string> damaged = PeImports(root / "damaged.dll");
  CHECK(damaged.size() == 1 && damaged[0] == "rexruntime.dll");

  CHECK(MissingDlls(game / "game.exe", sys).empty());

  // Windows Security took rexruntime.dll (#146): only that one is named, its own imports can't be read.
  fs::remove(game / "rexruntime.dll");
  std::vector<std::string> missing = MissingDlls(game / "game.exe", sys);
  CHECK(missing.size() == 1 && missing[0] == "rexruntime.dll");

  // A DLL two levels down.
  Write(game / "rexruntime.dll", Image({"amd_fidelityfx_dx12.dll", "KERNEL32.dll", "MSVCP140.dll"}));
  fs::remove(game / "amd_fidelityfx_dx12.dll");
  missing = MissingDlls(game / "game.exe", sys);
  CHECK(missing.size() == 1 && missing[0] == "amd_fidelityfx_dx12.dll");

  // Without a System32 folder to look in (not Windows), Windows' own DLLs count as missing too.
  missing = MissingDlls(game / "game.exe", {});
  CHECK(missing.size() == 2);

  fs::remove_all(root, ec);
  if (g_failures == 0) std::printf("pe_imports: all tests passed\n");
  return g_failures == 0 ? 0 : 1;
}
