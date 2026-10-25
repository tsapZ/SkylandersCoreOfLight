#include "figure_import.h"

// Two checks per file (the owner's follow-up: only what the game really accepts, so nobody has to
// clean up a collection first):
//   1. Here: 1024 bytes, the header's checksum (CRC-16 of bytes 0-0x1D at 0x1E), a toy the
//      catalog knows as an Imaginators one. An earlier game's figure is skipped: the portal menu
//      makes those itself.
//   2. The game itself: the files that passed go to the Imaginators exe with --check_figures
//      (games.h StartFigureCheck), which runs the game's own signature check on each and writes
//      the verdict. Only "ok" (a signed toy that passed) and "unsigned" (one the game doesn't check)
//      come in. The game's keys stay in the game.
// A file whose bytes are in the Imaginators folder already counts as there; a new one keeps its
// own name (or gets the figure's), numbered when the name is taken ("Starcast (2).dump").

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <string_view>
#include <thread>

#include "hub_log.h"
#include "portal/figure_catalog.h"
#include "portal/figure_crypto.h"
#include "portal/figure_file.h"
#include "portal/figure_info.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;
namespace sp = skylanders::portal;

std::string U8(const fs::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}

// The catalog entry for an Imaginators toy: its id and variant, else the first with its id.
const sp::SkylanderInfo* ImaginatorsToy(uint16_t id, uint16_t variant, std::string& other_game) {
  if (const sp::SkylanderInfo* info = sp::FindSkylander(id, variant)) {
    if (info->game == "Imaginators") return info;
    other_game = std::string(info->game);
    return nullptr;
  }
  for (const sp::SkylanderInfo& info : sp::AllSkylanders()) {
    if (info.id == id && info.game == "Imaginators") return &info;
  }
  return nullptr;
}

std::string Bytes(const sp::FigureData& data) { return std::string(reinterpret_cast<const char*>(data.data()), data.size()); }

}  // namespace

