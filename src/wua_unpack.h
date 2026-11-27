#pragma once

// Cemu's compressed Wii U games (.wua, Exzap's ZArchive format, thirdparty/zarchive on thirdparty/zstd) for the Wii U
// figures (Core of Light #166): a player who has the game as a .wua picks it as it is, instead of unpacking it with
// another tool first. A .wua isn't encrypted (Cemu decrypts the game with the player's own keys when it makes one), so
// nothing here deals with keys. It holds one folder per title, "<title id, 16 hex digits>_v<version>": the game
// (00050000xxxxxxxx), its update (0005000E, the same lower half) and its DLC (0005000C). The converters read only the
// game's content/archives (superpatcher.py and imagipatcher.py, find_wiiu), so only that is unpacked, the update's files
// over the game's, as Cemu runs them.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace coreoflight {

// Whether `path` is a .wua file by its name (any case); whether it is a real one shows when it is opened.
bool IsWuaFile(const std::filesystem::path& path);

// Unpacks the game's content/archives from `wua` into `target`/content/archives (`target` made if missing). The game is
// the title folder whose content/archives has `marker` (the file the converter looks for, "Bowser.pak"), else the
// first game title with a content/archives folder; its update's content/archives (the highest version) goes over it.
// Stops before writing when the drive has less room than the files plus 1 GB. `progress` (0..1 by bytes written) and
// `bytes` (the total to write, set before the first byte) may be null. False with `error` (English, for the log) on a
// broken archive, no game in it, no room, or a file that can't be written; what was written stays, so callers unpack
// into a scratch folder and remove it either way.
bool UnpackWuaArchives(const std::filesystem::path& wua, const std::filesystem::path& target, const std::string& marker,
                       std::atomic<float>* progress, std::atomic<uint64_t>* bytes, std::string& error);

}  // namespace coreoflight
