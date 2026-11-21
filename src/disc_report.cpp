#include "disc_report.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "games.h"
#include "platform.h"
#include "xiso.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;

// JSON string contents: quotes and backslashes escaped, control characters as \u00XX. The path
// comes from the player, so no character may end the string early.
std::string JsonText(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (const unsigned char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char escape[8];
          std::snprintf(escape, sizeof(escape), "\\u%04X", c);
          out += escape;
        } else {
          out += char(c);
        }
    }
  }
  return out;
}

std::string JsonValue(const char* text) { return text ? "\"" + JsonText(text) + "\"" : "null"; }

}  // namespace

int CheckDiscReport(const fs::path& disc) {
  std::vector<uint8_t> xex;
  uint64_t bytes = 0;
  if (!ReadDiscXex(disc, xex, &bytes)) {
    std::printf("{\"path\":\"%s\",\"error\":\"not an Xbox 360 disc or no default.xex\"}\n",
                JsonText(PathUtf8(disc)).c_str());
    return 1;
  }
  const XexId id = IdentifyXex(xex.data(), xex.size());
  const auto& games = AllGames();
  int game = -1;
  XexMatch match = XexMatch::kNone;
  for (size_t i = 0; i < games.size(); ++i) {
    const XexMatch found = MatchXex(games[i].xex_sha256, id);
    if (found != XexMatch::kNone) {
      game = int(i);
      match = found;
      break;
    }
  }
  const char* what = match == XexMatch::kExact ? "exact" : match == XexMatch::kSameCode ? "code" : "unknown";
  const char* game_key = game >= 0 ? games[game].key : nullptr;
  const char* game_name = game >= 0 ? games[game].name_en : nullptr;
  if (game < 0) {
    // A build of one of the six whose code isn't a port's: named by its title, not set up.
    if (const int other = GameForTitleId(id.title_id); other >= 0) {
      game = other;
      what = "other";
      game_key = games[other].key;
      game_name = games[other].name_en;
    }
  }
  const std::string path_json = JsonText(PathUtf8(disc));
  const std::string game_json = JsonValue(game_key);
  const std::string name_json = JsonValue(game_name);
  const std::string header_json = JsonText(id.header);
  const std::string pin_json = JsonText(id.code_pin);
  std::printf(
      "{\"path\":\"%s\",\"game\":%s,\"name\":%s,\"match\":\"%s\",\"sha256\":\"%s\",\"code_pin\":\"%s\","
      "\"header\":\"%s\",\"bytes\":%llu}\n",
      path_json.c_str(), game_json.c_str(), name_json.c_str(), what, id.sha256.c_str(), pin_json.c_str(),
      header_json.c_str(), static_cast<unsigned long long>(bytes));
  return match == XexMatch::kExact || match == XexMatch::kSameCode ? 0 : 1;
}

}  // namespace coreoflight
