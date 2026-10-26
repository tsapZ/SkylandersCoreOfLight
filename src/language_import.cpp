#include "language_import.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "alchemy/lzma_decoder.h"
#include "hub_log.h"

// stb_image's zlib decoder for the raw deflate blocks, every function static (nothing else of it
// is used).
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include <stb_image.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace fs = std::filesystem;

namespace coreoflight {
namespace {

// The constants of ImaginatorsRecomp's tools/german_text.py (which this ports).
constexpr uint32_t kBlock = 0x8000;
constexpr uint32_t kStored = 0xFFFFFFFF;
constexpr uint32_t kPlatformPs3 = 6;
constexpr uint32_t kPlatformXenon = 5;
constexpr char kSpanishText[] = "_es.lng";
constexpr char kSpanishVoices[] = "_es.pak";
constexpr char kBackupDir[] = "german_text_backup";  // the Python tool's name, so both undo each other
constexpr char kMoviesDir[] = "movies";
constexpr char kLanguageNote[] = "language.txt";
// The movies' spoken tracks (Bink track ids): this disc's 5 and 6 carry the PS3's 9 (Spanish) and
// 7 (Italian); the PS3's order from 5 is English, French, Italian, German, Spanish, ...
const std::map<uint32_t, uint32_t> kThisDiscSpoken = {{5, 9}, {6, 7}};

struct LanguageInfo {
  const char* code;
  const char* suffix;          // the files' suffix: "_en"
  const char* voice_folder;    // the voices' folder: "/english/"
  const char* cutscene_voice;  // a cutscene voice's ending: ".wav" (English has none), "_de.wav"
  uint32_t movie_track;        // the PS3 movies' spoken track
};
constexpr LanguageInfo kInfo[] = {
    {"en", "_en", "/english/", ".wav", 5},
    {"de", "_de", "/german/", "_de.wav", 8},
    {"fr", "_fr", "/french/", "_fr.wav", 6},
};
constexpr LanguageImport::Language kLanguages[] = {
    {"en", "English", "English", "Englisch"},
    {"de", "Deutsch", "German", "Deutsch"},
    {"fr", "Fran\xC3\xA7" "ais", "French", "Franz\xC3\xB6sisch"},
};

const LanguageInfo* InfoOf(const std::string& code) {
  for (const LanguageInfo& info : kInfo) {
    if (code == info.code) return &info;
  }
  return nullptr;
}

// The 360's own texts whose Spanish differs from both PS3 disc files (they name Xbox Live, the
// gamertag or the 360's START button), as german_text.py has them, and the same in English.
const std::map<std::string, std::string> kXenonGerman = {
    {"CGlobalPlatformStrings::_cableUnplugged", "Du musst bei Xbox Live angemeldet sein, um diese Funktion zu nutzen."},
    {"CGlobalPlatformStrings::_friendScoresUnavailable",
     "Der Imaginators-Dienst ist nicht verf\xC3\xBC" "gbar. Bitte versuche es sp\xC3\xA4ter noch einmal."},
    {"CGlobalPlatformStrings::_gamertagString", "GAMERTAG"},
    {"CGlobalPlatformStrings::_onlineMultiplayerRestrictedMessage",
     "Du musst mit einem Xbox Live-Profil angemeldet sein, das die n\xC3\xB6tigen Berechtigungen zum Spielen hat."},
    {"CGlobalPlatformStrings::_quitToHubConfirmation",
     "Zum O.R.T. zur\xC3\xBC" "ckkehren? Fortschritte seit dem letzten Speichern gehen verloren."},
    {"CGlobalPlatformStrings::_serverUnavailable",
     "Der Imaginators-Dienst ist nicht verf\xC3\xBC" "gbar. Bitte versuche es sp\xC3\xA4ter noch einmal."},
    {"CGlobalPlatformStrings::_signinToViewLeaderboard",
     "Melde dich bei Xbox Live an, um auf Online-Features zuzugreifen."},
    {"CGlobalPlatformStrings::_titleScreenPressStartString", "Dr\xC3\xBC" "cke START"},
    {"CGlobalPlatformStrings::_titleScreenTapToStartString", "Zum Starten antippen"},
};
const std::map<std::string, std::string> kXenonEnglish = {
    {"CGlobalPlatformStrings::_cableUnplugged", "You need to be signed in to Xbox Live to use this feature."},
    {"CGlobalPlatformStrings::_friendScoresUnavailable",
     "The Imaginators service is unavailable. Please try again later."},
    {"CGlobalPlatformStrings::_gamertagString", "GAMERTAG"},
    {"CGlobalPlatformStrings::_onlineMultiplayerRestrictedMessage",
     "You need to be signed in with an Xbox Live profile that is allowed to play online."},
    {"CGlobalPlatformStrings::_serverUnavailable", "The Imaginators service is unavailable. Please try again later."},
    {"CGlobalPlatformStrings::_signinToViewLeaderboard", "Sign in to Xbox Live to use the online features."},
    {"CGlobalPlatformStrings::_titleScreenPressStartString", "Press START"},
    {"CGlobalPlatformStrings::_titleScreenTapToStartString", "Tap to start"},
};
// French: the PS3's wording fits all but one (it names the PlayStation Network).
const std::map<std::string, std::string> kXenonFrench = {
    {"CGlobalPlatformStrings::_signinToViewLeaderboard",
     "Tu dois \xC3\xAAtre connect\xC3\xA9 \xC3\xA0 Xbox Live pour acc\xC3\xA9" "der aux fonctions en ligne de ce jeu."},
};

// A path from a UTF-8 name.
fs::path Path(const std::string& name) { return fs::path(std::u8string(name.begin(), name.end())); }

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return s;
}

bool EndsWith(const std::string& s, const std::string& end) {
  return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

std::string Replace(std::string s, const std::string& from, const std::string& to) {
  for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
  return s;
}

uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t Le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint64_t Be64(const uint8_t* p) { return uint64_t(Be32(p)) << 32 | Be32(p + 4); }
void PutLe32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
void PutBe32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (24 - 8 * i));
}
void PutBe64(uint8_t* p, uint64_t v) {
  PutBe32(p, uint32_t(v >> 32));
  PutBe32(p + 4, uint32_t(v));
}

// ---- The PS3 disc: its .iso's ISO 9660 directory, or an unpacked folder ----

// Where each of the disc's archives (PS3_GAME\USRDIR\archives) is: in the .iso at a byte offset, or
// a file. Names lower case.
class SourceDisc {
 public:
  bool Open(const fs::path& source) {
    std::error_code ec;
    if (fs::is_directory(source, ec)) return OpenFolder(source);
    return OpenIso(source);
  }
  bool Has(const std::string& name) const { return files_.count(name) != 0; }
  // Reads `size` bytes at `offset` of the named file.
  bool ReadPart(const std::string& name, uint64_t offset, size_t size, uint8_t* out) {
    auto it = files_.find(name);
    if (it == files_.end() || offset + size > it->second.size) return false;
    if (!it->second.path.empty()) {
      std::ifstream f(it->second.path, std::ios::binary);
      f.seekg(std::streamoff(offset));
      return bool(f.read(reinterpret_cast<char*>(out), std::streamsize(size)));
    }
    iso_.clear();
    iso_.seekg(std::streamoff(it->second.offset + offset));
    return bool(iso_.read(reinterpret_cast<char*>(out), std::streamsize(size)));
  }
  std::optional<std::vector<uint8_t>> Read(const std::string& name) {
    auto it = files_.find(name);
    if (it == files_.end()) return std::nullopt;
    std::vector<uint8_t> data(size_t(it->second.size));
    if (!data.empty() && !ReadPart(name, 0, data.size(), data.data())) return std::nullopt;
    return data;
  }
  uint64_t Size(const std::string& name) const {
    auto it = files_.find(name);
    return it == files_.end() ? 0 : it->second.size;
  }

