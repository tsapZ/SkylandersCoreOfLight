#pragma once

// Updates from GitHub Releases (#17): the newest release of the launcher's and each game's
// repository (github.com/tsapZ/<repo>), each with its setup as an asset. Asked through the GitHub
// API without an account (60 requests an hour per address), at most once a day unless the player
// asks (hub settings > "Nach Updates suchen"); the answers are kept in
// %LOCALAPPDATA%\CoreOfLight\releases.txt, so the cards know them right at the start.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coreoflight {

struct Release {
  std::string repo;     // "SwapForceRecomp"
  std::string version;  // "0.1.0-beta.7" (the tag without its "v")
  std::string asset;    // "SwapForceRecompSetup-0.1.0-beta.7.exe"
  std::string url;      // its browser_download_url
  uint64_t size = 0;    // bytes
};

// The newest release of tsapZ/<repo> (pre-releases too, drafts not) with an asset named
// <asset_prefix>-<version>.exe; nullopt without network, release or such an asset. Blocks (a few
// hundred milliseconds): call it from a background thread.
std::optional<Release> FetchLatestRelease(const std::string& repo, const std::string& asset_prefix);

// The releases kept from the last check, and when it was (Windows file time; 0: never).
std::vector<Release> LoadKnownReleases(uint64_t* checked = nullptr);
void SaveKnownReleases(const std::vector<Release>& releases);
// Whether the last check is older than a day.
bool UpdateCheckDue();

// Downloads `url` into `file` (written as <file>.part, renamed when complete): progress 0..1 into
// `progress` while it runs, stops early when `cancel` becomes true. False on failure or cancel.
bool DownloadFile(const std::string& url, const std::filesystem::path& file, std::atomic<float>* progress,
                  const std::atomic<bool>* cancel);

// Where downloaded setups go: %TEMP%\CoreOfLight\downloads.
std::filesystem::path DownloadsDir();

// --offline: no releases at all, neither the ones kept nor a check, so nothing gets downloaded or
// installed from GitHub (scripted tests of the launcher: a test once installed two real games).
void SetOffline(bool offline);
bool Offline();

}  // namespace coreoflight
