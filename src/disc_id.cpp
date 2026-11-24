// What a default.xex is (games.h): its SHA-256, and the code pin that another pressing of the
// same game shares (skylanders-portal's xex/xex_header.h). Here rather than in games.cpp so the
// test builds it without the launcher's window and game discovery.

#include "games.h"

#include <cstddef>

#include "xex/xex_header.h"
#include "xiso.h"

namespace coreoflight {

XexId IdentifyXex(const void* data, size_t size) {
  XexId id;
  id.sha256 = Sha256Hex(data, size);
  const auto* bytes = static_cast<const uint8_t*>(data);
  if (const auto header = skylanders::xex::ParseXexHeader(bytes, size)) {
    id.header = skylanders::xex::DescribeXexHeader(*header);
    id.title_id = header->title_id;
    id.version = header->version;
    id.region = header->region;
    id.has_header = true;
    if (header->code_pages_size) {
      id.code_pin = skylanders::xex::CodePin(
          *header, Sha256Hex(bytes + header->code_pages_offset, header->code_pages_size));
    }
  }
  return id;
}

XexMatch MatchXex(std::string_view list, const XexId& xex) {
  bool same_code = false;
  for (size_t start = 0;;) {
    const size_t comma = list.find(',', start);
    const std::string_view entry =
        list.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
    if (entry == xex.sha256) return XexMatch::kExact;
    if (!xex.code_pin.empty() && entry == xex.code_pin) same_code = true;
    if (comma == std::string_view::npos) break;
    start = comma + 1;
  }
  return same_code ? XexMatch::kSameCode : XexMatch::kNone;
}

bool XexInList(std::string_view list, const XexId& xex) { return MatchXex(list, xex) != XexMatch::kNone; }

bool IsGameDisc(const Game& game, const XexId& xex) { return XexInList(game.xex_sha256, xex); }

int GameForDiscIn(const std::vector<Game>& games, const XexId& xex) {
  // An exact pressing wins over a same-code one anywhere in the list: a future entry that overlaps
  // another game's code must not make the report, the import and the search name it differently.
  int same_code = -1;
  for (size_t i = 0; i < games.size(); ++i) {
    const XexMatch found = MatchXex(games[i].xex_sha256, xex);
    if (found == XexMatch::kExact) return int(i);
    if (found == XexMatch::kSameCode && same_code < 0) same_code = int(i);
  }
  return same_code;
}

int GameForTitleIdIn(const std::vector<Game>& games, uint32_t title_id) {
  if (title_id == 0) return -1;
  for (size_t i = 0; i < games.size(); ++i) {
    if (games[i].title_id == title_id) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace coreoflight