 private:
  struct Place {
    uint64_t offset = 0, size = 0;
    fs::path path;  // a folder's file; empty in an .iso
  };
  struct DirEntry {
    std::string name;  // lower case, without ";1"
    uint32_t lba = 0;
    uint64_t size = 0;
    bool dir = false;
  };

  bool OpenFolder(const fs::path& folder) {
    std::error_code ec;
    fs::path archives = folder;
    for (const fs::path& candidate : {folder / "PS3_GAME" / "USRDIR" / "archives", folder / "USRDIR" / "archives",
                                      folder / "archives"}) {
      if (fs::is_directory(candidate, ec)) {
        archives = candidate;
        break;
      }
    }
    for (const auto& e : fs::directory_iterator(archives, ec)) {
      if (!e.is_regular_file(ec)) continue;
      const std::u8string u8 = e.path().filename().u8string();
      files_[Lower(std::string(u8.begin(), u8.end()))] = {0, uint64_t(e.file_size(ec)), e.path()};
    }
    return !files_.empty();
  }

  bool OpenIso(const fs::path& path) {
    iso_.open(path, std::ios::binary);
    if (!iso_) return false;
    // The primary directory cuts long names to 31 characters (BOSS_Doomlander_Archer_bank_Lv_de.pak
    // isn't found there), so the Joliet one (up to 64, UCS-2) where the image has it.
    uint8_t pvd[2048], vd[2048];
    if (!Sector(16, pvd) || pvd[0] != 1 || std::memcmp(pvd + 1, "CD001", 5) != 0) return false;
    const uint8_t* use = pvd;
    for (uint32_t s = 17; s < 32 && Sector(s, vd) && std::memcmp(vd + 1, "CD001", 5) == 0 && vd[0] != 255; ++s) {
      if (vd[0] == 2 && vd[88] == '%' && vd[89] == '/' && (vd[90] == '@' || vd[90] == 'C' || vd[90] == 'E')) {
        joliet_ = true;
        use = vd;
        break;
      }
    }
    DirEntry root;
    root.lba = Le32(use + 156 + 2);
    root.size = Le32(use + 156 + 10);
    root.dir = true;
    DirEntry dir = root;
    for (const char* part : {"ps3_game", "usrdir", "archives"}) {
      bool found = false;
      for (const DirEntry& e : List(dir)) {
        if (e.dir && e.name == part) {
          dir = e;
          found = true;
          break;
        }
      }
      if (!found) return false;
    }
    for (const DirEntry& e : List(dir)) {
      if (!e.dir) files_[e.name] = {uint64_t(e.lba) * 2048, e.size, {}};
    }
    return !files_.empty();
  }

  bool Sector(uint32_t lba, uint8_t* out) {
    iso_.clear();
    iso_.seekg(std::streamoff(uint64_t(lba) * 2048));
    return bool(iso_.read(reinterpret_cast<char*>(out), 2048));
  }

  std::vector<DirEntry> List(const DirEntry& dir) {
    std::vector<DirEntry> out;
    std::vector<uint8_t> data(size_t((dir.size + 2047) / 2048 * 2048));
    for (size_t s = 0; s < data.size() / 2048; ++s) {
      if (!Sector(dir.lba + uint32_t(s), data.data() + s * 2048)) return out;
    }
    size_t at = 0;
    while (at < data.size()) {
      const uint8_t length = data[at];
      if (length == 0) {  // the rest of this sector is empty
        at = (at / 2048 + 1) * 2048;
        continue;
      }
      if (at + length > data.size() || length < 34) break;
      const uint8_t* rec = data.data() + at;
      const uint8_t name_length = rec[32];
      std::string name(reinterpret_cast<const char*>(rec + 33), std::min<size_t>(name_length, length - 33));
      const bool self_or_parent = name == std::string(1, '\0') || name == std::string(1, '\1');
      if (joliet_ && !self_or_parent) name = Ucs2(name);
      if (!self_or_parent) {
        if (const size_t semi = name.find(';'); semi != std::string::npos) name.resize(semi);
        if (!name.empty() && name.back() == '.') name.pop_back();
        out.push_back({Lower(name), Le32(rec + 2), Le32(rec + 10), (rec[25] & 2) != 0});
      }
      at += length;
    }
    return out;
  }

  // A Joliet name (UCS-2, big-endian) as UTF-8.
  static std::string Ucs2(const std::string& raw) {
    std::string out;
    for (size_t i = 0; i + 1 < raw.size(); i += 2) {
      const unsigned c = unsigned(uint8_t(raw[i])) << 8 | uint8_t(raw[i + 1]);
      if (c < 0x80) {
        out += char(c);
      } else if (c < 0x800) {
        out += char(0xC0 | c >> 6);
        out += char(0x80 | (c & 0x3F));
      } else {
        out += char(0xE0 | c >> 12);
        out += char(0x80 | (c >> 6 & 0x3F));
        out += char(0x80 | (c & 0x3F));
      }
    }
    return out;
  }

  std::ifstream iso_;
  bool joliet_ = false;
  std::unordered_map<std::string, Place> files_;
};

// ---- An Alchemy igArchive version 11 (big-endian), as far as this needs it ----

class Iga {
 public:
  using ReadFn = std::function<bool(uint64_t offset, size_t size, uint8_t* out)>;
  struct File {
    std::string name;  // as stored ("Temporary/BuildServer/xenon/Output/...")
    uint64_t ordoff = 0;
    uint32_t length = 0, block = 0;
  };

