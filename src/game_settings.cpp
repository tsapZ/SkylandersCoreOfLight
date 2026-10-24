#include "game_settings.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <thread>
#include <utility>

#include "games.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;

// The little JSON the games write (settings_schema.h): objects, arrays, strings, true / false.
struct Json {
  enum class Kind { kNull, kBool, kNumber, kString, kArray, kObject } kind = Kind::kNull;
  bool b = false;
  double number = 0;
  std::string text;
  std::vector<Json> items;
  std::vector<std::pair<std::string, Json>> members;

  const Json* Get(const std::string& key) const {
    for (const auto& [k, v] : members) {
      if (k == key) return &v;
    }
    return nullptr;
  }
  std::string Str(const std::string& key) const {
    const Json* v = Get(key);
    return v && v->kind == Kind::kString ? v->text : std::string();
  }
  bool Bool(const std::string& key) const {
    const Json* v = Get(key);
    return v && v->kind == Kind::kBool && v->b;
  }
};

class Parser {
 public:
  explicit Parser(const std::string& s) : s_(s) {}

  std::optional<Json> Parse() {
    auto v = Value();
    Skip();
    if (!v || pos_ != s_.size()) return std::nullopt;
    return v;
  }

 private:
  void Skip() {
    while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t')) {
      ++pos_;
    }
  }
  bool Eat(char c) {
    Skip();
    if (pos_ < s_.size() && s_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }
  bool Word(const char* w) {
    const std::string_view word(w);
    if (s_.compare(pos_, word.size(), word) != 0) return false;
    pos_ += word.size();
    return true;
  }
  std::optional<std::string> String() {
    if (!Eat('"')) return std::nullopt;
    std::string out;
    while (pos_ < s_.size()) {
      const char c = s_[pos_++];
      if (c == '"') return out;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (pos_ >= s_.size()) return std::nullopt;
      const char e = s_[pos_++];
      switch (e) {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          // Only control characters come escaped (the games write UTF-8 as it is).
          if (pos_ + 4 > s_.size()) return std::nullopt;
          const unsigned code = unsigned(std::strtoul(s_.substr(pos_, 4).c_str(), nullptr, 16));
          pos_ += 4;
          if (code < 0x80) out += char(code);
          break;
        }
        default: out += e; break;  // \" \\ \/
      }
    }
    return std::nullopt;
  }
  std::optional<Json> Value() {
    Skip();
    if (pos_ >= s_.size()) return std::nullopt;
    Json v;
    const char c = s_[pos_];
    if (c == '"') {
      auto text = String();
      if (!text) return std::nullopt;
      v.kind = Json::Kind::kString;
      v.text = std::move(*text);
    } else if (c == '{') {
      ++pos_;
      v.kind = Json::Kind::kObject;
      if (Eat('}')) return v;
      do {
        auto key = String();
        if (!key || !Eat(':')) return std::nullopt;
        auto item = Value();
        if (!item) return std::nullopt;
        v.members.emplace_back(std::move(*key), std::move(*item));
      } while (Eat(','));
      if (!Eat('}')) return std::nullopt;
    } else if (c == '[') {
      ++pos_;
      v.kind = Json::Kind::kArray;
      if (Eat(']')) return v;
      do {
        auto item = Value();
        if (!item) return std::nullopt;
        v.items.push_back(std::move(*item));
      } while (Eat(','));
      if (!Eat(']')) return std::nullopt;
    } else if (Word("true")) {
      v.kind = Json::Kind::kBool;
      v.b = true;
    } else if (Word("false")) {
      v.kind = Json::Kind::kBool;
    } else if (Word("null")) {
    } else {
      char* end = nullptr;
      v.kind = Json::Kind::kNumber;
      v.number = std::strtod(s_.c_str() + pos_, &end);
      if (end == s_.c_str() + pos_) return std::nullopt;
      pos_ = size_t(end - s_.c_str());
    }
    return v;
  }

  const std::string& s_;
  size_t pos_ = 0;
};

fs::path Utf8Path(const std::string& s) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

// Whether a file contains `text` (read in pieces, overlapping by the text's length).
bool HasText(const fs::path& file, std::string_view text) {
  std::ifstream in(file, std::ios::binary);
  std::string buffer(size_t(4) << 20, '\0');
  size_t keep = 0;
  while (in) {
    in.read(buffer.data() + keep, std::streamsize(buffer.size() - keep));
    const size_t n = keep + size_t(in.gcount());
    if (std::string_view(buffer.data(), n).find(text) != std::string_view::npos) return true;
    keep = std::min(text.size(), n);
    std::copy(buffer.begin() + ptrdiff_t(n - keep), buffer.begin() + ptrdiff_t(n), buffer.begin());
  }
  return false;
}

