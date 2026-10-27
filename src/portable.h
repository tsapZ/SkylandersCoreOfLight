#pragma once

// The portable launcher (#46): the same files as the setup's, as a ZIP with portable.txt next to the
// exe, for launcher managers (Quiver Launcher) that can't run a setup. It runs from wherever it was
// unpacked and updates itself from the release's CoreOfLightPortable-<version>.zip instead of the
// setup: the new files are written next to the old ones first (<name>.new), then the old ones,
// the running exe and its DLLs included, are renamed to <name>.old (Windows lets a running program
// be renamed, not overwritten) and the new ones put in their place; the next start deletes the
// .old files. The data stays where it always is (Documents\Skylanders).

#include <filesystem>
#include <string>

namespace coreoflight {

// The release asset a portable launcher updates from: <kPortablePrefix>-<version><kPortableExtension>.
inline constexpr char kPortablePrefix[] = "CoreOfLightPortable";
inline constexpr char kPortableExtension[] = ".zip";

// Whether the launcher in `launcher_dir` is the portable one (portable.txt next to it).
bool IsPortable(const std::filesystem::path& launcher_dir);

// Puts the launcher from the downloaded `zip` into `launcher_dir` as described above. `exe_name` is
// the running launcher's file name, which the ZIP must contain (with portable.txt). On failure
// nothing is replaced (or what was is put back) and `error` says why.
bool ApplyPortableUpdate(const std::filesystem::path& zip, const std::filesystem::path& launcher_dir,
                         const std::filesystem::path& exe_name, bool german, std::string& error);

// Deletes what an update left: the old files (<name>.old, <name>.old2 ...) and unused new ones
// (<name>.new). Call at start, in a portable launcher only.
void RemoveUpdateLeftovers(const std::filesystem::path& launcher_dir);

// Starts the launcher `exe` in its own folder with this launcher's arguments (after an update).
// False if it couldn't.
bool StartLauncher(const std::filesystem::path& exe);

}  // namespace coreoflight