  bool Open(ReadFn read) {
    read_ = std::move(read);
    uint8_t head[0x38];
    if (!read_(0, sizeof(head), head) || std::memcmp(head, "\x1A" "AGI", 4) != 0 || Be32(head + 4) != 11) return false;
    const uint32_t count = Be32(head + 0x0C);
    sector_ = Be32(head + 0x10);
    const uint32_t n_large = Be32(head + 0x1C), n_medium = Be32(head + 0x20), n_small = Be32(head + 0x24);
    const uint64_t names_off = Be64(head + 0x28);
    const uint32_t names_size = Be32(head + 0x30);
    if (count > 1000000 || !sector_) return false;
    entries_at_ = 0x38 + 4ull * count;
    std::vector<uint8_t> toc(4ull * count + 16ull * count + 4ull * n_large + 2ull * n_medium + n_small);
    std::vector<uint8_t> names(names_size);
    if (!read_(0x38, toc.size(), toc.data()) || !read_(names_off, names.size(), names.data())) return false;
    files_.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
      const uint8_t* e = toc.data() + 4ull * count + 16ull * i;
      files_[i].ordoff = Be64(e);
      files_[i].length = Be32(e + 8);
      files_[i].block = Be32(e + 12);
      const uint32_t at = Be32(names.data() + 4ull * i);
      if (at >= names.size()) return false;
      const char* name = reinterpret_cast<const char*>(names.data() + at);
      files_[i].name.assign(name, strnlen(name, names.size() - at));
    }
    size_t t = 4ull * count + 16ull * count;
    for (uint32_t i = 0; i < n_large; ++i, t += 4) large_.push_back(Be32(toc.data() + t));
    for (uint32_t i = 0; i < n_medium; ++i, t += 2) medium_.push_back(uint16_t(toc[t] << 8 | toc[t + 1]));
    small_.assign(toc.begin() + ptrdiff_t(t), toc.begin() + ptrdiff_t(t + n_small));
    return true;
  }

  const std::vector<File>& files() const { return files_; }
  uint64_t entries_at() const { return entries_at_; }
  uint32_t sector() const { return sector_; }

  std::optional<size_t> Find(const std::string& lower_name) const {
    for (size_t i = 0; i < files_.size(); ++i) {
      if (Lower(files_[i].name) == lower_name) return i;
    }
    return std::nullopt;
  }

  std::optional<std::vector<uint8_t>> Read(size_t i) const {
    const File& f = files_[i];
    const uint64_t offset = f.ordoff & 0xFFFFFFFFFFull;
    std::vector<uint8_t> out;
    if (f.block == kStored) {
      out.resize(f.length);
      if (f.length && !read_(offset, f.length, out.data())) return std::nullopt;
      return out;
    }
    const uint32_t comp = f.block >> 28, first = f.block & 0x0FFFFFFF;
    out.reserve(f.length);
    for (uint32_t b = 0; b < (f.length + kBlock - 1) / kBlock; ++b) {
      bool compressed = false;
      uint32_t sector = 0;
      if (!Entry(first + b, f.length, &compressed, &sector)) return std::nullopt;
      const uint32_t want = std::min(kBlock, f.length - b * kBlock);
      const uint64_t at = offset + uint64_t(sector) * sector_;
      if (!compressed) {
        const size_t old = out.size();
        out.resize(old + want);
        if (!read_(at, want, out.data() + old)) return std::nullopt;
        continue;
      }
      uint8_t size_bytes[2];
      if (!read_(at, 2, size_bytes)) return std::nullopt;
      const uint32_t packed = Le16(size_bytes) + (comp == 2 ? 5 : 0);  // LZMA: 5 property bytes first
      std::vector<uint8_t> block(packed);
      if (!read_(at + 2, packed, block.data())) return std::nullopt;
      if (comp == 2) {
        auto data = alchemy::LzmaDecode(block.data(), block.size(), want);
        if (!data) return std::nullopt;
        out.insert(out.end(), data->begin(), data->end());
      } else if (comp == 1) {
        const size_t old = out.size();
        out.resize(old + want);
        const int written = stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(out.data() + old), int(want),
                                                             reinterpret_cast<const char*>(block.data()), int(block.size()));
        if (written != int(want)) return std::nullopt;
      } else {
        return std::nullopt;
      }
    }
    return out;
  }

 private:
  bool Entry(uint32_t index, uint32_t length, bool* compressed, uint32_t* sector) const {
    if (0x7Full * sector_ < length) {
      if (0x7FFFull * sector_ < length) {
        if (index >= large_.size()) return false;
        *compressed = large_[index] >> 31;
        *sector = large_[index] & 0x7FFFFFFF;
        return true;
      }
      if (index >= medium_.size()) return false;
      *compressed = medium_[index] >> 15;
      *sector = medium_[index] & 0x7FFF;
      return true;
    }
    if (index >= small_.size()) return false;
    *compressed = small_[index] >> 7;
    *sector = small_[index] & 0x7F;
    return true;
  }

  ReadFn read_;
  uint32_t sector_ = 0x800;
  uint64_t entries_at_ = 0;
  std::vector<File> files_;
  std::vector<uint32_t> large_;
  std::vector<uint16_t> medium_;
  std::vector<uint8_t> small_;
};

Iga::ReadFn FileReader(const fs::path& path) {
  auto file = std::make_shared<std::ifstream>(path, std::ios::binary);
  return [file](uint64_t offset, size_t size, uint8_t* out) {
    file->clear();
    file->seekg(std::streamoff(offset));
    return bool(file->read(reinterpret_cast<char*>(out), std::streamsize(size)));
  };
}

Iga::ReadFn DiscReader(SourceDisc& disc, const std::string& name) {
  return [&disc, name](uint64_t offset, size_t size, uint8_t* out) { return disc.ReadPart(name, offset, size, out); };
}

// ---- Changing an archive, with the way back written first ----

// A change of one entry: data to append (stored) and point it at, or another entry of the same
// archive to point it at.
struct Change {
  size_t entry;
  std::vector<uint8_t> data;
  std::optional<size_t> other;
};

std::string Hex(uint64_t v, int digits) {
  char text[24];
  std::snprintf(text, sizeof(text), "%0*llx", digits, static_cast<unsigned long long>(v));
  return text;
}

// german_text.py's write_changes: the record {"size": <original length>, "entries": [[<offset>,
// "<old entry as hex>"], ...]} first (so an interrupted run can still be undone), then the data.
bool WriteChanges(const fs::path& path, const Iga& x, const fs::path& backup, const std::vector<Change>& changes) {
  std::error_code ec;
  const uint64_t original = fs::file_size(path, ec);
  if (ec) return false;
  std::string record = "{\"size\": " + std::to_string(original) + ", \"entries\": [";
  for (size_t k = 0; k < changes.size(); ++k) {
    const Iga::File& f = x.files()[changes[k].entry];
    record += (k ? ", [" : "[") + std::to_string(x.entries_at() + 16ull * changes[k].entry) + ", \"" + Hex(f.ordoff, 16) +
              Hex(f.length, 8) + Hex(f.block, 8) + "\"]";
  }
  record += "]}";
  {
    std::ofstream out(backup, std::ios::binary | std::ios::trunc);
    if (!out.write(record.data(), std::streamsize(record.size()))) return false;
  }
  std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
  if (!f) return false;
  uint64_t end = original;
  for (const Change& c : changes) {
    const uint64_t ordinal = x.files()[c.entry].ordoff >> 40 << 40;
    uint8_t entry[16];
    if (c.other) {
      const Iga::File& o = x.files()[*c.other];
      PutBe64(entry, ordinal | (o.ordoff & 0xFFFFFFFFFFull));
      PutBe32(entry + 8, o.length);
      PutBe32(entry + 12, o.block);
    } else {
      end = (end + x.sector() - 1) / x.sector() * x.sector();
      PutBe64(entry, ordinal | end);
      PutBe32(entry + 8, uint32_t(c.data.size()));
      PutBe32(entry + 12, kStored);
    }
    f.seekp(std::streamoff(x.entries_at() + 16ull * c.entry));
    f.write(reinterpret_cast<const char*>(entry), 16);
    if (!c.other) {
      f.seekp(std::streamoff(end));
      f.write(reinterpret_cast<const char*>(c.data.data()), std::streamsize(c.data.size()));
      end += c.data.size();
    }
  }
  return bool(f);
}

