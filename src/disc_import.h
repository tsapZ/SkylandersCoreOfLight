#pragma once

// Setting up a game's disc from the owner's own ISO (disc_import.cpp): the launcher reads the
// image's default.xex, knows the game by its SHA-256 (the hash each game's repository pins) and
// unpacks the disc into the rom\ folder that game reads.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "games.h"

namespace coreoflight {

class DiscImport {
 public:
  enum class Phase { kIdle, kReading, kUnpacking, kDone, kFailed };

  // Asks for an ISO with the Windows file dialog; false if none was chosen.
  static bool PickIso(std::filesystem::path& iso, bool german);
  // Asks for a folder with an unpacked disc (default.xex in it); false if none was chosen.
  static bool PickFolder(std::filesystem::path& folder, bool german);

  // Starts reading `source` (an ISO, or a folder with an unpacked disc) on a thread of its own;
  // `found` are the launcher's games (where each reads its disc). Not while one is running.
  void Start(const std::filesystem::path& iso, const std::vector<Found>& found, bool german);
  bool running() const;

  Phase phase() const { return shared_ ? shared_->phase.load() : Phase::kIdle; }
  // 0..1 while unpacking.
  float progress() const;
  std::string message() const;
  // The game the disc belongs to, or -1.
  int game() const { return shared_ ? shared_->game.load() : -1; }

 private:
  struct Shared {
    std::atomic<Phase> phase{Phase::kIdle};
    std::atomic<uint64_t> done{0}, total{0};
    std::atomic<int> game{-1};
    std::mutex mutex;
    std::string message;
  };
  std::shared_ptr<Shared> shared_;
};

}  // namespace coreoflight
