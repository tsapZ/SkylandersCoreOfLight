#include "disc_import.h"

#include <windows.h>
#include <shobjidl.h>

#include <fstream>
#include <iterator>
#include <thread>

#include "xiso.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;

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
    auto image = XisoImage::Open(iso);
    if (!image) {
      say(german ? "Das ist keine Xbox-360-Disc (ISO)." : "That isn't an Xbox 360 disc (ISO).", Phase::kFailed);
      return;
    }
    std::vector<uint8_t> xex;
    const XisoImage::Entry* entry = image->Find("default.xex");
    if (!entry || !image->Read(*entry, xex)) {
      say(german ? "Auf der Disc ist kein default.xex." : "The disc has no default.xex.", Phase::kFailed);
      return;
    }
    const std::string hash = Sha256Hex(xex.data(), xex.size());
    const auto& games = AllGames();
    int game = -1;
    for (size_t i = 0; i < games.size(); ++i) {
      if (hash == games[i].xex_sha256) game = int(i);
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
    shared->total = image->total_size();
    say(name + (german ? ": die Disc wird in den rom-Ordner des Spiels entpackt ..."
                       : ": unpacking the disc into the game's rom folder ..."),
        Phase::kUnpacking);
    std::string error;
    const bool ok = image->Extract(rom, [&](uint64_t done, uint64_t) {
      shared->done = done;
      return true;
    }, error);
    if (!ok) {
      say(name + (german ? ": Entpacken fehlgeschlagen (" : ": unpacking failed (") + error + ")", Phase::kFailed);
      return;
    }
    say(name + (german ? ": Disc eingerichtet, bereit zum Spielen." : ": disc set up, ready to play."), Phase::kDone);
  }).detach();
}

}  // namespace coreoflight
