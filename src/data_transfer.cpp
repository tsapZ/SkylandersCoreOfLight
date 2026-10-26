#include "data_transfer.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string_view>
#include <thread>

#include "hub_log.h"

namespace coreoflight {
namespace fs = std::filesystem;

namespace {

// The data folder's folders that are the player's (the six games' as the data folder names them,
// skylanders-portal data_folder.h), and its files that are. Below them, folders the games make
// again are left out: shader caches, pictures, HD texture packs.
constexpr std::string_view kDataFolders[] = {"Figures", "Backups", "Profiles", "Spyro's Adventure", "Giants",
                                             "Swap Force", "Trap Team", "SuperChargers", "Imaginators"};
constexpr std::string_view kDataFiles[] = {"coreoflight.toml", "playtime.txt", "owned.txt"};
constexpr std::string_view kLeftOut[] = {"cache", "Pictures", "Textures"};
// Inside the ZIP: says it is an export of this launcher.
constexpr char kMarker[] = "Core of Light export.txt";
constexpr char kImportedSettings[] = "Imported settings";

std::string Utf8(const fs::path& p) {
  const std::u8string u8 = p.generic_u8string();
  return std::string(u8.begin(), u8.end());
}

fs::path FromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

uint32_t Crc32(const std::vector<char>& data) {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  uint32_t crc = 0xFFFFFFFFu;
  for (char ch : data) crc = table[(crc ^ uint8_t(ch)) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

// A path past Windows' 260 characters with the prefix that lets it through (a data folder deep in
// another folder: a test's import into one lost 108 of 337 figures).
fs::path Long(const fs::path& p) {
  if (p.native().size() < 240 || !p.is_absolute() || p.native().starts_with(L"\\\\?\\")) return p;
  return fs::path(L"\\\\?\\" + p.lexically_normal().native());
}

bool ReadAll(const fs::path& file, std::vector<char>& out) {
  std::ifstream in(Long(file), std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), {});
  return !in.bad();
}

void Put16(std::string& s, uint32_t v) {
  s += char(v & 0xFF);
  s += char((v >> 8) & 0xFF);
}
void Put32(std::string& s, uint32_t v) {
  Put16(s, v & 0xFFFF);
  Put16(s, v >> 16);
}
uint32_t Get16(const unsigned char* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }
uint32_t Get32(const unsigned char* p) { return Get16(p) | Get16(p + 2) << 16; }

// A ZIP of stored (uncompressed) files, written as it goes; names in UTF-8.
class ZipWriter {
 public:
  explicit ZipWriter(const fs::path& file) : out_(file, std::ios::binary | std::ios::trunc) {}
  bool ok() const { return bool(out_); }
  bool Add(const std::string& name, const std::vector<char>& data) {
    if (entries_.size() >= 0xFFFF || offset_ + data.size() > 0xF0000000u) return false;  // no ZIP64
    // The file time as DOS keeps it: now (the import doesn't use it).
    const std::time_t now = std::time(nullptr);
    std::tm t{};
    localtime_s(&t, &now);
    const uint32_t dos_time = uint32_t(t.tm_hour << 11 | t.tm_min << 5 | t.tm_sec / 2);
    const uint32_t dos_date = uint32_t((t.tm_year - 80) << 9 | (t.tm_mon + 1) << 5 | t.tm_mday);
    const uint32_t crc = Crc32(data), size = uint32_t(data.size());
    std::string header;
    Put32(header, 0x04034B50);
    Put16(header, 20);       // version needed: 2.0
    Put16(header, 0x0800);   // the name is UTF-8
    Put16(header, 0);        // stored
    Put16(header, dos_time);
    Put16(header, dos_date);
    Put32(header, crc);
    Put32(header, size);
    Put32(header, size);
    Put16(header, uint32_t(name.size()));
    Put16(header, 0);
    header += name;
    out_.write(header.data(), std::streamsize(header.size()));
    out_.write(data.data(), std::streamsize(data.size()));
    entries_.push_back({name, crc, size, uint32_t(offset_), dos_time, dos_date});
    offset_ += header.size() + data.size();
    return bool(out_);
  }
  bool Finish() {
    std::string dir;
    for (const Entry& e : entries_) {
      Put32(dir, 0x02014B50);
      Put16(dir, 20);  // made by: 2.0
      Put16(dir, 20);
      Put16(dir, 0x0800);
      Put16(dir, 0);
      Put16(dir, e.time);
      Put16(dir, e.date);
      Put32(dir, e.crc);
      Put32(dir, e.size);
      Put32(dir, e.size);
      Put16(dir, uint32_t(e.name.size()));
      Put16(dir, 0);  // extra
      Put16(dir, 0);  // comment
      Put16(dir, 0);  // disk
      Put16(dir, 0);  // internal attributes
      Put32(dir, 0);  // external attributes
      Put32(dir, e.offset);
      dir += e.name;
    }
    std::string end;
    Put32(end, 0x06054B50);
    Put16(end, 0);
    Put16(end, 0);
    Put16(end, uint32_t(entries_.size()));
    Put16(end, uint32_t(entries_.size()));
    Put32(end, uint32_t(dir.size()));
    Put32(end, uint32_t(offset_));
    Put16(end, 0);
    out_.write(dir.data(), std::streamsize(dir.size()));
    out_.write(end.data(), std::streamsize(end.size()));
    out_.close();
    return !out_.fail();
  }
  size_t count() const { return entries_.size(); }

 private:
  struct Entry {
    std::string name;
    uint32_t crc, size, offset, time, date;
  };
  std::ofstream out_;
  std::vector<Entry> entries_;
  uint64_t offset_ = 0;
};

// Whether a path below the data folder is left out (a cache, the pictures, the HD packs).
bool LeftOut(const fs::path& relative) {
  for (const fs::path& part : relative) {
    for (const std::string_view skip : kLeftOut) {
      if (Utf8(part) == skip) return true;
    }
  }
  return false;
}

// A game's settings file next to its exe ("ssarecomp.toml").
fs::path SettingsFile(const Found& game) {
  return game.exe.parent_path() / (UserDataName(game.exe) + ".toml");
}


// A name in the ZIP as a path below a folder, refused when it would leave the folder ("..", a
// drive or a root): a ZIP from somewhere else must not write anywhere else.
bool SafeRelative(const std::string& name, fs::path& out) {
  const fs::path p = FromUtf8(name);
  if (name.empty() || p.has_root_name() || p.has_root_directory()) return false;
  for (const fs::path& part : p) {
    if (part == ".." || part == "." || Utf8(part).find(':') != std::string::npos) return false;
  }
  out = p;
  return true;
}

}  // namespace

fs::path ExportFolder() {
  auto known = [](REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path))) out = path;
    CoTaskMemFree(path);
    return out;
  };
  // --documents (a test on stand-in data): that folder, so a test never writes onto the desktop.
  if (DocumentsDir() != known(FOLDERID_Documents)) return DocumentsDir();
  return known(FOLDERID_Desktop);
}