// ---- .lng text files (IGZ version 9, igLocalizedStringData) ----

struct LngField {
  size_t field;     // the position of the pointer to the text
  std::string key;  // empty for the object list's name
  std::string text;
};

std::optional<std::vector<LngField>> LngParse(const std::vector<uint8_t>& data) {
  const auto u32 = [&](size_t at) -> uint32_t { return at + 4 <= data.size() ? Be32(data.data() + at) : 0; };
  std::vector<std::pair<uint32_t, uint32_t>> sections;  // (offset, length), section 0 the fixups
  for (size_t at = 0x14; at + 12 <= data.size(); at += 16) {
    if (u32(at + 4) == 0) break;
    sections.push_back({u32(at + 4), u32(at + 8)});
  }
  if (sections.empty()) return std::nullopt;
  struct Fixup {
    uint32_t count;
    size_t at, end;
  };
  std::map<std::string, Fixup> fixups;
  size_t at = sections[0].first;
  for (uint32_t n = u32(0x10); n--;) {
    if (at + 16 > data.size()) return std::nullopt;
    std::string tag(reinterpret_cast<const char*>(data.data() + at), 4);
    std::reverse(tag.begin(), tag.end());
    const uint32_t count = u32(at + 4), length = u32(at + 8), start = u32(at + 12);
    fixups[tag] = {count, at + start, at + length};
    if (!length) return std::nullopt;
    at += length;
  }
  for (const char* need : {"TMET", "TSTR", "EXNM", "RVTB", "RSTR"}) {
    if (!fixups.count(need)) return std::nullopt;
  }
  const auto cstring = [&](size_t p) {
    std::string s;
    while (p < data.size() && data[p]) s += char(data[p++]);
    return s;
  };
  const auto strings = [&](const char* tag) {
    const Fixup& f = fixups[tag];
    std::vector<std::string> out;
    size_t p = f.at;
    while (out.size() < f.count && p < f.end) {
      out.push_back(cstring(p));
      p += out.back().size() + 1;
      if (p < f.end && data[p] == 0 && (p - f.at) % 2) ++p;  // even distances
    }
    return out;
  };
  const auto pointer = [&](uint32_t value) -> std::optional<size_t> {
    const uint32_t pool = value >> 27, offset = value & 0x07FFFFFF;
    if (pool + 1 >= sections.size()) return std::nullopt;
    return size_t(sections[pool + 1].first) + offset;
  };
  const auto positions = [&](const char* tag) {
    const Fixup& f = fixups[tag];
    std::vector<std::optional<size_t>> out;
    std::vector<uint8_t> nibbles;
    for (size_t p = f.at; p < f.end && p < data.size(); ++p) {
      nibbles.push_back(data[p] & 0xF);
      nibbles.push_back(data[p] >> 4);
    }
    uint32_t position = 0;
    size_t i = 0;
    for (uint32_t n = 0; n < f.count && i < nibbles.size(); ++n) {
      uint32_t value = 0;
      int shift = 0;
      while (i < nibbles.size()) {  // 3 bits per nibble, bit 3 set while more follow
        value |= uint32_t(nibbles[i] & 7) << shift;
        shift += 3;
        if (!(nibbles[i++] & 8)) break;
      }
      position += value * 4;
      out.push_back(pointer(position));
    }
    return out;
  };
  const std::vector<std::string> types = strings("TMET"), names = strings("TSTR");
  const Fixup& ex = fixups["EXNM"];
  std::vector<uint32_t> exnm_name;
  for (uint32_t i = 0; i < ex.count; ++i) exnm_name.push_back(u32(ex.at + 8ull * i + 4));
  std::map<size_t, std::string> keys;
  for (const auto& obj : positions("RVTB")) {
    if (!obj) continue;
    const uint32_t kind = u32(*obj);
    if (kind < types.size() && types[kind] == "igLocalizedStringData") {
      const uint32_t handle = u32(*obj + 0x10) & 0x7FFFFFFF;
      if (handle + 1 < exnm_name.size() && exnm_name[handle + 1] < names.size()) {
        keys[*obj + 8] = names[exnm_name[handle + 1]];
      }
    }
  }
  std::vector<LngField> out;
  for (const auto& field : positions("RSTR")) {
    if (!field) return std::nullopt;
    const auto text_at = pointer(u32(*field));
    if (!text_at) return std::nullopt;
    auto key = keys.find(*field);
    out.push_back({*field, key == keys.end() ? std::string() : key->second, cstring(*text_at)});
  }
  return out;
}

std::map<std::string, std::string> LngTexts(const std::vector<uint8_t>& data) {
  std::map<std::string, std::string> out;
  if (auto fields = LngParse(data)) {
    for (const LngField& f : *fields) {
      if (!f.key.empty()) out[f.key] = f.text;
    }
  }
  return out;
}

// german_text.py's rewrite_lng: the texts are the last section, written anew (each at an even
// offset, a text used twice stored once, in RSTR's order), the pointers and its length set to match.
std::optional<std::vector<uint8_t>> RewriteLng(const std::vector<uint8_t>& data,
                                               const std::map<std::string, std::string>& texts) {
  auto fields = LngParse(data);
  if (!fields) return std::nullopt;
  std::vector<size_t> descriptors;
  std::vector<std::pair<uint32_t, uint32_t>> sections;
  for (size_t at = 0x14; at + 12 <= data.size(); at += 16) {
    if (Be32(data.data() + at + 4) == 0) break;
    descriptors.push_back(at);
    sections.push_back({Be32(data.data() + at + 4), Be32(data.data() + at + 8)});
  }
  const auto [pool_at, pool_length] = sections.back();
  if (size_t(pool_at) + pool_length != data.size()) return std::nullopt;
  const uint32_t pool_index = uint32_t(sections.size() - 2);
  std::vector<uint8_t> out(data.begin(), data.begin() + pool_at);
  std::vector<uint8_t> pool;
  std::map<std::string, uint32_t> placed;
  for (const LngField& f : *fields) {
    std::string text = f.text;
    if (!f.key.empty()) {
      if (auto it = texts.find(f.key); it != texts.end()) text = it->second;
    }
    if (!placed.count(text)) {
      if (pool.size() % 2) pool.push_back(0);
      placed[text] = uint32_t(pool.size());
      pool.insert(pool.end(), text.begin(), text.end());
      pool.push_back(0);
    }
    PutBe32(out.data() + f.field, pool_index << 27 | placed[text]);
  }
  PutBe32(out.data() + descriptors.back() + 8, uint32_t(pool.size()));
  out.insert(out.end(), pool.begin(), pool.end());
  return out;
}

// ---- The steps ----

