#include "disc_import.h"

#include <windows.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <thread>

#include "xiso.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;

// The disc's system update folder: the console's, not the game's (the installers leave it out
// too, extract-xiso -s); about 100 MB.
const std::string kSystemUpdate = "$SystemUpdate";
// Room left on the drive after the disc, so the PC isn't left with a full disk.
constexpr uint64_t kSpareBytes = 256ull << 20;

// Bytes free for this user on the drive `path` is on (its nearest existing folder), or nullopt.
std::optional<uint64_t> FreeBytes(fs::path path) {
  std::error_code ec;
  while (!path.empty() && !fs::exists(path, ec)) {
    if (path == path.parent_path()) break;
    path = path.parent_path();
  }
  ULARGE_INTEGER free{};
  if (!GetDiskFreeSpaceExW(path.c_str(), &free, nullptr, nullptr)) return std::nullopt;
  return free.QuadPart;
}

// The bytes of the files under `folder` (an older disc there is overwritten in place).
uint64_t FolderBytes(const fs::path& folder) {
  uint64_t total = 0;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(folder, ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    if (it->is_regular_file(ec)) total += it->file_size(ec);
  }
  return total;
}

std::string Gigabytes(uint64_t bytes, bool german) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.1f GB", double(bytes) / double(1ull << 30));
  std::string out = text;
  if (german) std::replace(out.begin(), out.end(), '.', ',');
  return out;
}

}  // namespace

bool DiscImport::PickIso(fs::path& iso, bool german) {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  bool picked = false;
  IFileOpenDialog* dialog = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    const COMDLG_FILTERSPEC filters[] = {{german ? L"Xbox-360-Disc (ISO)" : L"Xbox 360 disc (ISO)", L"*.iso"},
                                         {german ? L"Alle Dateien" : L"All files", L"*.*"}};
    dialog->SetFileTypes(2, filters);
    dialog->SetTitle(german ? L"Eigene Skylanders-Disc (Xbox 360, ISO) wählen"
                            : L"Choose your own Skylanders disc (Xbox 360, ISO)");
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->Show(GetActiveWindow())) && SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        iso = path;
        picked = true;
        CoTaskMemFree(path);
      }
      item->Release();
    }
    dialog->Release();
  }
  if (SUCCEEDED(init)) CoUninitialize();
  return picked;
}

bool DiscImport::PickFolder(fs::path& folder, bool german) {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  bool picked = false;
  IFileOpenDialog* dialog = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(german ? L"Ordner mit der entpackten Disc (mit default.xex) w\u00e4hlen"
                            : L"Choose the folder with the unpacked disc (with default.xex)");
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->Show(GetActiveWindow())) && SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        folder = path;
        picked = true;
        CoTaskMemFree(path);
      }
      item->Release();
    }
    dialog->Release();
  }
  if (SUCCEEDED(init)) CoUninitialize();
  return picked;
}

bool DiscImport::running() const {
  const Phase p = phase();
  return p == Phase::kReading || p == Phase::kUnpacking;
}

float DiscImport::progress() const {
  if (!shared_ || !shared_->total.load()) return 0;
  return float(double(shared_->done.load()) / double(shared_->total.load()));
}

std::string DiscImport::message() const {
  if (!shared_) return {};
  std::lock_guard lock(shared_->mutex);
  return shared_->message;
}