void DataTransfer::StartExport(const std::vector<Found>& games, bool german) {
  auto shared = std::make_shared<Shared>();
  shared_ = shared;
  // Copied for the thread: the settings files of the games set up here.
  std::vector<std::pair<std::string, fs::path>> settings;
  for (const Found& g : games) {
    if (g.exe.empty()) continue;
    const fs::path file = SettingsFile(g);
    std::error_code ec;
    if (fs::is_regular_file(file, ec)) settings.emplace_back(Utf8(file.filename()), file);
  }
  const fs::path data = SkylandersDir();
  std::thread([shared, settings, data, german] {
    std::string summary;
    fs::path written;
    char date[32];
    const std::time_t now = std::time(nullptr);
    std::tm t{};
    localtime_s(&t, &now);
    std::strftime(date, sizeof(date), "%Y-%m-%d %H-%M", &t);
    const fs::path desktop = ExportFolder();
    const fs::path file = desktop / (std::string("Skylanders data ") + date + ".zip");
    const fs::path temp = fs::path(file) += ".part";
    size_t figures = 0, saves = 0;
    bool ok = !desktop.empty();
    {
      ZipWriter zip(temp);
      ok = ok && zip.ok();
      std::vector<char> bytes;
      if (ok) {
        const std::string marker = std::string("Skylanders: Core of Light export\n") + date + "\n";
        ok = zip.Add(kMarker, std::vector<char>(marker.begin(), marker.end()));
      }
      for (const std::string_view top : kDataFiles) {
        const fs::path f = data / fs::path(top);
        std::error_code ec;
        if (ok && fs::is_regular_file(f, ec) && ReadAll(f, bytes)) ok = zip.Add("Skylanders/" + std::string(top), bytes);
      }
      for (const std::string_view top : kDataFolders) {
        const fs::path dir = data / FromUtf8(std::string(top));
        std::error_code ec;
        for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
             ok && it != end; it.increment(ec)) {
          if (ec) break;
          const fs::path relative = it->path().lexically_relative(data);
          if (it->is_directory(ec)) {
            if (LeftOut(relative)) it.disable_recursion_pending();
            continue;
          }
          if (!it->is_regular_file(ec) || LeftOut(relative) || !ReadAll(it->path(), bytes)) continue;
          ok = zip.Add("Skylanders/" + Utf8(relative), bytes);
          (top == "Figures" ? figures : saves) += 1;
        }
      }
      for (const auto& [name, f] : settings) {
        if (ok && ReadAll(f, bytes)) ok = zip.Add("Settings/" + name, bytes);
      }
      ok = ok && zip.Finish();
      if (ok) {
        char line[200];
        std::snprintf(line, sizeof(line), "data export: %zu files (%zu figures, %zu other, %zu game settings)",
                      zip.count(), figures, saves, settings.size());
        HubLog(line);
      }
    }
    std::error_code ec;
    if (ok) fs::rename(temp, file, ec);
    if (!ok || ec) {
      fs::remove(temp, ec);
      summary = german ? "Der Export hat nicht geklappt (der Desktop ist nicht beschreibbar?)."
                       : "The export didn't work (can't write to the desktop?).";
      HubLog("data export: failed");
    } else {
      written = file;
      char line[300];
      const std::u8string name = file.filename().u8string();
      std::snprintf(line, sizeof(line),
                    german ? "Auf dem Desktop: %s (%zu Figuren, %zu Spielst\xC3\xA4nde und mehr, %zu Spiel-Einstellungen)."
                           : "On the desktop: %s (%zu figures, %zu saves and more, %zu games' settings).",
                    reinterpret_cast<const char*>(name.c_str()), figures, saves, settings.size());
      summary = line;
    }
    std::lock_guard lock(shared->mutex);
    shared->summary = summary;
    shared->exported = written;
    shared->done = true;
  }).detach();
}

