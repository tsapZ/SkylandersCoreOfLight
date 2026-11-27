#include "data_transfer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string_view>
#include <thread>

#include "crc32.h"
#include "hub_log.h"
#include "platform.h"
#include "portal/data_folder.h"
#include "profiles.h"
#include "saves.h"

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
// A one-game file's marker line naming its game (Game::key), and the folder every save of a game is in
// (saves.h: <user data>\B13EBABEBABEBABE\<title id>\...): a "Save/" name must start there.
constexpr std::string_view kGameLine = "game: ";
constexpr char kSaveRoot[] = "B13EBABEBABEBABE";

std::string Utf8(const fs::path& p) {
  const std::u8string u8 = p.generic_u8string();
  return std::string(u8.begin(), u8.end());
}

fs::path FromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

uint32_t Crc32(const std::vector<char>& data) { return coreoflight::Crc32(data.data(), data.size()); }

// A path past Windows' 260 characters with the prefix that lets it through (a data folder deep in
// another folder: a test's import into one lost 108 of 337 figures).
fs::path Long(const fs::path& p) {
#ifdef _WIN32
  if (p.native().size() < 240 || !p.is_absolute() || p.native().starts_with(L"\\\\?\\")) return p;
  return fs::path(L"\\\\?\\" + p.lexically_normal().native());
#else
  return p;
#endif
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
    const std::tm t = ToLocalTm(std::time(nullptr));
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
  const fs::path docs = skylanders::DocumentsFolder();
  // --documents (a test on stand-in data): that folder, so a test never writes onto the desktop.
  if (DocumentsDir() != docs) return DocumentsDir();
  return DesktopDir();
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
    const std::tm t = ToLocalTm(std::time(nullptr));
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
      HubWarn("data export: failed");
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

void DataTransfer::StartExportGame(size_t game, const Found& found, bool figures, bool german) {
  auto shared = std::make_shared<Shared>();
  shared_ = shared;
  const Profile profile = ActiveProfile();
  const Game info = AllGames()[game];
  const fs::path exe = found.exe;
  const fs::path user_data = profile.root / GameFolderName(exe);
  const fs::path figures_dir = figures ? ProfileFiguresDir(profile) : fs::path();
  std::thread([shared, profile, info, exe, user_data, figures_dir, german] {
    std::string summary;
    fs::path written;
    char date[32];
    const std::tm t = ToLocalTm(std::time(nullptr));
    std::strftime(date, sizeof(date), "%Y-%m-%d %H-%M", &t);
    const fs::path folder = ExportFolder();
    const fs::path file = folder / FromUtf8(std::string("Skylanders ") + info.name_en + " saves " + date + ".zip");
    const fs::path temp = fs::path(file) += ".part";
    const std::vector<SaveSlot> slots = ListSaves(profile, exe);
    const bool none = std::none_of(slots.begin(), slots.end(), [](const SaveSlot& s) { return !s.options; });
    size_t saves = 0, figure_files = 0;
    bool ok = !folder.empty() && !none;
    {
      ZipWriter zip(temp);
      ok = ok && zip.ok();
      std::vector<char> bytes;
      if (ok) {
        const std::string marker = std::string("Skylanders: Core of Light export\n") + date + "\n" +
                                   std::string(kGameLine) + info.key + "\n";
        ok = zip.Add(kMarker, std::vector<char>(marker.begin(), marker.end()));
      }
      // Each save folder's files and its header, named by their place in the game's user data. A file
      // that can't be read stops the export: half a save would be worse than none.
      for (const SaveSlot& slot : slots) {
        if (!ok) break;
        if (slot.options) continue;  // the game's options belong to the PC it runs on
        std::error_code ec;
        for (fs::recursive_directory_iterator it(slot.folder, ec), end; ok && it != end; it.increment(ec)) {
          if (ec) break;
          if (!it->is_regular_file(ec)) continue;
          ok = ReadAll(it->path(), bytes) && zip.Add("Save/" + Utf8(it->path().lexically_relative(user_data)), bytes);
        }
        ok = ok && !ec;
        if (ok && fs::is_regular_file(slot.header, ec)) {
          ok = ReadAll(slot.header, bytes) && zip.Add("Save/" + Utf8(slot.header.lexically_relative(user_data)), bytes);
        }
        ++saves;
      }
      if (ok && saves && !figures_dir.empty()) {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(figures_dir, fs::directory_options::skip_permission_denied, ec), end;
             ok && it != end; it.increment(ec)) {
          if (ec) break;
          if (!it->is_regular_file(ec) || !ReadAll(it->path(), bytes)) continue;
          ok = zip.Add("Figures/" + Utf8(it->path().lexically_relative(figures_dir)), bytes);
          ++figure_files;
        }
      }
      ok = ok && saves && zip.Finish();
    }
    std::error_code ec;
    if (ok) fs::rename(temp, file, ec);
    if (!ok || ec) {
      fs::remove(temp, ec);
      if (none) {
        summary = std::string(german ? info.name_de : info.name_en) +
                  (german ? " hat noch keine Spielst\xC3\xA4nde." : " has no saves yet.");
      } else {
        summary = german ? "Der Export hat nicht geklappt (der Desktop ist nicht beschreibbar?)."
                         : "The export didn't work (can't write to the desktop?).";
        HubWarn("data export of one game: failed");
      }
    } else {
      written = file;
      char line[400];
      const std::u8string name = file.filename().u8string();
      std::snprintf(line, sizeof(line),
                    german ? "Auf dem Desktop: %s (%zu Spielst\xC3\xA4nde%s)."
                           : "On the desktop: %s (%zu saves%s).",
                    reinterpret_cast<const char*>(name.c_str()), saves,
                    figure_files ? (german ? " und deine Figuren" : " and your figures") : "");
      summary = line;
      std::snprintf(line, sizeof(line), "data export of one game: %s, %zu saves, %zu figure files", info.key, saves,
                    figure_files);
      HubLog(line);
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
  one_game_ = -1;
  one_game_saves_ = one_game_saves_here_ = 0;
  one_game_exe_.clear();
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
  // A stored file's bytes, from its local header (empty when it doesn't fit in the file).
  auto stored = [&](uint32_t offset, uint32_t size) {
    if (uint64_t(offset) + 30 > zip.size() || Get32(bytes + offset) != 0x04034B50) return std::string();
    const uint64_t data_at = uint64_t(offset) + 30 + Get16(bytes + offset + 26) + Get16(bytes + offset + 28);
    if (data_at + size > zip.size()) return std::string();
    return std::string(zip.data() + data_at, size);
  };
  bool marker = false;
  std::string game_key;  // a one-game file's game (its marker's "game: " line)
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
      const std::string text = stored(offset, size);
      if (const size_t line = text.find("\n" + std::string(kGameLine)); line != std::string::npos) {
        const size_t from = line + 1 + kGameLine.size();
        game_key = text.substr(from, text.find('\n', from) - from);
      }
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
    } else if (name.starts_with("Save/") && SafeRelative(name.substr(5), relative) &&
               Utf8(*relative.begin()) == kSaveRoot) {
      // A one-game file's save file: its place below the game's user data, set once the game is known.
      item.save = true;
      item.target = relative;
    } else if (name.starts_with("Figures/") && SafeRelative(name.substr(8), relative)) {
      item.target = ProfileFiguresDir(ActiveProfile()) / relative;
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
  // A one-game file: its game must be set up here (its saves go into that game's user data).
  if (!game_key.empty()) {
    for (size_t i = 0; i < AllGames().size() && i < games.size(); ++i) {
      if (game_key == AllGames()[i].key) one_game_ = int(i);
    }
    if (one_game_ < 0) {
      error = not_ours;
      items_.clear();
      return false;
    }
    const Game& g = AllGames()[size_t(one_game_)];
    if (games[size_t(one_game_)].exe.empty()) {
      error = std::string(german ? "Richte zuerst " : "Set up ") + (german ? g.name_de : g.name_en) +
              (german ? " ein, dann importiere seine Spielst\xC3\xA4nde." : " first, then import its saves.");
      items_.clear();
      one_game_ = -1;
      return false;
    }
    one_game_exe_ = games[size_t(one_game_)].exe;
    const Profile profile = ActiveProfile();
    const fs::path user_data = profile.root / GameFolderName(one_game_exe_);
    std::vector<std::string> in_file;  // the save folders in the file: <title id>/00000001/<save>
    for (Item& item : items_) {
      if (!item.save) continue;
      std::vector<std::string> parts;
      for (const fs::path& part : item.target) parts.push_back(Utf8(part));
      if (parts.size() >= 4 && parts[2] == "00000001") {
        const std::string key = parts[1] + "/" + parts[3];
        if (std::find(in_file.begin(), in_file.end(), key) == in_file.end()) in_file.push_back(key);
      }
      item.target = user_data / item.target;
    }
    one_game_saves_ = in_file.size();
    for (const SaveSlot& slot : ListSaves(profile, one_game_exe_)) one_game_saves_here_ += slot.options ? 0 : 1;
    if (one_game_saves_ == 0) {
      error = german ? "In der Datei ist kein Spielstand." : "There's no save in the file.";
      items_.clear();
      one_game_ = -1;
      return false;
    }
  } else if (std::any_of(items_.begin(), items_.end(), [](const Item& item) { return item.save; })) {
    // "Save/" files without the game line (only a one-game file has them): not taken as a whole export,
    // which would leave the saves out and bring the figures in without the question (code review t18).
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
  const int one_game = one_game_;
  const size_t one_game_saves = one_game_saves_;
  const fs::path one_game_exe = one_game_exe_;
  const Profile profile = ActiveProfile();
  std::thread([shared, items, file, german, replace_settings, one_game, one_game_saves, one_game_exe, profile] {
    std::vector<char> zip;
    size_t added = 0, same = 0, replaced = 0, waiting = 0, backed_up = 0, save_files = 0;
    std::vector<std::string> kept, broken;
    auto stop = [&](const std::string& log, const char* en, const char* de) {
      HubWarn("data import of one game: " + log + ", nothing changed");
      std::lock_guard lock(shared->mutex);
      shared->summary = german ? de : en;
      shared->done = true;
    };
    // A one-game file (#161): its saves replace that game's saves here, all or nothing (code review t18,
    // 2026-11-26: removing the old ones first and writing after could leave the game with none). In turn:
    //  1. every entry of the file read and its CRC checked (a damaged file changes nothing);
    //  2. each save here copied into the backups (one that fails stops it; RT on the card puts one back);
    //  3. the file's saves written into "import-new" beside them (a full disk stops it here);
    //  4. the swap: the saves here moved into "import-old", the new ones moved in. A move that fails (a save
    //     in use, the game started some other way) is undone with every move before it;
    //  5. "import-old" and "import-new" deleted.
    if (one_game >= 0) {
      const Game& game = AllGames()[size_t(one_game)];
      const std::vector<SaveSlot> slots = ListSaves(profile, one_game_exe);
      const fs::path user_data = profile.root / GameFolderName(one_game_exe);
      const fs::path stage = user_data / "import-new", holding = user_data / "import-old";
      auto entry = [&](const Item& item) -> const char* {  // an entry's bytes in `zip`, nullptr if it doesn't fit
        const auto* bytes = reinterpret_cast<const unsigned char*>(zip.data());
        if (uint64_t(item.offset) + 30 > zip.size() || Get32(bytes + item.offset) != 0x04034B50) return nullptr;
        const uint64_t data_at = uint64_t(item.offset) + 30 + Get16(bytes + item.offset + 26) +
                                 Get16(bytes + item.offset + 28);
        return data_at + item.size <= zip.size() ? zip.data() + data_at : nullptr;
      };
      bool readable = ReadAll(file, zip);
      for (const Item& item : items) {
        if (!readable) break;
        const char* data = entry(item);
        readable = data != nullptr && coreoflight::Crc32(data, item.size) == item.crc;
      }
      if (!readable) {
        stop("the file is damaged", "Nothing imported: the file is damaged.",
             "Nichts importiert: Die Datei ist besch\xC3\xA4" "digt.");
        return;
      }
      for (const SaveSlot& slot : slots) {
        if (slot.options) continue;
        if (!BackupSave(profile, game, slot, "before-import")) {
          stop("a backup failed (" + LastSaveError() + ")",
               "Nothing imported: your saves couldn't be backed up first. Is the game running? Close it and try again.",
               "Nichts importiert: Deine Spielst\xC3\xA4nde lie\xC3\x9F" "en sich nicht erst sichern. L\xC3\xA4uft das "
               "Spiel? Schlie\xC3\x9F" "e es und versuch es noch einmal.");
          return;
        }
        ++backed_up;
      }
      std::error_code ec;
      fs::remove_all(Long(stage), ec);  // left by an import that was cut off
      fs::remove_all(Long(holding), ec);
      // The units the swap moves: each save folder (<root>/<title>/00000001/<save>) and each header file,
      // relative to the user data.
      std::vector<fs::path> units;
      bool staged = true;
      for (const Item& item : items) {
        if (!staged) break;
        if (!item.save) continue;
        const fs::path rel = item.target.lexically_relative(user_data);
        std::vector<fs::path> parts(rel.begin(), rel.end());
        fs::path unit = rel;
        if (parts.size() >= 5 && parts[2] == "00000001") unit = parts[0] / parts[1] / parts[2] / parts[3];
        if (std::find(units.begin(), units.end(), unit) == units.end()) units.push_back(unit);
        const fs::path to = stage / rel;
        fs::create_directories(Long(to.parent_path()), ec);
        std::ofstream out(Long(to), std::ios::binary | std::ios::trunc);
        out.write(entry(item), std::streamsize(item.size));
        out.close();
        staged = !ec && !out.fail();
        ++save_files;
      }
      if (!staged) {
        fs::remove_all(Long(stage), ec);
        stop("the file's saves couldn't be written", "Nothing imported: the saves couldn't be written (is the disk full?).",
             "Nichts importiert: Die Spielst\xC3\xA4nde lie\xC3\x9F" "en sich nicht schreiben (Platte voll?).");
        return;
      }
      std::vector<std::pair<fs::path, fs::path>> moved;  // from -> to, undone backwards on a failure
      auto move = [&](const fs::path& from, const fs::path& to) {
        std::error_code e;
        fs::create_directories(Long(to.parent_path()), e);
        if (fs::exists(Long(to), e)) return false;  // never onto something (an options save of that name)
        fs::rename(Long(from), Long(to), e);
        if (e) return false;
        moved.emplace_back(from, to);
        return true;
      };
      bool swapped = true;
      for (const SaveSlot& slot : slots) {
        if (!swapped) break;
        if (slot.options) continue;
        swapped = move(slot.folder, holding / slot.folder.lexically_relative(user_data));
        if (swapped && fs::exists(Long(slot.header), ec)) {
          swapped = move(slot.header, holding / slot.header.lexically_relative(user_data));
        }
      }
      for (const fs::path& unit : units) {
        if (!swapped) break;
        swapped = move(stage / unit, user_data / unit);
      }
      if (!swapped) {
        bool undone = true;
        for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
          std::error_code e;
          fs::rename(Long(it->second), Long(it->first), e);
          undone = undone && !e;
        }
        fs::remove_all(Long(stage), ec);
        // A move back that failed leaves its save in "import-old" (and in the backups): not deleted.
        if (undone) fs::remove_all(Long(holding), ec);
        else HubWarn("data import of one game: a save couldn't be moved back, it stays in " + Utf8(holding));
        stop("a save couldn't be moved (in use?)",
             "Nothing imported: the game's saves are in use. Close the game and try again.",
             "Nichts importiert: Die Spielst\xC3\xA4nde sind in Benutzung. Schlie\xC3\x9F" "e das Spiel und versuch es noch einmal.");
        return;
      }
      fs::remove_all(Long(holding), ec);
      fs::remove_all(Long(stage), ec);
      zip.clear();
    }
    if (ReadAll(file, zip)) {
      const auto* bytes = reinterpret_cast<const unsigned char*>(zip.data());
      for (const Item& item : items) {
        if (item.save) continue;  // a one-game file's saves: swapped in above
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
    std::string summary;
    if (one_game >= 0) {
      // "Spyro's Adventure: the file's saves are in (2); yours are in the backups (3, RT on its card)."
      const Game& game = AllGames()[size_t(one_game)];
      std::snprintf(line, sizeof(line),
                    german ? "%s: Die Spielst\xC3\xA4nde aus der Datei sind drin (%zu)."
                           : "%s: the file's saves are in (%zu).",
                    german ? game.name_de : game.name_en, one_game_saves);
      summary = line;
      if (backed_up) {
        std::snprintf(line, sizeof(line),
                      german ? " Deine bisherigen sind in den Kopien (%zu, RT auf der Karte)."
                             : " Yours are in the copies (%zu, RT on its card).",
                      backed_up);
        summary += line;
      }
      if (added || same) {
        std::snprintf(line, sizeof(line), german ? " Figuren: %zu neu, %zu schon da." : " Figures: %zu new, %zu there already.",
                      added, same);
        summary += line;
      }
    } else {
      std::snprintf(line, sizeof(line), german ? "Import: %zu neu, %zu schon da." : "Import: %zu new, %zu there already.",
                    added, same);
      summary = line;
    }
    if (replaced) {
      if (replaced == 1) {
        std::snprintf(line, sizeof(line), "%s", german ? " Einstellungen von 1 Spiel ersetzt." : " One game's settings replaced.");
      } else {
        std::snprintf(line, sizeof(line), german ? " Einstellungen von %zu Spielen ersetzt." : " %zu games' settings replaced.",
                      replaced);
      }
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
    char log[320];
    std::snprintf(log, sizeof(log),
                  "data import%s%s: %zu new, %zu same, %zu settings replaced, %zu kept, %zu waiting, %zu broken, "
                  "%zu save files, %zu saves backed up first",
                  one_game >= 0 ? " of one game: " : "", one_game >= 0 ? AllGames()[size_t(one_game)].key : "", added,
                  same, replaced, kept.size(), waiting, broken.size(), save_files, backed_up);
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