struct Job {
  fs::path archives, backup;
  const LanguageInfo* lang = nullptr;
  SourceDisc disc;
  std::function<void(const std::string&, float)> report;
  bool german = false;  // the hub's language, for the steps' names
  int texts = 0, text_archives = 0, voices = 0, voice_archives = 0, movies = 0;
};

std::vector<std::string> ArchiveNames(const fs::path& archives, const char* ending) {
  std::vector<std::string> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(archives, ec)) {
    if (!e.is_regular_file(ec)) continue;
    const std::u8string u8 = e.path().filename().u8string();
    const std::string name(u8.begin(), u8.end());
    if (EndsWith(Lower(name), ending)) out.push_back(name);
  }
  std::sort(out.begin(), out.end());
  return out;
}

// The source disc's twin of a 360 text file, lower case ("/xenon/" -> "/ps3/", "_es.lng" -> "_en.lng").
std::string TwinText(const std::string& x360_name, const LanguageInfo& lang) {
  std::string lower = Replace(Lower(x360_name), "/xenon/", "/ps3/");
  return lower.substr(0, lower.size() - std::strlen(kSpanishText)) + lang.suffix + ".lng";
}

// A voice's name without the build's encoding settings after ".wav" (they differ between the discs).
std::string WithoutEncoding(const std::string& name) {
  const size_t at = name.find(".wav,");
  return at == std::string::npos ? name : name.substr(0, at + 4);
}

std::string TwinVoice(const std::string& x360_name, const LanguageInfo& lang) {
  std::string lower = Replace(Lower(x360_name), "/xenon/", "/ps3/");
  return Replace(Replace(lower, "/spanish/", lang.voice_folder), "_es.wav", lang.cutscene_voice);
}

// One 360 archive's Spanish entries pointed at the source disc's twins.
int Patch(Job& job, const std::string& name, const std::string& source_name, bool voices) {
  const fs::path path = job.archives / Path(name);
  const fs::path backup = job.backup / Path(name + ".json");
  std::error_code ec;
  if (fs::exists(backup, ec)) return 0;  // done in an earlier run
  Iga x, p;
  if (!x.Open(FileReader(path)) || !p.Open(DiscReader(job.disc, source_name))) return 0;
  std::unordered_map<std::string, size_t> twins;
  for (size_t i = 0; i < p.files().size(); ++i) twins[WithoutEncoding(Lower(p.files()[i].name))] = i;
  std::vector<Change> changes;
  for (size_t i = 0; i < x.files().size(); ++i) {
    const std::string& fname = x.files()[i].name;
    if (!voices && !EndsWith(Lower(fname), kSpanishText)) continue;
    auto j = twins.find(WithoutEncoding(voices ? TwinVoice(fname, *job.lang) : TwinText(fname, *job.lang)));
    if (j == twins.end()) continue;
    auto data = p.Read(j->second);
    if (!data) continue;
    if (data->size() >= 0x10 && std::memcmp(data->data(), "IGZ\x01", 4) == 0 && Be32(data->data() + 0x0C) == kPlatformPs3) {
      PutBe32(data->data() + 0x0C, kPlatformXenon);
    }
    changes.push_back({i, std::move(*data), std::nullopt});
  }
  if (changes.empty() || !WriteChanges(path, x, backup, changes)) return 0;
  return int(changes.size());
}

// Files this disc has in the language itself, next to the Spanish one (the licence agreement).
int DiscOwn(Job& job) {
  int total = 0;
  for (const std::string& name : ArchiveNames(job.archives, ".pak")) {
    if (EndsWith(Lower(name), kSpanishVoices)) continue;
    const fs::path backup = job.backup / Path(name + ".disc.json");
    std::error_code ec;
    if (fs::exists(backup, ec)) continue;
    Iga x;
    if (!x.Open(FileReader(job.archives / Path(name)))) continue;
    std::vector<Change> changes;
    for (size_t i = 0; i < x.files().size(); ++i) {
      const std::string lower = Lower(x.files()[i].name);
      const size_t slash = lower.rfind('/');
      const size_t dot = lower.rfind('.');
      if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) continue;
      const std::string root = lower.substr(0, dot), ext = lower.substr(dot);
      if (ext == ".lng" || !EndsWith(root, "_es")) continue;
      if (auto j = x.Find(root.substr(0, root.size() - 3) + job.lang->suffix + ext)) changes.push_back({i, {}, *j});
    }
    if (!changes.empty() && WriteChanges(job.archives / Path(name), x, backup, changes)) {
      total += int(changes.size());
    }
  }
  return total;
}

// The 360's own texts (xenonPlatformStrings_es.lng in permanent_xenon.pak), made from the PS3
// disc's ps3/pcPlatformStrings by key. Returns how many stay as they were.
int PlatformStrings(Job& job) {
  const std::string name = "permanent_xenon.pak";
  const fs::path path = job.archives / name;
  const fs::path backup = job.backup / (name + ".platform.json");
  std::error_code ec;
  if (fs::exists(backup, ec) || !fs::exists(path, ec) || !job.disc.Has("permanent_ps3.pak")) return 0;
  Iga x, p;
  if (!x.Open(FileReader(path)) || !p.Open(DiscReader(job.disc, "permanent_ps3.pak"))) return 0;
  std::optional<size_t> i;
  for (size_t k = 0; k < x.files().size() && !i; ++k) {
    if (EndsWith(Lower(x.files()[k].name), "/xenonplatformstrings_es.lng")) i = k;
  }
  if (!i) return 0;
  auto xenon = x.Read(*i);
  if (!xenon) return 0;
  const auto source_texts = [&](const std::string& file) {
    for (size_t k = 0; k < p.files().size(); ++k) {
      if (EndsWith(Lower(p.files()[k].name), "/" + file)) {
        if (auto d = p.Read(k)) return LngTexts(*d);
      }
    }
    return std::map<std::string, std::string>();
  };
  const std::string suffix = job.lang->suffix;
  const std::pair<std::map<std::string, std::string>, std::map<std::string, std::string>> sources[] = {
      {source_texts("ps3platformstrings_es.lng"), source_texts("ps3platformstrings" + suffix + ".lng")},
      {source_texts("pcplatformstrings_es.lng"), source_texts("pcplatformstrings" + suffix + ".lng")}};
  const std::string code = job.lang->code;
  const std::map<std::string, std::string>* hand = code == "de"   ? &kXenonGerman
                                                   : code == "en" ? &kXenonEnglish
                                                   : code == "fr" ? &kXenonFrench
                                                                  : nullptr;
  std::map<std::string, std::string> texts;
  int missing = 0;
  for (const auto& [key, text] : LngTexts(*xenon)) {
    bool done = false;
    for (const auto& [their_es, their_lang] : sources) {
      auto es = their_es.find(key);
      auto other = their_lang.find(key);
      if (es != their_es.end() && es->second == text && other != their_lang.end()) {
        texts[key] = other->second;
        done = true;
        break;
      }
    }
    if (done) continue;
    if (hand) {
      if (auto h = hand->find(key); h != hand->end()) {
        texts[key] = h->second;
        continue;
      }
    }
    // Else the PS3's own wording in that language (it may name the PS3), better than Spanish.
    if (auto other = sources[0].second.find(key); other != sources[0].second.end()) {
      texts[key] = other->second;
      continue;
    }
    ++missing;
  }
  auto rewritten = RewriteLng(*xenon, texts);
  if (!rewritten) return -1;
  std::vector<Change> changes;
  changes.push_back({*i, std::move(*rewritten), std::nullopt});
  WriteChanges(path, x, backup, changes);
  return missing;
}

