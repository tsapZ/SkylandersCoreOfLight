#include "disc_search.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "games.h"
#include "language_import.h"
#include "platform.h"
#include "portal/data_folder.h"
#include "xiso.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;

// How deep it goes below the usual folders and below a drive, the folders it leaves out (Windows'
// own, programs, caches) and the smallest ISO it reads (a disc image is GBs).
constexpr int kPlaceDepth = 4;  // Documents\<folder>\<folder>\<folder>\Trap Team.iso
constexpr int kDriveDepth = 2;
constexpr uintmax_t kSmallestIso = 100ull << 20;
const char* const kSkipFolders[] = {"windows", "program files", "program files (x86)", "programdata",
                                    "appdata", "node_modules",  "recovery",            "perflogs"};

bool IsFile(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

// Calls `on_disc` for every .iso file under `dir` and every folder with a default.xex (an
// unpacked disc), `depth` folder levels down, until `stop`. A disc folder itself is one level
// below the limit too: the setup's look next to a disc chosen by hand finds the others there.
void FindDiscs(const fs::path& dir, int depth, const std::atomic<bool>& stop, std::atomic<int>& folders,
               const std::function<void(const fs::path&)>& on_disc) {
  std::error_code ec;
  ++folders;
  if (IsFile(dir / "default.xex")) {  // the search root itself is an unpacked disc
    on_disc(dir);
    return;
  }
  for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; it != end;
       it.increment(ec)) {
    if (ec || stop) break;
    const fs::path p = it->path();
    const std::u8string u8_name = p.filename().u8string();
    std::string name(u8_name.begin(), u8_name.end());
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (name.empty() || name[0] == '$' || name[0] == '.') continue;
    std::error_code type_ec;
    if (it->is_symlink(type_ec)) continue;
    if (it->is_directory(type_ec)) {
#ifdef _WIN32
      const DWORD attributes = GetFileAttributesW(p.c_str());
      if (attributes == INVALID_FILE_ATTRIBUTES ||
          (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT))) {
        continue;
      }
#endif
      // An unpacked disc: read it as one, whatever the depth; the files inside it are no discs.
      if (IsFile(p / "default.xex")) {
        on_disc(p);
        continue;
      }
      if (depth <= 0) continue;
      if (std::find(std::begin(kSkipFolders), std::end(kSkipFolders), name) != std::end(kSkipFolders)) continue;
      FindDiscs(p, depth - 1, stop, folders, on_disc);
    } else {
      const std::u8string u8_ext = p.extension().u8string();
      std::string ext(u8_ext.begin(), u8_ext.end());
      for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (ext == ".iso" && it->file_size(type_ec) >= kSmallestIso) {
        on_disc(p);
      }
    }
  }
}

}  // namespace

std::shared_ptr<DiscSearch> StartDiscSearch() {
  std::vector<std::pair<fs::path, int>> roots;
  for (const fs::path& p : {UserDownloadsDir(), DesktopDir(), skylanders::DocumentsFolder()}) {
    if (!p.empty()) roots.emplace_back(p, kPlaceDepth);
  }
#ifdef _WIN32
  const DWORD drives = GetLogicalDrives();
  for (int d = 0; d < 26; ++d) {
    if (!(drives & (1u << d))) continue;
    const std::wstring root = std::wstring(1, wchar_t(L'A' + d)) + L":\\";
    const UINT type = GetDriveTypeW(root.c_str());
    if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) roots.emplace_back(fs::path(root), kDriveDepth);
  }
#else
  if (const std::string home = EnvVarUtf8("HOME"); !home.empty()) {
    roots.emplace_back(PathFromUtf8(home), kPlaceDepth + 1);
  }
  for (const char* mount : {"/run/media", "/media", "/mnt"}) {
    std::error_code ec;
    if (fs::is_directory(mount, ec)) roots.emplace_back(fs::path(mount), kDriveDepth);
  }
#endif
  return StartDiscSearch(std::move(roots));
}

std::shared_ptr<DiscSearch> StartDiscSearch(std::vector<std::pair<fs::path, int>> roots) {
  auto search = std::make_shared<DiscSearch>();
  std::thread([search, roots] {
    std::set<std::string> seen;  // a disc under two of the roots once
    for (const auto& [root, depth] : roots) {
      FindDiscs(root, depth, search->stop, search->folders, [&](const fs::path& disc) {
        std::error_code ec;
        fs::path canon = fs::weakly_canonical(disc, ec);
        std::string key = PathUtf8(ec ? disc : canon);
#ifdef _WIN32
        for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
        if (!seen.insert(key).second) return;
        const bool folder = fs::is_directory(disc, ec);
        if (folder) {
          ++search->unpacked;
        } else {
          ++search->isos;
        }
        // Read like a disc being set up (disc_import.cpp): its default.xex, known by its SHA-256.
        std::vector<uint8_t> data;
        uint64_t bytes = 0;
        if (!ReadDiscXex(disc, data, &bytes)) {
          if (folder || !IsImaginatorsPs3Disc(disc)) return;
          size_t im = 0;
          while (im < AllGames().size() && std::string_view(AllGames()[im].name_en) != "Imaginators") ++im;
          std::lock_guard lock(search->mutex);
          if (im < AllGames().size()) search->found.push_back({int(im), disc, true});
          return;
        }
        const XexId id = IdentifyXex(data.data(), data.size());
        int game = -1;
        for (size_t i = 0; i < AllGames().size(); ++i) {
          if (IsGameDisc(AllGames()[i], id)) game = int(i);
        }
        std::lock_guard lock(search->mutex);
        search->found.push_back({game, disc, false, bytes});
      });
      if (search->stop) break;
    }
    search->done = true;
  }).detach();
  return search;
}

}  // namespace coreoflight