void DiscImport::Start(const fs::path& iso, const std::vector<Found>& found, bool german) {
  if (running()) return;
  auto shared = std::make_shared<Shared>();
  shared->phase = Phase::kReading;
  shared_ = shared;
  // What the thread needs of the games, copied: the launcher may look for them again meanwhile.
  struct Target {
    bool installed;
    fs::path rom;
  };
  std::vector<Target> targets;
  for (const Found& f : found) targets.push_back({!f.exe.empty(), f.rom});
  std::thread([shared, iso, targets, german] {
    auto say = [&](const std::string& text, Phase phase) {
      {
        std::lock_guard lock(shared->mutex);
        shared->message = text;
      }
      shared->phase = phase;
    };
    // A folder with an unpacked disc, or an ISO.
    std::error_code ec;
    const bool folder = fs::is_directory(iso, ec);
    std::unique_ptr<XisoImage> image;
    std::vector<uint8_t> xex;
    if (folder) {
      std::ifstream in(iso / "default.xex", std::ios::binary);
      xex.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      if (xex.empty()) {
        say(german ? "In dem Ordner ist kein default.xex (die entpackte Disc fehlt)."
                   : "That folder has no default.xex (the unpacked disc is missing).",
            Phase::kFailed);
        return;
      }
    } else {
      image = XisoImage::Open(iso);
      if (!image) {
        say(german ? "Das ist keine Xbox-360-Disc (ISO)." : "That isn't an Xbox 360 disc (ISO).", Phase::kFailed);
        return;
      }
      const XisoImage::Entry* entry = image->Find("default.xex");
      if (!entry || !image->Read(*entry, xex)) {
        say(german ? "Auf der Disc ist kein default.xex." : "The disc has no default.xex.", Phase::kFailed);
        return;
      }
    }
    const std::string hash = Sha256Hex(xex.data(), xex.size());
    const auto& games = AllGames();
    int game = -1;
    for (size_t i = 0; i < games.size(); ++i) {
      if (IsGameDisc(games[i], hash)) game = int(i);
    }
    if (game < 0) {
      say(german ? "Diese Disc kennt Core of Light nicht (ein anderes Spiel oder eine andere Version)."
                 : "Core of Light doesn't know this disc (another game or another version).",
          Phase::kFailed);
      return;
    }
    shared->game = game;
    const std::string name = german ? games[game].name_de : games[game].name_en;
    if (!targets[game].installed) {
      say(name + (german ? ": das Spiel ist noch nicht installiert (erst das Spiel, dann die Disc)."
                         : ": the game isn't installed yet (the game first, then its disc)."),
          Phase::kFailed);
      return;
    }
    const fs::path rom = targets[game].rom;
    // Already there: the same default.xex in its rom folder.
    {
      std::ifstream in(rom / "default.xex", std::ios::binary);
      const std::vector<char> have((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      if (!have.empty() && Sha256Hex(have.data(), have.size()) == hash) {
        say(name + (german ? ": die Disc ist schon eingerichtet." : ": the disc is set up already."), Phase::kDone);
        return;
      }
    }
    // What will be written (the system update left out), and whether the drive has room for it.
    std::vector<std::pair<fs::path, uintmax_t>> files;
    uint64_t total = 0;
    if (folder) {
      for (fs::recursive_directory_iterator it(iso, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const fs::path relative = fs::relative(it->path(), iso, ec);
        const std::u8string u8 = relative.generic_u8string();
        if (XisoImage::InFolder(std::string(u8.begin(), u8.end()), kSystemUpdate)) continue;
        const uintmax_t size = it->file_size(ec);
        files.emplace_back(relative, size);
        total += size;
      }
      // default.xex last (as when unpacking an ISO).
      std::stable_partition(files.begin(), files.end(), [](const auto& f) { return f.first != "default.xex"; });
    } else {
      total = image->total_size(kSystemUpdate);
    }
    const uint64_t existing = FolderBytes(rom);
    const uint64_t needed = (total > existing ? total - existing : 0) + kSpareBytes;
    if (const auto free = FreeBytes(rom); free && *free < needed) {
      say(name + (german ? ": nicht genug Platz auf dem Laufwerk (" + Gigabytes(*free, true) + " frei, " +
                               Gigabytes(needed, true) + " n\xC3\xB6tig)."
                         : ": not enough room on the drive (" + Gigabytes(*free, false) + " free, " +
                               Gigabytes(needed, false) + " needed)."),
          Phase::kFailed);
      return;
    }
    shared->total = total;
    std::string error;
    bool ok = false;
    uint64_t written = 0;
    if (folder) {
      say(name + (german ? ": die Disc wird in den rom-Ordner des Spiels kopiert ..."
                         : ": copying the disc into the game's rom folder ..."),
          Phase::kUnpacking);
      ok = true;
      for (const auto& [relative, size] : files) {
        if (shared->cancel) {
          error = "stopped";
          ok = false;
          break;
        }
        fs::create_directories((rom / relative).parent_path(), ec);
        if (!fs::copy_file(iso / relative, rom / relative, fs::copy_options::overwrite_existing, ec)) {
          error = (german ? "kann nicht kopieren: " : "cannot copy: ") + relative.string();
          ok = false;
          break;
        }
        written += size;
        shared->done = written;
      }
    } else {
      say(name + (german ? ": die Disc wird in den rom-Ordner des Spiels entpackt ..."
                         : ": unpacking the disc into the game's rom folder ..."),
          Phase::kUnpacking);
      ok = image->Extract(rom, [&](uint64_t done, uint64_t) {
        shared->done = written = done;
        return !shared->cancel.load();
      }, error, kSystemUpdate);
    }
    if (!ok) {
      // Files of this disc are in the folder now, so an older disc's default.xex there would make
      // the mix look like a complete disc: without it the game and the launcher see none.
      if (written) fs::remove(rom / "default.xex", ec);
      if (shared->cancel) {
        say(name + (german ? ": Einlesen abgebrochen, die Disc ist nicht eingerichtet."
                           : ": stopped, the disc isn't set up."),
            Phase::kFailed);
      } else {
        say(name + (german ? ": Entpacken fehlgeschlagen (" : ": unpacking failed (") + error + ")", Phase::kFailed);
      }
      return;
    }
    say(name + (german ? ": Disc eingerichtet, bereit zum Spielen." : ": disc set up, ready to play."), Phase::kDone);
  }).detach();
}

}  // namespace coreoflight
