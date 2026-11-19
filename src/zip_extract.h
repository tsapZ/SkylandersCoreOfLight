#pragma once

// Unpacks a ZIP to a folder one entry at a time (the Wii U converter package, wiiu_figures.cpp, is a few hundred MB
// unpacked; portable.cpp's reader holds a whole update in memory, fine for the launcher's own files). Stored and
// deflated entries, each checked against its CRC; no ZIP64. Entry names must stay below the folder (no drive, no
// leading slash, no "." or ".." part).

#include <atomic>
#include <filesystem>
#include <string>

namespace coreoflight {

// Unpacks `zip` into `target` (made if missing; files there with the same names are replaced). `progress` (0..1 by
// bytes read) may be null. False with `error` (English, for the log) on a broken archive or a file that can't be
// written; what was written stays, so callers unpack into a fresh folder and rename it when this succeeds.
bool ExtractZip(const std::filesystem::path& zip, const std::filesystem::path& target, std::atomic<float>* progress,
                std::string& error);

}  // namespace coreoflight
