#include "ds3_games.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <mutex>

#include "games.h"
#include "hub_log.h"
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
#else
constexpr char kAzaharAsset[] = "azahar-skylanders-linux";    // none yet: the screen says so
#endif
constexpr char kAzaharFolderName[] = "Azahar for Skylanders";
#ifdef _WIN32
constexpr char kAzaharExe[] = "azahar.exe";
#else
constexpr char kAzaharExe[] = "azahar";
#endif

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

// --azahar_zip=<zip>: a local Azahar for Skylanders to install (scripted tests, also --offline).
fs::path AzaharZipArg() {
  for (const std::string& a : LaunchArgs()) {
    if (a.starts_with("--azahar_zip=")) return PathFromUtf8(a.substr(13));
  }
  return {};
}

// The version in an asset's name: azahar-skylanders-windows-0.1.0-beta.1.zip -> 0.1.0-beta.1.
std::string VersionOfZip(const fs::path& zip) {
  std::string name = PathUtf8(zip.filename());
  const std::string prefix = std::string(kAzaharAsset) + "-";
  if (!name.starts_with(prefix) || !name.ends_with(".zip")) return {};
  return name.substr(prefix.size(), name.size() - prefix.size() - 4);
}

std::mutex g_newest_mutex;
std::string g_newest;
bool g_newest_asked = false;

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
    case 'K': return german ? "Korea" : "Korea";
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
  for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (it.depth() >= 2) it.disable_recursion_pending();
    std::error_code type_ec;
    if (!it->is_regular_file(type_ec)) continue;
    const std::string ext = LowerAscii(PathUtf8(it->path().extension()));
    if (ext != ".cci" && ext != ".3ds") continue;
    std::optional<Ds3Dump> dump = ReadDs3Dump(it->path());
    if (!dump) continue;
    // One per game and region: a decrypted one wins over an encrypted one.
    auto same = std::find_if(found.begin(), found.end(),
                             [&](const Ds3Dump& d) { return d.game == dump->game && d.region == dump->region; });
    if (same == found.end()) {
      found.push_back(std::move(*dump));
    } else if (same->encrypted && !dump->encrypted) {
      *same = std::move(*dump);
    }
  }
  std::sort(found.begin(), found.end(), [](const Ds3Dump& a, const Ds3Dump& b) {
    return a.game != b.game ? a.game < b.game : a.region < b.region;
  });
  return found;
}

fs::path AzaharFolder() { return GamesFolder() / kAzaharFolderName; }

std::string InstalledAzaharVersion() {
  std::error_code ec;
  if (!fs::is_regular_file(AzaharFolder() / kAzaharExe, ec)) return {};
  std::ifstream in(AzaharFolder() / "version.txt");
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
    fs::path zip = AzaharZipArg();
    std::string version;
    if (!zip.empty()) {
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
    // The files below the ZIP's one top folder (azahar-skylanders-<version>\) into AzaharFolder(),
    // over the old ones; its "user" folder (settings and saves) is never replaced.
    const fs::path dest = AzaharFolder();
    std::error_code ec;
    fs::create_directories(dest / "user", ec);
    size_t written = 0;
    for (const ZipFile& f : files) {
      auto part = f.path.begin();
      if (part == f.path.end()) continue;
      fs::path rel;
      for (++part; part != f.path.end(); ++part) rel /= *part;
      if (rel.empty() || *rel.begin() == "user") continue;
      const fs::path out = dest / rel;
      fs::create_directories(out.parent_path(), ec);
      std::ofstream o(out, std::ios::binary | std::ios::trunc);
      o.write(f.data.data(), std::streamsize(f.data.size()));
      if (!o) return fail((german ? "Konnte nicht schreiben: " : "Couldn't write: ") + PathUtf8(out));
      ++written;
    }
    if (!fs::is_regular_file(dest / kAzaharExe, ec)) {
      return fail(german ? "Im Paket fehlt azahar.exe." : "The package has no azahar.exe.");
    }
    std::ofstream(dest / "version.txt", std::ios::trunc) << (version.empty() ? "?" : version) << "\n";
    HubLog("3DS: Azahar for Skylanders " + version + " installed into " + PathUtf8(dest) + " (" +
           std::to_string(written) + " files)");
    s->ok = true;
    s->message = (german ? "Azahar for Skylanders " : "Azahar for Skylanders ") + version +
                 (german ? " ist installiert." : " is installed.");
    s->progress = 1.0f;
    s->done = true;
  });
}

bool AzaharInstall::TakeResult(std::string& message, bool& ok) {
  if (!state_ || !state_->done.load()) return false;
  if (thread_.joinable()) thread_.join();
  message = state_->message;
  ok = state_->ok;
  state_.reset();
  return true;
}

void Ds3LookForAzaharUpdate() {
  {
    std::lock_guard<std::mutex> lock(g_newest_mutex);
    if (g_newest_asked || Offline()) return;
    g_newest_asked = true;
  }
  std::thread([] {
    const std::optional<Release> release = FetchLatestRelease(kAzaharRepo, kAzaharAsset, ".zip", kAzaharTagPrefix);
    if (!release) return;
    std::lock_guard<std::mutex> lock(g_newest_mutex);
    g_newest = release->version;
  }).detach();
}

std::string Ds3NewestAzahar() {
  std::lock_guard<std::mutex> lock(g_newest_mutex);
  return g_newest;
}

void* StartDs3Dump(const Ds3Dump& dump, bool german, std::string& error) {
  const fs::path exe = AzaharFolder() / kAzaharExe;
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
  void* process = SpawnProcess(exe, {PathUtf8(dump.file)}, AzaharFolder());
  if (!process) {
    error = german ? "Azahar lie\xC3\x9F sich nicht starten." : "Azahar couldn't be started.";
    return nullptr;
  }
  HubLog("3DS: started " + std::string(Ds3Games()[dump.game].name_en) + " (" + Ds3RegionName(dump.region, false) +
         ") in Azahar for Skylanders " + InstalledAzaharVersion());
  return process;
}

}  // namespace coreoflight
