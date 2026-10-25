#pragma once

// Imaginators figures from a folder (#34, the owner's decision 2026-10-25): this game's toys are
// signed by their maker, so nothing can make them, and the launcher offers an import instead of
// any download. It takes a folder (subfolders too), checks every file and copies the ones the game
// accepts into the figure folder's Imaginators folder under the figure's name, never over a file
// that is there (figure_import.cpp).

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "games.h"

namespace coreoflight {

class FigureImport {
 public:
  // Starts on a thread of its own: `source` the folder picked, `imaginators` the launcher's
  // Imaginators (its exe checks the files), `figures` the active profile's figure folder.
  void Start(const std::filesystem::path& source, const Found& imaginators, const std::filesystem::path& figures,
             bool german);
  bool running() const { return shared_ && !shared_->done; }
  // What came in and what didn't, once, when it is done; else empty.
  std::string TakeSummary();

 private:
  struct Shared {
    std::atomic<bool> done{false};
    std::mutex mutex;
    std::string summary;
    bool taken = false;
  };
  std::shared_ptr<Shared> shared_;
};

}  // namespace coreoflight