// ---- Movies (Bink 2): the source disc's spoken track in both of this disc's spoken slots ----

struct BinkLayout {
  std::vector<uint32_t> ids;
  size_t table = 0;  // where the frame offsets start
  std::vector<uint32_t> offsets;
};

std::optional<BinkLayout> Bink(const std::vector<uint8_t>& d) {
  if (d.size() < 48) return std::nullopt;
  const uint32_t frames = Le32(d.data() + 8), n = Le32(d.data() + 40);
  size_t at = 44 + ((d[0] == 'K' && d[1] == 'B' && d[2] == '2' && d[3] >= 'g') ? 4 : 0);
  if (n > 64 || frames > 1000000) return std::nullopt;
  at += 8ull * n;  // the largest decoded size and the sample rate / flags of each track
  if (at + 4ull * n + 4ull * (frames + 1) > d.size()) return std::nullopt;
  BinkLayout b;
  for (uint32_t i = 0; i < n; ++i) b.ids.push_back(Le32(d.data() + at + 4ull * i));
  at += 4ull * n;
  b.table = at;
  for (uint32_t i = 0; i <= frames; ++i) b.offsets.push_back(Le32(d.data() + at + 4ull * i) & ~1u);
  return b;
}

std::optional<std::vector<uint8_t>> MovieInLanguage(const std::vector<uint8_t>& x360, const std::vector<uint8_t>& ps3,
                                                    uint32_t want, std::string& why) {
  auto xb = Bink(x360), pb = Bink(ps3);
  if (!xb || !pb) return why = "not a Bink 2 movie", std::nullopt;
  if (xb->offsets.size() != pb->offsets.size()) return why = "different length", std::nullopt;
  std::vector<uint32_t> spoken;
  for (uint32_t id : xb->ids) {
    if (id) spoken.push_back(id);
  }
  std::sort(spoken.begin(), spoken.end());
  if (spoken != std::vector<uint32_t>{5, 6} || std::find(pb->ids.begin(), pb->ids.end(), want) == pb->ids.end()) {
    return why = "other tracks", std::nullopt;
  }
  const auto p_index = [&](uint32_t id) { return size_t(std::find(pb->ids.begin(), pb->ids.end(), id) - pb->ids.begin()); };
  const size_t frames = xb->offsets.size() - 1;
  std::vector<uint8_t> body;
  std::vector<uint32_t> sizes;
  uint32_t largest = 0;
  for (size_t f = 0; f < frames; ++f) {
    const auto packets_of = [&](const std::vector<uint8_t>& d, uint32_t start, size_t tracks, size_t* video_at) {
      std::vector<std::pair<uint32_t, uint32_t>> packets;  // (offset of the data, size)
      size_t at = start;
      for (size_t t = 0; t < tracks; ++t) {
        if (at + 4 > d.size()) return packets;
        const uint32_t size = Le32(d.data() + at);
        packets.push_back({uint32_t(at + 4), size});
        at += 4 + size;
      }
      *video_at = at;
      return packets;
    };
    size_t xv = 0, pv = 0;
    auto xp = packets_of(x360, xb->offsets[f], xb->ids.size(), &xv);
    auto pp = packets_of(ps3, pb->offsets[f], pb->ids.size(), &pv);
    if (xp.size() != xb->ids.size() || pp.size() != pb->ids.size() || xv > xb->offsets[f + 1]) {
      return why = "frame " + std::to_string(f) + " unreadable", std::nullopt;
    }
    const size_t start = body.size();
    for (size_t t = 0; t < xb->ids.size(); ++t) {
      const uint8_t* data = x360.data() + xp[t].first;
      uint32_t size = xp[t].second;
      if (auto same = kThisDiscSpoken.find(xb->ids[t]); same != kThisDiscSpoken.end()) {
        // Only when this disc's packet is the PS3's same-language one: the same movie.
        const auto& twin = pp[p_index(same->second)];
        if (twin.second != size || std::memcmp(ps3.data() + twin.first, data, size) != 0) {
          return why = "frame " + std::to_string(f) + " differs", std::nullopt;
        }
        const auto& wanted = pp[p_index(want)];
        data = ps3.data() + wanted.first;
        size = wanted.second;
      }
      uint8_t le[4];
      PutLe32(le, size);
      body.insert(body.end(), le, le + 4);
      body.insert(body.end(), data, data + size);
    }
    body.insert(body.end(), x360.begin() + ptrdiff_t(xv), x360.begin() + ptrdiff_t(xb->offsets[f + 1]));
    sizes.push_back(uint32_t(body.size() - start));
    largest = std::max(largest, sizes.back());
  }
  std::vector<uint8_t> out(x360.begin(), x360.begin() + ptrdiff_t(xb->table));
  uint32_t at = xb->offsets[0];  // the header's own length is unchanged
  for (size_t f = 0; f <= frames; ++f) {
    uint8_t le[4];
    PutLe32(le, at | (Le32(x360.data() + xb->table + 4 * f) & 1));
    out.insert(out.end(), le, le + 4);
    if (f < frames) at += sizes[f];
  }
  out.insert(out.end(), x360.begin() + ptrdiff_t(xb->table + 4 * (frames + 1)), x360.begin() + ptrdiff_t(xb->offsets[0]));
  out.insert(out.end(), body.begin(), body.end());
  PutLe32(out.data() + 4, uint32_t(xb->offsets[0] + body.size() - 8));
  PutLe32(out.data() + 12, largest);
  return out;
}

std::vector<std::string> SpokenMovies(const fs::path& archives) {
  std::vector<std::string> out;
  for (const std::string& name : ArchiveNames(archives, ".bk2")) {
    std::ifstream f(archives / Path(name), std::ios::binary);
    std::vector<uint8_t> head(4096);
    f.read(reinterpret_cast<char*>(head.data()), std::streamsize(head.size()));
    head.resize(size_t(f.gcount()));
    if (head.size() < 48) continue;
    const uint32_t n = Le32(head.data() + 40);
    const size_t at = 44 + ((head[0] == 'K' && head[1] == 'B' && head[2] == '2' && head[3] >= 'g') ? 4 : 0) + 8ull * n;
    if (n >= 64 || at + 4ull * n > head.size()) continue;
    for (uint32_t i = 0; i < n; ++i) {
      if (kThisDiscSpoken.count(Le32(head.data() + at + 4ull * i))) {
        out.push_back(name);
        break;
      }
    }
  }
  return out;
}

std::optional<std::vector<uint8_t>> ReadFile(const fs::path& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return std::nullopt;
  std::vector<uint8_t> d(size_t(f.tellg()));
  f.seekg(0);
  if (!d.empty() && !f.read(reinterpret_cast<char*>(d.data()), std::streamsize(d.size()))) return std::nullopt;
  return d;
}

