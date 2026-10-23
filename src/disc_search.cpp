#include "disc_search.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cwctype>
#include <functional>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include "games.h"
#include "xiso.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;

fs::path Known(REFKNOWNFOLDERID id) {
  PWSTR path = nullptr;
  fs::path out;
  if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path))) out = path;
  CoTaskMemFree(path);
  return out;
}

std::wstring Lower(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
  return s;
}

// How deep it goes below the usual folders and below a drive, the folders it leaves out (Windows'
// own, programs, caches) and the smallest ISO it reads (a disc image is GBs).
constexpr int kPlaceDepth = 4;  // Documents\<folder>\<folder>\<folder>\Trap Team.iso
constexpr int kDriveDepth = 2;
constexpr uintmax_t kSmallestIso = 100ull << 20;
const wchar_t* const kSkipFolders[] = {L"windows", L"program files", L"program files (x86)", L"programdata",
                                       L"appdata", L"node_modules", L"recovery", L"perflogs"};

// Calls `on_iso` for every .iso file under `dir`, `depth` folder levels down, until `stop`.
void FindIsos(const fs::path& dir, int depth, const std::atomic<bool>& stop, std::atomic<int>& folders,
              const std::function<void(const fs::path&)>& on_iso) {
  std::error_code ec;
  ++folders;
  for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; it != end;
       it.increment(ec)) {
    if (ec || stop) break;
    const fs::path p = it->path();
    const std::wstring name = Lower(p.filename().wstring());
    if (name.empty() || name[0] == L'$' || name[0] == L'.') continue;
    std::error_code type_ec;
    if (it->is_directory(type_ec)) {
      if (depth <= 0) continue;
      const DWORD attributes = GetFileAttributesW(p.c_str());
      if (attributes == INVALID_FILE_ATTRIBUTES ||
          (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT))) {
        continue;
      }
      if (std::find(std::begin(kSkipFolders), std::end(kSkipFolders), name) != std::end(kSkipFolders)) continue;
      FindIsos(p, depth - 1, stop, folders, on_iso);
    } else if (p.extension().wstring().size() == 4 && Lower(p.extension().wstring()) == L".iso" &&
               it->file_size(type_ec) >= kSmallestIso) {
      on_iso(p);
    }
  }
}

}  // namespace

std::shared_ptr<DiscSearch> StartDiscSearch() {
  auto search = std::make_shared<DiscSearch>();
  std::vector<std::pair<fs::path, int>> roots;
  for (REFKNOWNFOLDERID id : {FOLDERID_Downloads, FOLDERID_Desktop, FOLDERID_Documents}) {
    if (const fs::path p = Known(id); !p.empty()) roots.emplace_back(p, kPlaceDepth);
  }
  const DWORD drives = GetLogicalDrives();
  for (int d = 0; d < 26; ++d) {
    if (!(drives & (1u << d))) continue;
    const std::wstring root = std::wstring(1, wchar_t(L'A' + d)) + L":\\";
    const UINT type = GetDriveTypeW(root.c_str());
    if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) roots.emplace_back(fs::path(root), kDriveDepth);
  }
  std::thread([search, roots] {
    std::set<std::wstring> seen;  // an ISO under two of the roots once
    for (const auto& [root, depth] : roots) {
      FindIsos(root, depth, search->stop, search->folders, [&](const fs::path& iso) {
        std::error_code ec;
        const std::wstring key = Lower(fs::weakly_canonical(iso, ec).wstring());
        if (!seen.insert(key).second) return;
        ++search->isos;
        // Read like a disc being set up (disc_import.cpp): its default.xex, known by its SHA-256.
        auto image = XisoImage::Open(iso);
        const XisoImage::Entry* xex = image ? image->Find("default.xex") : nullptr;
        std::vector<uint8_t> data;
        if (!xex || !image->Read(*xex, data)) return;
        const std::string hash = Sha256Hex(data.data(), data.size());
        int game = -1;
        for (size_t i = 0; i < AllGames().size(); ++i) {
          if (IsGameDisc(AllGames()[i], hash)) game = int(i);
        }
        std::lock_guard lock(search->mutex);
        search->found.push_back({game, iso});
      });
      if (search->stop) break;
    }
    search->done = true;
  }).detach();
  return search;
}

}  // namespace coreoflight
