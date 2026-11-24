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
constexpr char kRepo[] = "3dsrecomp";
constexpr char kTagPrefix[] = "skylanders-";  // its tags: skylanders-0.1.0-beta.1

uint32_t Le32(const unsigned char* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t Le64(const unsigned char* p) { return Le32(p) | uint64_t(Le32(p + 4)) << 32; }

bool ReadAt(std::ifstream& in, uint64_t at, unsigned char* out, size_t n) {
  in.clear();
  in.seekg(std::streamoff(at));
  in.read(reinterpret_cast<char*>(out), std::streamsize(n));
  return size_t(in.gcount()) == n;
}

std::string Hex64(uint64_t v) {
  char s[17];
  std::snprintf(s, sizeof(s), "%016llx", static_cast<unsigned long long>(v));
  return s;
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

// The version in an asset's name: skylanders-3ds-giants-windows-0.1.0-beta.1.zip -> 0.1.0-beta.1.
std::string VersionOfZip(const Ds3Game& game, const fs::path& zip) {
  const std::string name = PathUtf8(zip.filename());
  const std::string prefix = std::string(game.asset) + "-";
  if (!name.starts_with(prefix) || !name.ends_with(".zip")) return {};
  return name.substr(prefix.size(), name.size() - prefix.size() - 4);
}

// The program id an info.json (the setup's) names, "" if none.
std::string InfoTitle(const fs::path& info) {
  std::ifstream in(info);
  std::string text((std::istreambuf_iterator<char>(in)), {});
  const size_t key = text.find("\"program_id\"");
  if (key == std::string::npos) return {};
  const size_t open = text.find('"', text.find(':', key));
  const size_t close = open == std::string::npos ? open : text.find('"', open + 1);
  return close == std::string::npos ? std::string() : LowerAscii(text.substr(open + 1, close - open - 1));
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

// The setup's own files in resources\game (the rest of that folder is the package's).
constexpr const char* kSetupFiles[] = {"exheader.bin", "code.bin", "romfs.bin", "info.json", "exefs"};

}  // namespace

const std::vector<Ds3Game>& Ds3Games() {
  static const std::vector<Ds3Game> games = {
      {"ASP", "Spyro's Adventure", "Spyro's Adventure", 2011, "000400000004c000", "skylanders-3ds-ssa-windows", "ssa.exe",
       "Skylanders Spyros Adventure 3DS"},
      {"AG6", "Giants", "Giants", 2012, "00040000000a3500", "skylanders-3ds-giants-windows", "giants.exe",
       "Skylanders Giants 3DS"},
      {"BSF", "Swap Force", "Swap Force", 2013, "00040000000e7f00", "skylanders-3ds-sf-windows", "sf.exe",
       "Skylanders Swap Force 3DS"},
      {"BS9", "Trap Team", "Trap Team", 2014, "000400000013be00", "skylanders-3ds-tt-windows", "tt.exe",
       "Skylanders Trap Team 3DS"},
      {"BL5", "SuperChargers Racing", "SuperChargers Racing", 2015, "000400000016ae00", "skylanders-3ds-scr-windows",
       "scr.exe", "Skylanders SuperChargers Racing 3DS"},
  };
  return games;
}

bool Ds3Dump::supported() const { return game < Ds3Games().size() && title_id == Ds3Games()[game].title_id; }

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
  dump.title_id = Hex64(Le64(ncch + 0x118));  // the NCCH header's program id (the exheader's too, for a game)
  dump.game = Ds3Games().size();
  for (size_t i = 0; i < Ds3Games().size(); ++i) {
    if (letters == Ds3Games()[i].code) dump.game = i;
  }
  if (dump.game == Ds3Games().size()) return std::nullopt;
  // Flag 7 bit 2 (NoCrypto): a decrypted dump; an encrypted one needs the console's keys for the setup.
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

fs::path Ds3PackageFolder(const Ds3Game& game) { return GamesFolder() / "3DS" / game.folder; }

std::string InstalledDs3Version(const Ds3Game& game) {
  const fs::path dir = Ds3PackageFolder(game);
  std::error_code ec;
  if (!fs::is_regular_file(dir / game.exe, ec)) return {};
  std::ifstream in(dir / "version.txt");
  std::string line;
  std::getline(in, line);
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
  return line.empty() ? "?" : line;
}

bool Ds3SetUp(const Ds3Game& game) {
  const fs::path dir = Ds3PackageFolder(game) / "resources" / "game";
  std::error_code ec;
  return fs::is_regular_file(dir / "code.bin", ec) && fs::is_regular_file(dir / "romfs.bin", ec) &&
         InfoTitle(dir / "info.json") == game.title_id;
}

fs::path Ds3SaveFolder(const Ds3Game& game) { return SkylandersDir() / "3DS" / game.folder; }

Ds3Install::~Ds3Install() {
  if (state_) state_->cancel = true;
  if (thread_.joinable()) thread_.join();
}

void Ds3Install::Start(size_t index, const Ds3Dump* dump_ptr, bool package, bool german) {
  if (running() || index >= Ds3Games().size()) return;
  if (thread_.joinable()) thread_.join();
  state_ = std::make_shared<State>();
  std::shared_ptr<State> s = state_;
  std::optional<Ds3Dump> dump;
  if (dump_ptr) dump = *dump_ptr;
  thread_ = std::thread([s, index, dump, package, german] {
    const Ds3Game& game = Ds3Games()[index];
    const std::string name = std::string("Skylanders ") + (german ? game.name_de : game.name_en) + " (3DS)";
    auto fail = [&](const std::string& why) {
      s->ok = false;
      s->message = why;
      HubWarn("3DS: " + name + ": " + why);
      s->done = true;
    };
    const fs::path dest = Ds3PackageFolder(game);
    std::string version;
    if (package) {
#ifndef _WIN32
      return fail(german ? "Die 3DS-Spiele gibt es bisher nur f\xC3\xBCr Windows." : "The 3DS games are Windows-only so far.");
#endif
      // --ds3_package_zip only with a scratch --install_dir: a test never writes the player's own install.
      fs::path zip = PathFromUtf8(ArgValue("--ds3_package_zip="));
      if (!zip.empty()) {
        if (ArgValue("--install_dir=").empty()) return fail("--ds3_package_zip needs --install_dir=<a scratch folder>");
        version = VersionOfZip(game, zip);
      } else if (Offline()) {
        return fail(german ? "Offline: nichts heruntergeladen." : "Offline: nothing downloaded.");
      } else {
        const std::optional<Release> release = FetchLatestRelease(kRepo, game.asset, ".zip", kTagPrefix);
        if (!release) {
          return fail(german ? "Keine Verbindung zu GitHub, oder dort gibt es das Spiel noch nicht."
                             : "No connection to GitHub, or the game isn't there yet.");
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
      // One top folder (Skylanders Giants 3DS\) with everything in it, the game's exe among it.
      if (files.empty()) return fail(damaged);
      const fs::path top = *files.front().path.begin();
      bool has_exe = false;
      for (const ZipFile& f : files) {
        auto part = f.path.begin();
        if (part == f.path.end() || *part != top || std::distance(f.path.begin(), f.path.end()) < 2) return fail(damaged);
        if (f.path == top / game.exe) has_exe = true;
      }
      if (!has_exe) return fail(damaged);

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
      // The swap: the old folder's game files (the setup's, from the dump) into the new one, the old
      // folder aside, the new one in its place, the old one deleted. A running game keeps its files open,
      // so the old folder can't move then: everything goes back as it was.
      const bool had_old = fs::exists(dest, ec);
      std::vector<std::string> moved;
      if (had_old) {
        fs::remove_all(old, ec);
        fs::create_directories(staging / "resources" / "game", ec);
        for (const char* f : kSetupFiles) {
          const fs::path from = dest / "resources" / "game" / f, to = staging / "resources" / "game" / f;
          if (!fs::exists(from, ec)) continue;
          fs::remove_all(to, ec);
          fs::rename(from, to, ec);
          if (ec) {
            std::error_code back;
            for (const std::string& m : moved) fs::rename(staging / "resources" / "game" / m, dest / "resources" / "game" / m, back);
            return give_up(german ? "Die Spieldateien lie\xC3\x9F" "en sich nicht verschieben (l\xC3\xA4uft das Spiel noch?)."
                                  : "The game files couldn't be moved (is the game still running?).");
          }
          moved.push_back(f);
        }
        fs::rename(dest, old, ec);
        if (ec) {
          std::error_code back;
          for (const std::string& m : moved) fs::rename(staging / "resources" / "game" / m, dest / "resources" / "game" / m, back);
          return give_up(german ? "Der alte Ordner lie\xC3\x9F sich nicht ersetzen (l\xC3\xA4uft das Spiel noch?)."
                                : "The old folder couldn't be replaced (is the game still running?).");
        }
      } else {
        fs::create_directories(dest.parent_path(), ec);
      }
      fs::rename(staging, dest, ec);
      if (ec) {
        std::error_code back;
        if (had_old) {
          fs::rename(old, dest, back);
          for (const std::string& m : moved) fs::rename(staging / "resources" / "game" / m, dest / "resources" / "game" / m, back);
        }
        return give_up(german ? "Der neue Ordner lie\xC3\x9F sich nicht an seinen Platz bringen."
                              : "The new folder couldn't be put in place.");
      }
      if (had_old) fs::remove_all(old, ec);
      HubLog("3DS: " + name + " " + version + " installed into " + PathUtf8(dest) + " (" + std::to_string(written) + " files)");
    }
    s->progress = 1.0f;
    // The game files from the player's own dump, made by the package itself (minutes for a big one).
    if (dump && !Ds3SetUp(game)) {
      if (!dump->supported()) {
        return fail(german ? "Dieser Dump ist nicht die europ\xC3\xA4ische Fassung, f\xC3\xBCr die das Spiel gemacht ist."
                           : "This dump isn't the European edition the game was made for.");
      }
      s->setting_up = true;
      const fs::path exe = dest / game.exe, out = dest / "resources" / "game";
      void* p = SpawnProcess(exe, {"--setup", PathUtf8(dump->file), "--out", PathUtf8(out)}, dest, true);
      if (!p) return fail(german ? "Die Einrichtung lie\xC3\x9F sich nicht starten." : "The setup couldn't be started.");
      int64_t code = -1;
      while (IsProcessRunning(p, 200, &code)) {
        if (s->cancel) {
          TerminateAndCloseProcess(p);
          return fail(german ? "Abgebrochen." : "Cancelled.");
        }
      }
      CloseProcessHandle(p);
      s->setting_up = false;
      if (code != 0 || !Ds3SetUp(game)) {
        return fail(german ? (dump->encrypted ? "Die Einrichtung hat nicht geklappt: der Dump ist verschl\xC3\xBCsselt, lege boot9.bin "
                                                "deiner Konsole daneben."
                                              : "Die Einrichtung hat nicht geklappt.")
                           : (dump->encrypted ? "The setup didn't work: the dump is encrypted, put your console's boot9.bin next to it."
                                              : "The setup didn't work."));
      }
      HubLog("3DS: " + name + " set up from " + PathUtf8(dump->file));
    }
    s->ok = true;
    s->message = name + (german ? " ist bereit." : " is ready.");
    s->done = true;
  });
}

bool Ds3Install::TakeResult(std::string& message, bool& ok) {
  if (!state_ || !state_->done.load()) return false;
  if (thread_.joinable()) thread_.join();
  message = state_->message;
  ok = state_->ok;
  state_.reset();
  return true;
}

void Ds3LookForUpdates() {
  Newest& n = TheNewest();
  {
    std::lock_guard<std::mutex> lock(n.mutex);
    if (n.asking || n.known || Offline()) return;
    n.asking = true;
  }
  std::thread([&n] {
    // One release has all five: Giants' asset names its version.
    const std::optional<Release> release = FetchLatestRelease(kRepo, Ds3Games()[1].asset, ".zip", kTagPrefix);
    std::lock_guard<std::mutex> lock(n.mutex);
    n.asking = false;
    // A failed try (no network, GitHub slow) is asked again at the next opening of the screen.
    if (!release) return;
    n.version = release->version;
    n.known = true;
  }).detach();
}

std::string Ds3NewestVersion() {
  Newest& n = TheNewest();
  std::lock_guard<std::mutex> lock(n.mutex);
  return n.version;
}

void* StartDs3Game(const Ds3Dump& dump, bool german, std::string& error) {
  const Ds3Game& game = Ds3Games()[dump.game];
  const fs::path dir = Ds3PackageFolder(game), exe = dir / game.exe;
  std::error_code ec;
  if (!fs::is_regular_file(exe, ec)) {
    error = german ? "Das Spiel ist noch nicht installiert (Y)." : "The game isn't installed yet (Y).";
    return nullptr;
  }
  if (!Ds3SetUp(game)) {
    error = german ? "Das Spiel ist noch nicht eingerichtet." : "The game isn't set up yet.";
    return nullptr;
  }
  const fs::path saves = Ds3SaveFolder(game);
  fs::create_directories(saves, ec);
  void* process = SpawnProcess(exe, {"--save-dir=" + PathUtf8(saves)}, dir);
  if (!process) {
    error = german ? "Das Spiel lie\xC3\x9F sich nicht starten." : "The game couldn't be started.";
    return nullptr;
  }
  HubLog("3DS: started " + std::string(game.name_en) + " " + InstalledDs3Version(game) + ", saves in " + PathUtf8(saves));
  return process;
}

}  // namespace coreoflight