int Movies(Job& job, std::vector<std::string>& skipped) {
  const fs::path store = job.backup / kMoviesDir;
  std::error_code ec;
  fs::create_directories(store, ec);
  int changed = 0;
  const std::vector<std::string> names = SpokenMovies(job.archives);
  for (size_t k = 0; k < names.size(); ++k) {
    const std::string& name = names[k];
    job.report((job.german ? "Filme " : "movies ") + std::to_string(k + 1) + " / " + std::to_string(names.size()), 0.85f + 0.15f * float(k) / float(names.size()));
    const fs::path target = job.archives / Path(name);
    if (fs::exists(store / Path(name), ec)) continue;  // done in an earlier run
    const std::string lower = Lower(name);
    if (!job.disc.Has(lower)) {
      skipped.push_back(name + " (not on the PS3 disc)");
      continue;
    }
    auto x360 = ReadFile(target);
    auto ps3 = job.disc.Read(lower);
    if (!x360 || !ps3) continue;
    std::string why;
    auto movie = MovieInLanguage(*x360, *ps3, job.lang->movie_track, why);
    if (!movie) {
      skipped.push_back(name + " (" + why + ")");
      continue;
    }
    fs::rename(target, store / Path(name), ec);
    if (ec) continue;
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(movie->data()), std::streamsize(movie->size()));
    ++changed;
  }
  if (fs::is_empty(store, ec)) fs::remove(store, ec);
  return changed;
}

// ---- The way back ----

// german_text.py's restore: the movies back, every archive's entries back and cut to its length.
bool Restore(const fs::path& archives, int* restored) {
  const fs::path backup = archives / kBackupDir;
  std::error_code ec;
  *restored = 0;
  if (!fs::is_directory(backup, ec)) return true;
  const fs::path store = backup / kMoviesDir;
  if (fs::is_directory(store, ec)) {
    for (const auto& e : fs::directory_iterator(store, ec)) fs::rename(e.path(), archives / e.path().filename(), ec);
    fs::remove(store, ec);
  }
  struct Record {
    uint64_t size = 0;
    std::vector<std::pair<uint64_t, std::string>> entries;
    fs::path file;
  };
  std::map<std::string, std::vector<Record>> steps;  // archive -> its records
  for (const auto& e : fs::directory_iterator(backup, ec)) {
    const std::u8string u8 = e.path().filename().u8string();
    std::string record_name(u8.begin(), u8.end());
    if (!EndsWith(record_name, ".json")) continue;
    std::string archive = record_name.substr(0, record_name.size() - 5);
    if (!EndsWith(Lower(archive), ".pak")) archive = archive.substr(0, archive.rfind('.'));  // "<archive>.<step>.json"
    auto text = ReadFile(e.path());
    if (!text) continue;
    const std::string json(text->begin(), text->end());
    Record r;
    r.file = e.path();
    const size_t size_at = json.find("\"size\"");
    if (size_at == std::string::npos) continue;
    r.size = std::strtoull(json.c_str() + json.find(':', size_at) + 1, nullptr, 10);
    for (size_t at = json.find("[[") == std::string::npos ? std::string::npos : json.find("[", json.find("\"entries\"")) + 1;
         at != std::string::npos && at < json.size();) {
      const size_t open = json.find('[', at);
      if (open == std::string::npos) break;
      char* end = nullptr;
      const uint64_t offset = std::strtoull(json.c_str() + open + 1, &end, 10);
      const size_t q1 = json.find('"', size_t(end - json.c_str()));
      const size_t q2 = q1 == std::string::npos ? q1 : json.find('"', q1 + 1);
      if (q2 == std::string::npos) break;
      r.entries.push_back({offset, json.substr(q1 + 1, q2 - q1 - 1)});
      at = q2 + 1;
    }
    steps[archive].push_back(std::move(r));
  }
  for (auto& [archive, records] : steps) {
    // The later steps first (their records start from a longer file), so an entry two steps changed
    // ends up as the disc has it.
    std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) { return a.size > b.size; });
    const fs::path path = archives / Path(archive);
    {
      std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
      if (!f) return false;
      for (const Record& r : records) {
        for (const auto& [offset, hex] : r.entries) {
          if (hex.size() != 32) continue;
          uint8_t entry[16];
          for (int i = 0; i < 16; ++i) entry[i] = uint8_t(std::stoul(hex.substr(2 * i, 2), nullptr, 16));
          f.seekp(std::streamoff(offset));
          f.write(reinterpret_cast<const char*>(entry), 16);
        }
      }
    }
    uint64_t smallest = records.front().size;
    for (const Record& r : records) smallest = std::min(smallest, r.size);
    fs::resize_file(path, smallest, ec);
    if (ec) return false;
    for (const Record& r : records) fs::remove(r.file, ec);
    ++*restored;
  }
  fs::remove(backup / kLanguageNote, ec);
  fs::remove(backup, ec);
  return true;
}

std::string LanguageName(const std::string& code, bool german) {
  for (const LanguageImport::Language& l : kLanguages) {
    if (code == l.code) return german ? l.name_de : l.name_en;
  }
  return code;
}

}  // namespace

const LanguageImport::Language* LanguageImport::Languages(size_t* count) {
  *count = std::size(kLanguages);
  return kLanguages;
}

bool IsPs3Disc(const fs::path& iso) {
  std::ifstream f(iso, std::ios::binary);
  char pvd[2048];
  f.seekg(16 * 2048);
  if (!f.read(pvd, sizeof(pvd))) return false;
  return pvd[0] == 1 && std::memcmp(pvd + 1, "CD001", 5) == 0 && std::memcmp(pvd + 40, "PS3VOLUME", 9) == 0;
}

bool IsImaginatorsPs3Disc(const fs::path& source) {
  SourceDisc disc;
  return disc.Open(source) && disc.Has("permanent_ps3.pak") && disc.Has("juicedomain_permanent.pak");
}

std::string ImportedLanguage(const fs::path& rom) {
  const fs::path backup = rom / "archives" / kBackupDir;
  std::error_code ec;
  if (!fs::is_directory(backup, ec) || fs::is_empty(backup, ec)) return {};
  if (auto note = ReadFile(backup / kLanguageNote)) {
    std::string code(note->begin(), note->end());
    while (!code.empty() && (code.back() == '\n' || code.back() == '\r' || code.back() == ' ')) code.pop_back();
    if (!code.empty()) return code;
  }
  return "?";
}

std::string LanguageImport::step() const {
  if (!shared_) return {};
  std::lock_guard lock(shared_->mutex);
  return shared_->step;
}

std::string LanguageImport::TakeSummary() {
  if (!shared_ || !shared_->done) return {};
  std::lock_guard lock(shared_->mutex);
  if (shared_->taken) return {};
  shared_->taken = true;
  return shared_->summary;
}

