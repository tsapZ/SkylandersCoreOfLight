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
  std::string sha256;   // the asset's SHA-256 as GitHub gives it ("digest"), lower-case hex; empty
                        // for a release from before GitHub gave one
};

// The newest release of tsapZ/<repo> (pre-releases too, drafts not) with an asset named
// <asset_prefix>-<version><extension> (a setup's .exe, the portable launcher's .zip); nullopt
// without network, release or such an asset. Blocks (a few hundred milliseconds): call it from a
// background thread.
std::optional<Release> FetchLatestRelease(const std::string& repo, const std::string& asset_prefix,
                                          const std::string& extension = ".exe");

// The releases kept from the last check, and when it was (Windows file time; 0: never).
std::vector<Release> LoadKnownReleases(uint64_t* checked = nullptr);
void SaveKnownReleases(const std::vector<Release>& releases);
// Whether the last check is older than six hours.
bool UpdateCheckDue();

// Downloads `url` into `file` (written as <file>.part, renamed when complete): progress 0..1 into
// `progress` while it runs, stops early when `cancel` becomes true. False on failure or cancel.
// SHA-256 of a file (Windows' CNG), lower-case hex; empty if it can't be read.
std::string FileSha256(const std::filesystem::path& file);

// With `sha256` given (a release's), a download whose bytes don't hash to it is deleted and counts as
// failed: a damaged or swapped file never gets run.
bool DownloadFile(const std::string& url, const std::filesystem::path& file, std::atomic<float>* progress,
                  const std::atomic<bool>* cancel, const std::string& sha256 = {});

// Where downloaded setups go: %TEMP%\CoreOfLight\downloads.
std::filesystem::path DownloadsDir();

// --offline: no releases at all, neither the ones kept nor a check, so nothing gets downloaded or
// installed from GitHub (scripted tests of the launcher: a test once installed two real games).
void SetOffline(bool offline);
bool Offline();

}  // namespace coreoflight