bool DataTransfer::PlanImport(const fs::path& file, const std::vector<Found>& games, bool german, std::string& error) {
  german_ = german;
  items_.clear();
  settings_differ_.clear();
  import_file_ = file;
  std::vector<char> zip;
  if (!ReadAll(file, zip) || zip.size() < 22) {
    error = german ? "Die Datei l\xC3\xA4sst sich nicht lesen." : "Can't read the file.";
    return false;
  }
  const auto* bytes = reinterpret_cast<const unsigned char*>(zip.data());
  // The end of the central directory: its signature, searched from the end (a comment may follow).
  size_t end = std::string::npos;
  for (size_t i = zip.size() - 22; ; --i) {
    if (Get32(bytes + i) == 0x06054B50) {
      end = i;
      break;
    }
    if (i == 0 || zip.size() - i > 22 + 0xFFFF) break;
  }
  const char* not_ours = german ? "Das ist kein Export von \xE2\x80\x9EMeine Daten exportieren\xE2\x80\x9C."
                                : "That isn't a file from \"Export my data\".";
  if (end == std::string::npos) {
    error = not_ours;
    return false;
  }
  const uint32_t count = Get16(bytes + end + 10), dir_size = Get32(bytes + end + 12), dir_at = Get32(bytes + end + 16);
  if (uint64_t(dir_at) + dir_size > end) {
    error = not_ours;
    return false;
  }
  // Where each game's settings file goes: next to its exe when it is set up here.
  auto settings_target = [&](const std::string& name, std::string& game) {
    for (size_t i = 0; i < games.size() && i < AllGames().size(); ++i) {
      if (games[i].exe.empty() || Utf8(SettingsFile(games[i]).filename()) != name) continue;
      game = german ? AllGames()[i].name_de : AllGames()[i].name_en;
      return SettingsFile(games[i]);
    }
    game.clear();
    return SkylandersDir() / kImportedSettings / FromUtf8(name);
  };
  bool marker = false;
  size_t at = dir_at;
  for (uint32_t i = 0; i < count; ++i) {
    if (at + 46 > end || Get32(bytes + at) != 0x02014B50) {
      error = not_ours;
      return false;
    }
    const uint32_t method = Get16(bytes + at + 10), crc = Get32(bytes + at + 16), size = Get32(bytes + at + 20),
                   usize = Get32(bytes + at + 24), name_len = Get16(bytes + at + 28),
                   extra_len = Get16(bytes + at + 30), comment_len = Get16(bytes + at + 32),
                   offset = Get32(bytes + at + 42);
    if (at + 46 + name_len > end) {
      error = not_ours;
      return false;
    }
    const std::string name(zip.data() + at + 46, name_len);
    at += 46 + name_len + extra_len + comment_len;
    if (name == kMarker) {
      marker = true;
      continue;
    }
    if (name.empty() || name.back() == '/') continue;  // a folder entry (a ZIP packed again elsewhere)
    if (method != 0 || size != usize) {
      // Packed again with compression (by Windows or another tool): this launcher only reads its own.
      error = german ? "Die Datei wurde neu gepackt; nimm bitte die Datei, wie der Export sie gemacht hat."
                     : "The file was packed again; please take it as the export made it.";
      return false;
    }
    Item item;
    item.name = name;
    item.offset = offset;
    item.size = size;
    item.crc = crc;
    fs::path relative;
    if (name.starts_with("Skylanders/") && SafeRelative(name.substr(11), relative)) {
      item.target = SkylandersDir() / relative;
    } else if (name.starts_with("Settings/") && SafeRelative(name.substr(9), relative) &&
               !relative.has_parent_path() && relative.extension() == ".toml") {
      item.settings = true;
      item.target = settings_target(Utf8(relative), item.game);
    } else {
      continue;  // not one of the export's files
    }
    items_.push_back(std::move(item));
  }
  if (!marker) {
    error = not_ours;
    items_.clear();
    return false;
  }
  // The games whose settings file is there and differs (the question before the import).
  for (const Item& item : items_) {
    if (!item.settings || item.game.empty()) continue;
    std::vector<char> there;
    if (!ReadAll(item.target, there)) continue;
    if (uint64_t(item.offset) + 30 > zip.size()) continue;
    const uint32_t data_at = item.offset + 30 + Get16(bytes + item.offset + 26) + Get16(bytes + item.offset + 28);
    if (uint64_t(data_at) + item.size > zip.size()) continue;
    if (there.size() != item.size || !std::equal(there.begin(), there.end(), zip.begin() + data_at)) {
      settings_differ_.push_back(item.game);
    }
  }
  return true;
}