std::shared_ptr<GameSettings> FromJson(const Json& root) {
  auto out = std::make_shared<GameSettings>();
  out->config_file = Utf8Path(root.Str("config_file"));
  const Json* pages = root.Get("pages");
  if (!pages || out->config_file.empty()) return nullptr;
  for (const Json& p : pages->items) {
    GameSettingsPage page{p.Str("en"), p.Str("de"), {}};
    const Json* settings = p.Get("settings");
    if (!settings) continue;
    for (const Json& s : settings->items) {
      GameSetting g;
      g.cvar = s.Str("cvar");
      g.en = s.Str("en");
      g.de = s.Str("de");
      g.help_en = s.Str("help_en");
      g.help_de = s.Str("help_de");
      g.type = s.Str("type");
      g.category = s.Str("category");
      g.value = s.Bool("preset") ? s.Str("current") : s.Str("value");
      g.restart = s.Bool("restart");
      g.buttons = s.Bool("buttons");
      g.enabled_by = s.Str("enabled_by");
      // Games built before the schema named it: the keyboard's player means something only with
      // the keyboard on (a colleague's wish, 2026-10-23).
      if (g.enabled_by.empty() && g.cvar == "keyboard_player") g.enabled_by = "mnk_mode";
      if (const Json* options = s.Get("options")) {
        for (const Json& o : options->items) g.options.push_back({o.Str("value"), o.Str("en"), o.Str("de")});
      }
      if (const Json* presets = s.Get("presets")) {
        for (const auto& [option, list] : presets->members) {
          auto& assignments = g.presets[option];
          for (const Json& a : list.items) {
            if (a.items.size() < 4) continue;
            assignments.push_back({a.items[0].text, a.items[1].text, a.items[2].text, a.items[3].text});
          }
        }
      }
      if (g.cvar.empty() && g.presets.empty()) continue;
      page.settings.push_back(std::move(g));
    }
    if (!page.settings.empty()) out->pages.push_back(std::move(page));
  }
  return out->pages.empty() ? nullptr : out;
}

std::shared_ptr<GameSettings> Load(const fs::path& exe, const fs::path& workdir, const std::string& key) {
  wchar_t temp[MAX_PATH] = L"";
  GetTempPathW(MAX_PATH, temp);
  const fs::path dir = fs::path(temp) / L"CoreOfLight";
  std::error_code ec;
  fs::create_directories(dir, ec);
  const fs::path schema = dir / (key + "_settings.json");
  fs::remove(schema, ec);
  // A build from before --settings_schema would start the whole game instead: only an exe that
  // has the setting's name in it is asked.
  if (!HasText(exe, "settings_schema")) return nullptr;
  std::wstring command = L"\"" + exe.wstring() + L"\" \"--settings_schema=" + schema.wstring() + L"\"";
  STARTUPINFOW startup = {sizeof(startup)};
  PROCESS_INFORMATION info = {};
  if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                      workdir.c_str(), &startup, &info)) {
    return nullptr;
  }
  CloseHandle(info.hThread);
  // It quits by itself once it has written the file; a game too old for --settings_schema would
  // start for real, so it is stopped after a while.
  if (WaitForSingleObject(info.hProcess, 30000) != WAIT_OBJECT_0) TerminateProcess(info.hProcess, 1);
  CloseHandle(info.hProcess);
  std::ifstream in(schema, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (text.empty()) return nullptr;
  auto json = Parser(text).Parse();
  std::shared_ptr<GameSettings> out = json ? FromJson(*json) : nullptr;
  if (!out) return nullptr;
  // A button of the launcher's own at the end of the graphics page: the game's shader cache emptied
  // (the owner's wish, 2026-10-24, after a colleague saw a tinted picture following a graphics
  // change). LauncherUi deletes it; the game builds it again on its next start.
  GameSetting reset;
  reset.cvar = "reset_shader_cache";
  reset.en = "Shader cache";
  reset.de = "Shader-Cache";
  reset.help_en = "Deletes the shaders this game has kept from earlier starts. The next start builds them "
                  "again (it stutters a little at first). Worth a try when the picture shows errors after "
                  "an update or a change of the graphics settings.";
  reset.help_de = "L\xC3\xB6scht die Shader, die das Spiel von fr\xC3\xBCheren Starts aufbewahrt. Der n\xC3\xA4" "chste "
                  "Start baut sie neu (am Anfang ruckelt es kurz). Einen Versuch wert, wenn das Bild nach "
                  "einem Update oder einer ge\xC3\xA4nderten Grafikeinstellung Fehler zeigt.";
  reset.type = "action";
  reset.options = {{"", "Empty it", "Leeren"}};
  auto page = std::find_if(out->pages.begin(), out->pages.end(),
                           [](const GameSettingsPage& p) { return p.en == "Graphics"; });
  (page != out->pages.end() ? *page : out->pages.back()).settings.push_back(std::move(reset));
  return out;
}