void LanguageImport::StartRestore(const fs::path& rom, bool german) {
  if (running()) return;
  shared_ = std::make_shared<Shared>();
  shared_->step = german ? "zur\xC3\xBC" "cksetzen" : "restoring";
  std::thread([shared = shared_, archives = rom / "archives", german] {
    int restored = 0;
    const bool ok = Restore(archives, &restored);
    std::lock_guard lock(shared->mutex);
    shared->summary = ok ? (german ? "Imaginators spricht wieder die Sprachen der Disc (" + std::to_string(restored) +
                                         " Archive zur\xC3\xBC" "ckgesetzt)."
                                   : "Imaginators speaks its disc's languages again (" + std::to_string(restored) +
                                         " archives restored).")
                         : (german ? "Zur\xC3\xBC" "cksetzen ging nicht ganz (l\xC3\xA4uft Imaginators?)."
                                   : "Couldn't restore everything (is Imaginators running?).");
    HubLog("language import: " + shared->summary);
    shared->done = true;
  }).detach();
}

void LanguageImport::Start(const fs::path& source, const fs::path& rom, const std::string& language, bool german) {
  if (running()) return;
  shared_ = std::make_shared<Shared>();
  std::thread([shared = shared_, source, rom, language, german] {
    auto job = std::make_unique<Job>();
    job->archives = rom / "archives";
    job->backup = job->archives / kBackupDir;
    job->lang = InfoOf(language);
    job->german = german;
    job->report = [shared](const std::string& step, float progress) {
      std::lock_guard lock(shared->mutex);
      shared->step = step;
      shared->progress = progress;
    };
    const auto finish = [&](const std::string& summary) {
      std::lock_guard lock(shared->mutex);
      shared->summary = summary;
      HubLog("language import: " + summary);
      shared->done = true;
    };
    std::error_code ec;
    if (!job->lang || !fs::is_directory(job->archives, ec)) {
      return finish(german ? "Imaginators hat noch keine Disc: zuerst die Xbox-360-Disc einlesen."
                           : "Imaginators has no disc yet: set its Xbox 360 disc up first.");
    }
    if (!job->disc.Open(source) || !job->disc.Has("permanent_ps3.pak")) {
      return finish(german ? "Das ist keine PS3-Disc von Imaginators." : "That isn't a PS3 disc of Imaginators.");
    }
    // A dump straight off the disc (redump's .iso) keeps the PS3's encryption: its directory reads,
    // its files don't. A decrypted image (.dec.iso, from PS3 Disc Dumper or ps3dec) or the unpacked
    // folder does.
    {
      Iga probe;
      if (!probe.Open(DiscReader(job->disc, "permanent_ps3.pak"))) {
        return finish(german ? "Diese PS3-Disc ist noch verschl\xC3\xBCsselt: nimm ein entschl\xC3\xBCsseltes Abbild "
                               "(.dec.iso) oder den entpackten Ordner."
                             : "This PS3 disc image is still encrypted: use a decrypted one (.dec.iso) or the "
                               "unpacked folder.");
      }
    }
    // Another language before: undone first (an entry is pointed at one twin only).
    const std::string before = ImportedLanguage(rom);
    if (!before.empty() && before != language) {
      job->report(german ? "die vorige Sprache wird zur\xC3\xBC" "ckgesetzt" : "undoing the previous language", 0.01f);
      int restored = 0;
      if (!Restore(job->archives, &restored)) {
        return finish(german ? "Die vorige Sprache lie\xC3\x9F sich nicht zur\xC3\xBC" "cksetzen (l\xC3\xA4uft Imaginators?)."
                             : "Couldn't undo the previous language (is Imaginators running?).");
      }
    }
    fs::create_directories(job->backup, ec);
    // Text: every archive with Spanish text.
    std::vector<std::string> text_archives;
    const std::vector<std::string> paks = ArchiveNames(job->archives, ".pak");
    for (size_t k = 0; k < paks.size(); ++k) {
      if (k % 200 == 0) job->report(german ? "Texte werden gesucht" : "looking for the texts", 0.05f * float(k) / float(paks.size()));
      if (EndsWith(Lower(paks[k]), kSpanishVoices)) continue;
      Iga x;
      if (!x.Open(FileReader(job->archives / Path(paks[k])))) continue;
      for (const Iga::File& f : x.files()) {
        if (EndsWith(Lower(f.name), kSpanishText)) {
          text_archives.push_back(paks[k]);
          break;
        }
      }
    }
    for (size_t k = 0; k < text_archives.size(); ++k) {
      job->report((german ? "Texte " : "texts ") + std::to_string(k + 1) + " / " + std::to_string(text_archives.size()),
                  0.05f + 0.45f * float(k) / float(std::max<size_t>(1, text_archives.size())));
      const std::string twin = Replace(Lower(text_archives[k]), "xenon", "ps3");
      if (!job->disc.Has(twin)) continue;
      const int n = Patch(*job, text_archives[k], twin, false);
      job->texts += n;
      job->text_archives += n ? 1 : 0;
    }
    DiscOwn(*job);
    const int platform_missing = PlatformStrings(*job);
    // Voices: every Spanish voice bank.
    const std::vector<std::string> voice_paks = ArchiveNames(job->archives, kSpanishVoices);
    for (size_t k = 0; k < voice_paks.size(); ++k) {
      job->report((german ? "Stimmen " : "voices ") + std::to_string(k + 1) + " / " + std::to_string(voice_paks.size()),
                  0.5f + 0.35f * float(k) / float(std::max<size_t>(1, voice_paks.size())));
      const std::string lower = Lower(voice_paks[k]);
      const std::string twin = lower.substr(0, lower.size() - std::strlen(kSpanishVoices)) + job->lang->suffix + ".pak";
      if (!job->disc.Has(twin)) continue;
      const int n = Patch(*job, voice_paks[k], twin, true);
      job->voices += n;
      job->voice_archives += n ? 1 : 0;
    }
    std::vector<std::string> skipped;
    job->movies = Movies(*job, skipped);
    // Which language it is, once all of it is in (an interrupted run reads as "?": undone first).
    {
      std::ofstream note(job->backup / kLanguageNote, std::ios::binary | std::ios::trunc);
      note << language << "\n";
    }
    const std::string name = LanguageName(language, german);
    // The game shows it in place of Spanish: whatever its F4 > Game > Language says but Italian.
    char text[400];
    std::snprintf(text, sizeof(text),
                  german ? "Imaginators spricht jetzt %s (%d Texte, %d Stimmen, %d Filme). Im Spiel unter F4 > Spiel > "
                           "Sprache alles au\xC3\x9F" "er Italienisch."
                         : "Imaginators speaks %s now (%d texts, %d voices, %d movies). In the game's F4 > Game > "
                           "Language, anything but Italian.",
                  name.c_str(), job->texts, job->voices, job->movies);
    HubLog("language import: " + std::to_string(job->text_archives) + " text archives, " +
           std::to_string(job->voice_archives) + " voice archives");
    for (const std::string& s : skipped) HubLog("language import: movie left as it is: " + s);
    if (platform_missing > 0) HubLog("language import: " + std::to_string(platform_missing) + " platform texts stay Spanish");
    if (!job->texts) {
      return finish(german ? "Keine Texte gefunden: passt die Disc zu diesem Imaginators?"
                           : "No texts found: does the disc match this Imaginators?");
    }
    finish(text);
  }).detach();
}

}  // namespace coreoflight
