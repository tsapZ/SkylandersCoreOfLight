#include "ds3_games.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <mutex>

#include "games.h"
#include "hub_log.h"
#include "hub_settings.h"
#include "platform.h"
#include "portable.h"
#include "updates.h"

namespace coreoflight {
namespace fs = std::filesystem;

namespace {

constexpr uint32_t kMedia = 0x200;  // the 3DS's media unit: offsets and sizes in its headers count these
constexpr char kAzaharRepo[] = "azahar";
constexpr char kAzaharTagPrefix[] = "skylanders-";  // its tags: skylanders-0.1.0-beta.1
#ifdef _WIN32
constexpr char kAzaharAsset[] = "azahar-skylanders-windows";  // -<version>.zip
constexpr char kAzaharExe[] = "azahar.exe";
#else
constexpr char kAzaharAsset[] = "azahar-skylanders-linux";    // none yet: the install says so
constexpr char kAzaharExe[] = "azahar";
#endif
constexpr char kAzaharFolderName[] = "Azahar for Skylanders";

uint32_t Le32(const unsigned char* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

bool ReadAt(std::ifstream& in, uint64_t at, unsigned char* out, size_t n) {
  in.clear();
  in.seekg(std::streamoff(at));
  in.read(reinterpret_cast<char*>(out), std::streamsize(n));
  return size_t(in.gcount()) == n;
}

// The large icon of an SMDH (48x48 RGB565 at 0x24C0, in 8x8 tiles, the pixels of a tile in Z
// order: bit 0 of the index is x's bit 0, bit 1 y's bit 0, and so on) as RGBA rows.
std::vector<uint8_t> SmdhIcon(const std::vector<unsigned char>& smdh) {
  constexpr size_t kAt = 0x24C0, kSide = 48;
  if (smdh.size() < kAt + kSide * kSide * 2 || std::memcmp(smdh.data(), "SMDH", 4) != 0) return {};
  std::vector<uint8_t> rgba(kSide * kSide * 4);
  for (size_t i = 0; i < kSide * kSide; ++i) {
    const size_t tile = i / 64, w = i % 64;
    const size_t x = (tile % 6) * 8 + ((w & 1) | ((w >> 1) & 2) | ((w >> 2) & 4));
    const size_t y = (tile / 6) * 8 + (((w >> 1) & 1) | ((w >> 2) & 2) | ((w >> 3) & 4));
    const unsigned v = smdh[kAt + i * 2] | unsigned(smdh[kAt + i * 2 + 1]) << 8;
    const unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    uint8_t* p = &rgba[(y * kSide + x) * 4];
    p[0] = uint8_t(r << 3 | r >> 2);
    p[1] = uint8_t(g << 2 | g >> 4);
    p[2] = uint8_t(b << 3 | b >> 2);
    p[3] = 255;
  }
  return rgba;
}

std::string LowerAscii(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string ArgValue(const char* prefix) {
  for (const std::string& a : LaunchArgs()) {
    if (a.starts_with(prefix)) return a.substr(std::strlen(prefix));
  }
  return {};
}

// The version in an asset's name: azahar-skylanders-windows-0.1.0-beta.1.zip -> 0.1.0-beta.1.
std::string VersionOfZip(const fs::path& zip) {
  const std::string name = PathUtf8(zip.filename());
  const std::string prefix = std::string(kAzaharAsset) + "-";
  if (!name.starts_with(prefix) || !name.ends_with(".zip")) return {};
  return name.substr(prefix.size(), name.size() - prefix.size() - 4);
}

// The newest version on GitHub, kept on the heap and never freed: the detached thread that asks
// may still run while the launcher ends.
struct Newest {
  std::mutex mutex;
  std::string version;
  bool asking = false, known = false;
};
Newest& TheNewest() {
  static Newest* newest = new Newest;
  return *newest;
}

}  // namespace

const std::vector<Ds3Game>& Ds3Games() {
  static const std::vector<Ds3Game> games = {
      {"ASP", "Spyro's Adventure", "Spyro's Adventure", 2011},
      {"AG6", "Giants", "Giants", 2012},
      {"BSF", "Swap Force", "Swap Force", 2013},
      {"BS9", "Trap Team", "Trap Team", 2014},
      {"BL5", "SuperChargers Racing", "SuperChargers Racing", 2015},
  };
  return games;
}

const char* Ds3RegionName(char region, bool german) {
  switch (region) {
    case 'P': return german ? "Europa" : "Europe";
    case 'E': return "USA";
    case 'J': return "Japan";
    case 'K': return "Korea";
    case 'U': return german ? "Australien" : "Australia";
    default: return "";
  }
}

std::optional<Ds3Dump> ReadDs3Dump(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return std::nullopt;
  // NCSD (the cartridge image): partition 0's offset at 0x120; that partition is the game's NCCH.
  unsigned char head[0x200];
  if (!ReadAt(in, 0, head, sizeof(head)) || std::memcmp(head + 0x100, "NCSD", 4) != 0) return std::nullopt;
  const uint64_t ncch_at = uint64_t(Le32(head + 0x120)) * kMedia;
  unsigned char ncch[0x200];
  if (!ReadAt(in, ncch_at, ncch, sizeof(ncch)) || std::memcmp(ncch + 0x100, "NCCH", 4) != 0) return std::nullopt;
  // Its product code at 0x150: CTR-P-AG6P, the game's letters, then the region's.
  char code[17] = {};
  std::memcpy(code, ncch + 0x150, 16);
  const std::string product(code);
  if (product.size() != 10 || !product.starts_with("CTR-")) return std::nullopt;
  const std::string letters = product.substr(6, 3);
  Ds3Dump dump;
  dump.file = file;
  dump.region = product[9];
  dump.game = Ds3Games().size();
  for (size_t i = 0; i < Ds3Games().size(); ++i) {
    if (letters == Ds3Games()[i].code) dump.game = i;
  }
  if (dump.game == Ds3Games().size()) return std::nullopt;
  // Flag 7 bit 2 (NoCrypto): a decrypted dump, which Azahar runs without the console's keys.
  dump.encrypted = (ncch[0x188 + 7] & 0x04) == 0;
  if (dump.encrypted) return dump;
  // The icon: the ExeFS (offset at 0x1A0) has a file "icon", the SMDH.
  const uint64_t exefs_at = ncch_at + uint64_t(Le32(ncch + 0x1A0)) * kMedia;
  unsigned char exefs[0x200];
  if (Le32(ncch + 0x1A0) == 0 || !ReadAt(in, exefs_at, exefs, sizeof(exefs))) return dump;
  for (int i = 0; i < 10; ++i) {
    const unsigned char* e = exefs + i * 16;
    if (std::memcmp(e, "icon\0\0\0\0", 8) != 0) continue;
    const uint32_t offset = Le32(e + 8), size = Le32(e + 12);
    if (size < 0x36C0 || size > 0x10000) break;
    std::vector<unsigned char> smdh(size);
    if (ReadAt(in, exefs_at + 0x200 + offset, smdh.data(), size)) dump.icon = SmdhIcon(smdh);
    break;
  }
  return dump;
}

std::vector<Ds3Dump> FindDs3Dumps(const fs::path& folder) {
  std::vector<Ds3Dump> found;
  std::error_code ec;
  if (folder.empty() || !fs::is_directory(folder, ec)) return found;
  fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec);
  const fs::recursive_directory_iterator end;
  while (!ec && it != end) {
    if (it.depth() >= 2) it.disable_recursion_pending();
    std::error_code type_ec;
    const std::string ext = LowerAscii(PathUtf8(it->path().extension()));
    if ((ext == ".cci" || ext == ".3ds") && it->is_regular_file(type_ec)) {
      if (std::optional<Ds3Dump> dump = ReadDs3Dump(it->path())) {
        // One per game and region: a decrypted one wins over an encrypted one.
        auto same = std::find_if(found.begin(), found.end(),
                                 [&](const Ds3Dump& d) { return d.game == dump->game && d.region == dump->region; });
        if (same == found.end()) {
          found.push_back(std::move(*dump));
        } else if (same->encrypted && !dump->encrypted) {
          *same = std::move(*dump);
        }
      }
    }
    it.increment(ec);
    // A folder that can't be read (a broken link, a cloud placeholder, one deleted meanwhile): leave
    // it and go on with the next one instead of ending the whole search.
    for (int tries = 0; ec && it != end && tries < 64; ++tries) {
      ec.clear();
      it.pop(ec);
    }
  }
  std::sort(found.begin(), found.end(), [](const Ds3Dump& a, const Ds3Dump& b) {
    return a.game != b.game ? a.game < b.game : a.region < b.region;
  });
  return found;
}

fs::path AzaharFolder() {
  // Where it was installed, as long as it is still there (a new games folder doesn't strand its saves).
  if (const std::string stored = LoadHubSettings().ds3_azahar; !stored.empty()) {
    const fs::path dir = FolderFromStored(stored);
    std::error_code ec;
    if (fs::is_regular_file(dir / kAzaharExe, ec)) return dir;
  }
  return GamesFolder() / kAzaharFolderName;
}

std::string InstalledAzaharVersion() {
  const fs::path dir = AzaharFolder();
  std::error_code ec;
  if (!fs::is_regular_file(dir / kAzaharExe, ec)) return {};
  std::ifstream in(dir / "version.txt");
  std::string line;
  std::getline(in, line);
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
  return line.empty() ? "?" : line;
}

AzaharInstall::~AzaharInstall() {
  if (state_) state_->cancel = true;
  if (thread_.joinable()) thread_.join();
}

void AzaharInstall::Start(bool german) {
  if (running()) return;
  if (thread_.joinable()) thread_.join();
  state_ = std::make_shared<State>();
  std::shared_ptr<State> s = state_;
  thread_ = std::thread([s, german] {
    auto fail = [&](const std::string& why) {
      s->ok = false;
      s->message = why;
      HubWarn("3DS: Azahar for Skylanders not installed: " + why);
      s->done = true;
    };
    // --azahar_zip only with a scratch --install_dir: a test never writes the player's own install.
    fs::path zip = PathFromUtf8(ArgValue("--azahar_zip="));
    std::string version;
    if (!zip.empty()) {
      if (ArgValue("--install_dir=").empty()) return fail("--azahar_zip needs --install_dir=<a scratch folder>");
      version = VersionOfZip(zip);
    } else if (Offline()) {
      return fail(german ? "Offline: nichts heruntergeladen." : "Offline: nothing downloaded.");
    } else {
      const std::optional<Release> release = FetchLatestRelease(kAzaharRepo, kAzaharAsset, ".zip", kAzaharTagPrefix);
      if (!release) {
#ifdef _WIN32
        return fail(german ? "Keine Verbindung zu GitHub, oder dort gibt es noch keine Version."
                           : "No connection to GitHub, or no release there yet.");
#else
        return fail(german ? "Azahar for Skylanders gibt es bisher nur f\xC3\xBCr Windows."
                           : "Azahar for Skylanders is Windows-only so far.");
#endif
      }
      version = release->version;
      zip = DownloadsDir() / PathFromUtf8(release->asset);
      std::error_code ec;
      fs::create_directories(zip.parent_path(), ec);
      if (!DownloadFile(release->url, zip, &s->progress, &s->cancel, release->sha256)) {
        return fail(german ? "Der Download hat nicht geklappt." : "The download didn't work.");
      }
    }
    std::vector<ZipFile> files;
    std::string error;
    if (!ReadZipFiles(zip, files, german, error)) return fail(error);
    const std::string damaged = german ? "Das Paket ist anders aufgebaut als erwartet." : "The package isn't laid out as expected.";
    // One top folder (azahar-skylanders-<version>\) with everything in it, azahar.exe among it.
    if (files.empty()) return fail(damaged);
    const fs::path top = *files.front().path.begin();
    bool has_exe = false;
    for (const ZipFile& f : files) {
      auto part = f.path.begin();
      if (part == f.path.end() || *part != top || std::distance(f.path.begin(), f.path.end()) < 2) return fail(damaged);
      if (f.path == top / kAzaharExe) has_exe = true;
    }
    if (!has_exe) return fail(damaged);

    const fs::path dest = AzaharFolder();
    const fs::path staging = WithPathSuffix(dest, ".new"), old = WithPathSuffix(dest, ".old");
    std::error_code ec;
    fs::remove_all(staging, ec);
    fs::create_directories(staging, ec);
    auto give_up = [&](const std::string& why) {
      std::error_code ignore;
      fs::remove_all(staging, ignore);
      fail(why);
    };
    size_t written = 0;
    for (const ZipFile& f : files) {
      fs::path rel;
      for (auto part = std::next(f.path.begin()); part != f.path.end(); ++part) rel /= *part;
      if (*rel.begin() == "user") continue;  // the player's own settings and saves come from the old folder
      const fs::path out = staging / rel;
      fs::create_directories(out.parent_path(), ec);
      std::ofstream o(out, std::ios::binary | std::ios::trunc);
      o.write(f.data.data(), std::streamsize(f.data.size()));
      o.close();
      if (!o) return give_up((german ? "Konnte nicht schreiben: " : "Couldn't write: ") + PathUtf8(out));
      ++written;
    }
    {
      std::ofstream v(staging / "version.txt", std::ios::trunc);
      v << (version.empty() ? "?" : version) << "\n";
      v.close();
      if (!v) return give_up((german ? "Konnte nicht schreiben: " : "Couldn't write: ") + PathUtf8(staging / "version.txt"));
    }
#ifndef _WIN32
    MakeExecutable(staging / kAzaharExe);
#endif
    // The swap: the old folder's user (settings, saves) into the new one, the old folder aside, the
    // new one in its place, the old one deleted. A running Azahar keeps its files open, so the old
    // folder can't move then: everything goes back as it was.
    const bool had_old = fs::exists(dest, ec);
    if (had_old) {
      fs::remove_all(old, ec);
      const bool had_user = fs::is_directory(dest / "user", ec);
      if (had_user) {
        fs::rename(dest / "user", staging / "user", ec);
        if (ec) return give_up(german ? "Die Spielst\xC3\xA4nde lie\xC3\x9F" "en sich nicht verschieben (l\xC3\xA4uft Azahar noch?)."
                                      : "The saves couldn't be moved (is Azahar still running?).");
      }
      fs::rename(dest, old, ec);
      if (ec) {
        std::error_code back;
        if (had_user) fs::rename(staging / "user", dest / "user", back);
        return give_up(german ? "Der alte Ordner lie\xC3\x9F sich nicht ersetzen (l\xC3\xA4uft Azahar noch?)."
                              : "The old folder couldn't be replaced (is Azahar still running?).");
      }
    } else {
      fs::create_directories(dest.parent_path(), ec);
    }
    fs::create_directories(staging / "user", ec);
    fs::rename(staging, dest, ec);
    if (ec) {
      std::error_code back;
      if (had_old) {
        fs::rename(old, dest, back);
        fs::rename(staging / "user", dest / "user", back);
      }
      return give_up(german ? "Der neue Ordner lie\xC3\x9F sich nicht an seinen Platz bringen."
                            : "The new folder couldn't be put in place.");
    }
    if (had_old) fs::remove_all(old, ec);
    HubLog("3DS: Azahar for Skylanders " + version + " installed into " + PathUtf8(dest) + " (" +
           std::to_string(written) + " files)");
    s->ok = true;
    s->folder = dest;
    s->message = "Azahar for Skylanders " + version + (german ? " ist installiert." : " is installed.");
    s->progress = 1.0f;
    s->done = true;
  });
}

bool AzaharInstall::TakeResult(std::string& message, bool& ok, std::filesystem::path& folder) {
  if (!state_ || !state_->done.load()) return false;
  if (thread_.joinable()) thread_.join();
  message = state_->message;
  ok = state_->ok;
  folder = state_->folder;
  state_.reset();
  return true;
}

void Ds3LookForAzaharUpdate() {
  Newest& n = TheNewest();
  {
    std::lock_guard<std::mutex> lock(n.mutex);
    if (n.asking || n.known || Offline()) return;
    n.asking = true;
  }
  std::thread([&n] {
    const std::optional<Release> release = FetchLatestRelease(kAzaharRepo, kAzaharAsset, ".zip", kAzaharTagPrefix);
    std::lock_guard<std::mutex> lock(n.mutex);
    n.asking = false;
    // A failed try (no network, GitHub slow) is asked again at the next opening of the screen.
    if (!release) return;
    n.version = release->version;
    n.known = true;
  }).detach();
}

std::string Ds3NewestAzahar() {
  Newest& n = TheNewest();
  std::lock_guard<std::mutex> lock(n.mutex);
  return n.version;
}

void* StartDs3Dump(const Ds3Dump& dump, bool german, std::string& error) {
  const fs::path dir = AzaharFolder(), exe = dir / kAzaharExe;
  std::error_code ec;
  if (!fs::is_regular_file(exe, ec)) {
    error = german ? "Azahar for Skylanders ist noch nicht installiert." : "Azahar for Skylanders isn't installed yet.";
    return nullptr;
  }
  if (dump.encrypted) {
    error = german ? "Dieser Dump ist verschl\xC3\xBCsselt: Azahar braucht ihn entschl\xC3\xBCsselt."
                   : "This dump is encrypted: Azahar needs it decrypted.";
    return nullptr;
  }
  void* process = SpawnProcess(exe, {PathUtf8(dump.file)}, dir);
  if (!process) {
    error = german ? "Azahar lie\xC3\x9F sich nicht starten." : "Azahar couldn't be started.";
    return nullptr;
  }
  HubLog("3DS: started " + std::string(Ds3Games()[dump.game].name_en) + " (" + Ds3RegionName(dump.region, false) +
         ") in Azahar for Skylanders " + InstalledAzaharVersion());
  return process;
}

}  // namespace coreoflight
