#include "game_files.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>

#include "hub_log.h"
#include "xiso.h"

namespace coreoflight {
namespace fs = std::filesystem;

namespace {

constexpr char kLanguageBackup[] = "archives/german_text_backup";

std::string Generic(const fs::path& p) {
  const std::u8string u8 = p.generic_u8string();
  return std::string(u8.begin(), u8.end());
}

fs::path FromGeneric(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

// Whether Imaginators' language import changed the file on purpose: its archives' backup
// (<name>.json, .disc.json, .platform.json) or its movies' (movies\<name>).
bool ChangedByLanguageImport(const fs::path& rom, const std::string& path) {
  const fs::path backup = rom / FromGeneric(kLanguageBackup);
  const fs::path name = FromGeneric(path).filename();
  std::error_code ec;
  for (const char* suffix : {".json", ".disc.json", ".platform.json"}) {
    if (fs::exists(backup / (name.wstring() + fs::path(suffix).wstring()), ec)) return true;
  }
  return fs::exists(backup / "movies" / name, ec);
}

}  // namespace

bool WriteDiscFileList(const fs::path& rom, const std::vector<std::pair<std::string, uint64_t>>& files) {
  std::ofstream out(rom / kDiscFileList, std::ios::binary | std::ios::trunc);
  out << "# Skylanders: Core of Light: the disc's files as they were set up (path, size), for \"Check game files\"\n";
  for (const auto& [path, size] : files) out << size << '\t' << path << '\n';
  return bool(out);
}

GameFilesCheck CheckGameFiles(const fs::path& rom, const std::string& xex_sha256) {
  GameFilesCheck check;
  std::error_code ec;
  // default.xex: the supported disc's.
  {
    std::ifstream in(rom / "default.xex", std::ios::binary);
    const std::vector<char> xex((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!xex.empty()) {
      const std::string hash = Sha256Hex(xex.data(), xex.size());
      std::stringstream known(xex_sha256);
      for (std::string h; std::getline(known, h, ',');) check.xex_ok = check.xex_ok || h == hash;
    }
  }
  std::ifstream list(rom / kDiscFileList, std::ios::binary);
  check.had_list = bool(list);
  if (check.had_list) {
    for (std::string line; std::getline(list, line);) {
      if (line.empty() || line[0] == '#') continue;
      const size_t tab = line.find('\t');
      if (tab == std::string::npos) continue;
      const uint64_t size = std::strtoull(line.c_str(), nullptr, 10);
      const std::string path = line.substr(tab + 1);
      ++check.files;
      const fs::path file = rom / FromGeneric(path);
      if (!fs::is_regular_file(file, ec)) {
        check.missing.push_back(path);
      } else if (fs::file_size(file, ec) != size && !ChangedByLanguageImport(rom, path)) {
        check.changed.push_back(path);
      }
    }
  } else if (check.xex_ok) {
    // A disc set up before there was a list: its default.xex is right, so the folder as it is now
    // becomes the list (the language import's backup and this list left out).
    std::vector<std::pair<std::string, uint64_t>> files;
    for (fs::recursive_directory_iterator it(rom, fs::directory_options::skip_permission_denied, ec), end; it != end;
         it.increment(ec)) {
      if (ec) break;
      const std::string path = Generic(it->path().lexically_relative(rom));
      if (it->is_directory(ec)) {
        // Not the disc's own files: the language import's backup, and the console's system update
        // (a disc import leaves it out).
        if (path == kLanguageBackup || XisoImage::InFolder(path, "$SystemUpdate")) it.disable_recursion_pending();
        continue;
      }
      if (!it->is_regular_file(ec) || path == kDiscFileList) continue;
      files.emplace_back(path, it->file_size(ec));
    }
    check.files = files.size();
    WriteDiscFileList(rom, files);
  }
  if (!check.xex_ok && std::find(check.missing.begin(), check.missing.end(), "default.xex") == check.missing.end()) {
    // Not in the list's results yet (no list, or the list has it with the right size).
    if (!fs::is_regular_file(rom / "default.xex", ec)) {
      check.missing.insert(check.missing.begin(), "default.xex");
    } else if (std::find(check.changed.begin(), check.changed.end(), "default.xex") == check.changed.end()) {
      check.changed.insert(check.changed.begin(), "default.xex");
    }
  }
  HubLog("game files: " + Generic(rom) + ": " + std::to_string(check.files) + " files" +
         (check.had_list ? "" : " (list made now)") + ", " + std::to_string(check.missing.size()) + " missing, " +
         std::to_string(check.changed.size()) + " changed, default.xex " + (check.xex_ok ? "right" : "wrong"));
  return check;
}

}  // namespace coreoflight