// Where a line's comment starts (a '#' outside a quoted string after the '='), or npos.
size_t CommentStart(const std::string& line) {
  const size_t eq = line.find('=');
  if (eq == std::string::npos) return std::string::npos;
  bool quoted = false;
  for (size_t i = eq + 1; i < line.size(); ++i) {
    if (line[i] == '\\' && quoted) {
      ++i;
    } else if (line[i] == '"') {
      quoted = !quoted;
    } else if (line[i] == '#' && !quoted) {
      return i;
    }
  }
  return std::string::npos;
}

// The setting a line assigns ("portal_mode" for `portal_mode = "usb"   # comment`), or empty.
std::string KeyOf(const std::string& line) {
  const size_t start = line.find_first_not_of(" \t");
  if (start == std::string::npos || line[start] == '#' || line[start] == '[') return {};
  const size_t eq = line.find('=', start);
  if (eq == std::string::npos) return {};
  std::string key = line.substr(start, eq - start);
  key.erase(key.find_last_not_of(" \t") + 1);
  return key;
}

}  // namespace

void SettingsLoader::Start(const Found& game, const std::string& key) {
  auto shared = std::make_shared<Shared>();
  shared_ = shared;
  const fs::path exe = game.exe, workdir = game.workdir;
  std::thread([shared, exe, workdir, key] {
    auto settings = Load(exe, workdir, key);
    {
      std::lock_guard lock(shared->mutex);
      shared->settings = settings;
    }
    shared->state = settings ? 1 : -1;
  }).detach();
}

void SettingsLoader::SetReady(std::shared_ptr<GameSettings> settings) {
  auto shared = std::make_shared<Shared>();
  shared->settings = std::move(settings);
  shared->state = shared->settings ? 1 : -1;
  shared_ = shared;
}

std::shared_ptr<GameSettings> SettingsLoader::settings() const {
  if (!shared_) return nullptr;
  std::lock_guard lock(shared_->mutex);
  return shared_->settings;
}

bool WriteGameSetting(const fs::path& file, const std::string& category, const std::string& name,
                      const std::string& type, const std::string& value) {
  std::string toml_value = value;
  if (type == "string") {
    toml_value = "\"";
    for (const char c : value) {
      if (c == '\\' || c == '"') toml_value += '\\';
      toml_value += c;
    }
    toml_value += "\"";
  }
  std::vector<std::string> lines;
  {
    std::ifstream in(file, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
  }
  const bool crlf = !lines.empty() && !lines[0].empty() && lines[0].back() == '\r';
  for (std::string& line : lines) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
  }
  const std::string entry = name + " = " + toml_value;
  bool done = false;
  for (std::string& line : lines) {
    if (KeyOf(line) != name) continue;
    // The comment after the value stays, in its column if the new value leaves room.
    const size_t comment = CommentStart(line);
    std::string replaced = entry;
    if (comment != std::string::npos) {
      replaced.append(comment > replaced.size() + 1 ? comment - replaced.size() : 2, ' ');
      replaced += line.substr(comment);
    }
    line = replaced;
    done = true;
    break;
  }
  if (!done) {
    const std::string quoted = "[\"" + category + "\"]", bare = "[" + category + "]";
    for (size_t i = 0; i < lines.size() && !done; ++i) {
      std::string header = lines[i];
      header.erase(header.find_last_not_of(" \t") + 1);
      if (header == quoted || header == bare) {
        lines.insert(lines.begin() + ptrdiff_t(i) + 1, entry);
        done = true;
      }
    }
  }
  if (!done) {
    if (!lines.empty() && !lines.back().empty()) lines.emplace_back();
    lines.push_back("[\"" + category + "\"]");
    lines.push_back(entry);
  }
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  for (const std::string& line : lines) out << line << (crlf ? "\r\n" : "\n");
  return bool(out);
}

}  // namespace coreoflight
