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

// Length of the valid UTF-8 sequence that starts at `text[i]`, or 0 when there is none (a stray
// continuation byte, a truncated or overlong sequence, a surrogate, or beyond U+10FFFF).
size_t Utf8Length(const std::string& text, size_t i) {
  const unsigned char first = static_cast<unsigned char>(text[i]);
  if (first < 0x80) return 1;
  size_t length = 0;
  uint32_t code = 0;
  if ((first & 0xE0) == 0xC0) {
    length = 2;
    code = first & 0x1F;
  } else if ((first & 0xF0) == 0xE0) {
    length = 3;
    code = first & 0x0F;
  } else if ((first & 0xF8) == 0xF0) {
    length = 4;
    code = first & 0x07;
  } else {
    return 0;
  }
  if (i + length > text.size()) return 0;
  for (size_t k = 1; k < length; ++k) {
    const unsigned char next = static_cast<unsigned char>(text[i + k]);
    if ((next & 0xC0) != 0x80) return 0;
    code = (code << 6) | (next & 0x3F);
  }
  if ((length == 2 && code < 0x80) || (length == 3 && code < 0x800) ||
      (length == 4 && code < 0x10000)) {
    return 0;  // overlong
  }
  if ((code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF) return 0;
  return length;
}

// JSON string contents: quotes and backslashes escaped, control characters as \u00XX. The path
// comes from the player, so no character may end the string early; a byte that isn't part of a
// valid UTF-8 sequence becomes U+FFFD, so the line decodes as UTF-8 whatever the file system's
// bytes are (a Linux path needn't be).
std::string JsonText(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (size_t i = 0; i < text.size();) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    switch (c) {
      case '"':
        out += "\\\"";
        ++i;
        break;
      case '\\':
        out += "\\\\";
        ++i;
        break;
      case '\n':
        out += "\\n";
        ++i;
        break;
      case '\r':
        out += "\\r";
        ++i;
        break;
      case '\t':
        out += "\\t";
        ++i;
        break;
      default:
        if (c < 0x20) {
          char escape[8];
          std::snprintf(escape, sizeof(escape), "\\u%04X", c);
          out += escape;
          ++i;
        } else if (c < 0x80) {
          out += char(c);
          ++i;
        } else if (const size_t length = Utf8Length(text, i); length) {
          out.append(text, i, length);
          i += length;
        } else {
          out += "\\uFFFD";
          ++i;
        }
        break;
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
  // One rule with the import and the search (games.h GameForDisc): an exact pressing wins over a
  // same-code one anywhere in the list.
  int game = GameForDisc(id);
  XexMatch match = XexMatch::kNone;
  if (game >= 0) match = MatchXex(games[size_t(game)].xex_sha256, id);
  const char* what = match == XexMatch::kExact ? "exact" : match == XexMatch::kSameCode ? "code" : "unknown";
  const char* game_key = game >= 0 ? games[size_t(game)].key : nullptr;
  const char* game_name = game >= 0 ? games[size_t(game)].name_en : nullptr;
  if (game < 0) {
    // A build of one of the six whose code isn't a port's: named by its title, not set up.
    if (const int other = GameForTitleId(id.title_id); other >= 0) {
      game = other;
      what = "other";
      game_key = games[size_t(other)].key;
      game_name = games[size_t(other)].name_en;
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
