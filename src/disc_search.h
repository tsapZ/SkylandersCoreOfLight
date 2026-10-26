#pragma once

// Looking for the owner's discs (disc_search.cpp): every .iso of 100 MB and more in Downloads,
// Desktop and Documents (four folder levels down) and on the fixed and removable drives (two
// levels), each read like a disc being set up: its default.xex, known by its SHA-256. On a thread
// of its own; the file browser's "Discs suchen" (Y) and the first-run setup use it.

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace coreoflight {

// A disc the search found: the game (an index of AllGames(), -1 for another disc or another
// version of one of them) and the ISO; `languages`: Imaginators' PS3 disc, which brings its
// languages (language_import.h) rather than being set up.
struct FoundDisc {
  int game;
  std::filesystem::path iso;
  bool languages = false;
};

struct DiscSearch {
  std::atomic<bool> stop{false}, done{false};
  std::atomic<int> folders{0}, isos{0};
  std::mutex mutex;
  std::vector<FoundDisc> found;
};

// Starts a search; `stop` ends it early, `done` says it has finished.
std::shared_ptr<DiscSearch> StartDiscSearch();
// The same over the given folders only, each `depth` folder levels down (the setup's look next to
// an ISO chosen by hand: its folder and the one above).
std::shared_ptr<DiscSearch> StartDiscSearch(std::vector<std::pair<std::filesystem::path, int>> roots);

}  // namespace coreoflight