void DataTransfer::StartImport(bool replace_settings) {
  auto shared = std::make_shared<Shared>();
  shared_ = shared;
  const auto items = items_;
  const fs::path file = import_file_;
  const bool german = german_;
  std::thread([shared, items, file, german, replace_settings] {
    std::vector<char> zip;
    size_t added = 0, same = 0, replaced = 0, waiting = 0;
    std::vector<std::string> kept, broken;
    if (ReadAll(file, zip)) {
      const auto* bytes = reinterpret_cast<const unsigned char*>(zip.data());
      for (const Item& item : items) {
        if (uint64_t(item.offset) + 30 > zip.size() || Get32(bytes + item.offset) != 0x04034B50) {
          broken.push_back(item.name);
          continue;
        }
        const uint32_t data_at = item.offset + 30 + Get16(bytes + item.offset + 26) + Get16(bytes + item.offset + 28);
        if (uint64_t(data_at) + item.size > zip.size()) {
          broken.push_back(item.name);
          continue;
        }
        std::vector<char> data(zip.begin() + data_at, zip.begin() + data_at + item.size);
        if (Crc32(data) != item.crc) {
          broken.push_back(item.name);
          continue;
        }
        std::vector<char> there;
        std::error_code ec;
        const fs::path target = Long(item.target);
        const bool exists = fs::exists(target, ec);
        if (exists && ReadAll(item.target, there) && there == data) {
          ++same;
          continue;
        }
        if (exists && !(item.settings && replace_settings && !item.game.empty())) {
          kept.push_back(item.settings && !item.game.empty() ? item.game : Utf8(item.target.filename()));
          continue;
        }
        fs::create_directories(target.parent_path(), ec);
        const fs::path temp = fs::path(target) += ".import";
        {
          std::ofstream out(temp, std::ios::binary | std::ios::trunc);
          out.write(data.data(), std::streamsize(data.size()));
          if (!out) {
            broken.push_back(item.name);
            continue;
          }
        }
        if (exists) {
          // A game's settings, replaced as asked: the old one kept beside it.
          fs::copy_file(target, fs::path(target) += ".before-import", fs::copy_options::overwrite_existing, ec);
          ++replaced;
        } else if (item.settings && item.game.empty()) {
          ++waiting;
        } else {
          ++added;
        }
        fs::rename(temp, target, ec);
        if (ec) {
          fs::remove(temp, ec);
          broken.push_back(item.name);
        }
      }
    } else {
      broken.push_back(Utf8(file.filename()));
    }
    // Short: the toast is one line. The numbers that are 0 are left out.
    char line[400];
    std::snprintf(line, sizeof(line), german ? "Import: %zu neu, %zu schon da." : "Import: %zu new, %zu there already.",
                  added, same);
    std::string summary = line;
    if (replaced) {
      std::snprintf(line, sizeof(line), german ? " %zu Spiel-Einstellungen ersetzt." : " %zu games' settings replaced.",
                    replaced);
      summary += line;
    }
    if (!kept.empty()) {
      summary += german ? " Behalten (deins ist anders): " : " Kept (yours differ): ";
      for (size_t i = 0; i < kept.size() && i < 3; ++i) summary += (i ? ", " : "") + kept[i];
      if (kept.size() > 3) summary += ", ...";
      summary += ".";
    }
    if (waiting) {
      summary += german ? " Einstellungen nicht eingerichteter Spiele warten, bis sie eingerichtet sind: dann noch einmal importieren."
                        : " Settings of games not set up yet wait until they are: then import again.";
    }
    if (!broken.empty()) {
      summary += german ? " Nicht lesbar: " : " Couldn't read: ";
      summary += std::to_string(broken.size());
      summary += german ? " Dateien." : " files.";
    }
    char log[200];
    std::snprintf(log, sizeof(log), "data import: %zu new, %zu same, %zu settings replaced, %zu kept, %zu waiting, %zu broken",
                  added, same, replaced, kept.size(), waiting, broken.size());
    HubLog(log);
    std::lock_guard lock(shared->mutex);
    shared->summary = summary;
    shared->done = true;
  }).detach();
}

std::string DataTransfer::TakeSummary() {
  if (!shared_ || !shared_->done) return {};
  std::lock_guard lock(shared_->mutex);
  if (shared_->taken) return {};
  shared_->taken = true;
  return shared_->summary;
}

fs::path DataTransfer::TakeExported() {
  if (!shared_ || !shared_->done) return {};
  std::lock_guard lock(shared_->mutex);
  if (shared_->exported_taken) return {};
  shared_->exported_taken = true;
  return shared_->exported;
}

}  // namespace coreoflight