void FigureImport::Start(const fs::path& source, const Found& imaginators, const fs::path& figures, bool german) {
  if (running()) return;
  shared_ = std::make_shared<Shared>();
  std::thread([shared = shared_, source, imaginators, figures, german] {
    auto finish = [&](const std::string& text) {
      HubLog("figure import: " + text);
      std::lock_guard lock(shared->mutex);
      shared->summary = text;
      shared->done = true;
    };
    if (imaginators.exe.empty()) {
      finish(german ? "Imaginators ist nicht installiert: die Pr\xC3\xBC" "fung braucht das Spiel."
                    : "Imaginators isn't installed: the check needs the game.");
      return;
    }
    // Step 1, here.
    struct Candidate {
      fs::path file;
      sp::FigureData data;
      std::string name;
    };
    std::vector<Candidate> candidates;
    std::map<std::string, int> skipped;  // reason -> count
    auto skip = [&](const fs::path& file, const std::string& reason) {
      ++skipped[reason];
      HubLog("figure import: skipped " + U8(file) + " (" + reason + ")");
    };
    std::error_code ec;
    size_t seen = 0;
    for (fs::recursive_directory_iterator it(source, ec), end; it != end && seen < 20000; it.increment(ec)) {
      if (ec) break;
      if (!it->is_regular_file(ec)) continue;
      ++seen;
      std::string ext = it->path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
      const auto size = it->file_size(ec);
      // Figure files only: a dump's 1024 bytes, or a .dump / .bin that isn't (said why).
      if (ec || size != 1024) {
        if (ext == ".dump" || ext == ".bin") skip(it->path(), german ? "keine 1024 Bytes" : "not 1024 bytes");
        continue;
      }
      const auto data = sp::LoadFigureFile(it->path());
      if (!data) continue;
      const uint16_t crc = uint16_t((*data)[0x1E] | (*data)[0x1F] << 8);
      if (sp::FigureCrc16(data->data(), 0x1E) != crc) {
        skip(it->path(), german ? "Kopf-Pr\xC3\xBC" "fsumme falsch" : "header checksum wrong");
        continue;
      }
      std::string other_game;
      const sp::SkylanderInfo* info = ImaginatorsToy(sp::ReadFigureId(*data), sp::ReadFigureVariant(*data), other_game);
      if (!info) {
        skip(it->path(), !other_game.empty() ? (german ? "aus " + other_game + " (macht das Portal-Men\xC3\xBC selbst)"
                                                        : "from " + other_game + " (the portal menu makes those)")
                                              : (german ? "unbekanntes Spielzeug" : "unknown toy"));
        continue;
      }
      // The file's own name when it is one (a collection's "Water Quickshot", "Bronze Mystery Chest
      // 54"): the catalog knows senseis by name, but crystals and chests only by a generic one
      // ("Water Ninja 0", "Bronze Mystery Chest 1" for every chest). A name without three letters
      // ("0012.bin") gets the catalog's.
      const std::u8string stem8 = it->path().stem().u8string();
      std::string name(stem8.begin(), stem8.end());
      if (std::count_if(name.begin(), name.end(), [](unsigned char ch) { return std::isalpha(ch); }) < 3) {
        const std::string shown = sp::FigureDisplayName(*data);
        name = sp::DisplayName(shown.empty() ? info->name : shown);
      }
      for (char& ch : name) {
        if (std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos) ch = '_';  // not in file names
      }
      candidates.push_back({it->path(), *data, std::move(name)});
    }
    if (candidates.empty()) {
      int n = 0;
      for (const auto& [reason, count] : skipped) n += count;
      finish(german ? "Keine Imaginators-Figuren gefunden" + (n ? " (" + std::to_string(n) + " \xC3\xBC" "bersprungen)." : std::string("."))
                    : "No Imaginators figures found" + (n ? " (" + std::to_string(n) + " skipped)." : std::string(".")));
      return;
    }
    // Step 2, the game.
    wchar_t temp[MAX_PATH] = L"";
    GetTempPathW(MAX_PATH, temp);
    const fs::path list = fs::path(temp) / "CoreOfLight" / "figure_check.txt";
    fs::create_directories(list.parent_path(), ec);
    fs::remove(fs::path(list.wstring() + L".result"), ec);
    {
      std::ofstream out(list, std::ios::binary | std::ios::trunc);
      for (const Candidate& c : candidates) out << U8(c.file) << "\n";
    }
    void* process = nullptr;
    if (!StartFigureCheck(imaginators, list, &process)) {
      finish(german ? "Diese Imaginators-Version kann die Figuren noch nicht pr\xC3\xBC" "fen: bitte aktualisieren."
                    : "This Imaginators version can't check figures yet: please update it.");
      return;
    }
    // About 5 s; a minute is plenty, then it is stopped.
    if (WaitForSingleObject(HANDLE(process), 60000) == WAIT_TIMEOUT) TerminateProcess(HANDLE(process), 1);
    CloseHandle(HANDLE(process));
    std::map<std::string, std::string> verdict;  // file -> "ok" / "unsigned" / "bad"
    {
      std::ifstream in(fs::path(list.wstring() + L".result"), std::ios::binary);
      std::string line;
      while (std::getline(in, line)) {
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const size_t tab2 = line.find('\t', tab + 1);
        verdict[line.substr(tab + 1, tab2 == std::string::npos ? std::string::npos : tab2 - tab - 1)] = line.substr(0, tab);
      }
    }
    if (verdict.empty()) {
      finish(german ? "Imaginators hat die Figuren nicht gepr\xC3\xBC" "ft (siehe Problem melden)."
                    : "Imaginators didn't check the figures (see Report a problem).");
      return;
    }
    // In: what passed and isn't there yet.
    const fs::path target = figures / "Imaginators";
    std::set<std::string> have;
    for (fs::recursive_directory_iterator it(target, ec), end; it != end; it.increment(ec)) {
      if (ec) break;
      if (!it->is_regular_file(ec)) continue;
      if (const auto data = sp::LoadFigureFile(it->path())) have.insert(Bytes(*data));
    }
    fs::create_directories(target, ec);
    int imported = 0, already = 0;
    for (const Candidate& c : candidates) {
      const auto v = verdict.find(U8(c.file));
      if (v == verdict.end() || (v->second != "ok" && v->second != "unsigned")) {
        skip(c.file, german ? "vom Spiel abgelehnt" : "refused by the game");
        continue;
      }
      if (!have.insert(Bytes(c.data)).second) {
        ++already;
        continue;
      }
      const fs::path to = sp::UniqueFigurePath(target, c.name);
      if (sp::SaveFigureFileAtomic(to, c.data)) {
        ++imported;
        HubLog("figure import: " + U8(c.file) + " -> " + U8(to));
      } else {
        skip(c.file, german ? "konnte nicht schreiben" : "couldn't write");
      }
    }
    int skipped_count = 0;
    std::string reasons;
    for (const auto& [reason, count] : skipped) {
      skipped_count += count;
      reasons += (reasons.empty() ? "" : ", ") + std::to_string(count) + " " + reason;
    }
    std::string text = german ? std::to_string(imported) + " neu, " + std::to_string(already) + " schon da"
                              : std::to_string(imported) + " new, " + std::to_string(already) + " already there";
    if (skipped_count) {
      text += german ? ", " + std::to_string(skipped_count) + " \xC3\xBC" "bersprungen (" + reasons + ")"
                     : ", " + std::to_string(skipped_count) + " skipped (" + reasons + ")";
    }
    finish(text);
  }).detach();
}

std::string FigureImport::TakeSummary() {
  if (!shared_ || !shared_->done) return {};
  std::lock_guard lock(shared_->mutex);
  if (shared_->taken) return {};
  shared_->taken = true;
  return shared_->summary;
}

}  // namespace coreoflight
