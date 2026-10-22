#pragma once

// A disc's NXE dashboard art (nxe_art.cpp): the Xbox 360 dashboard's pictures of the game, in the
// disc's rom\nxeart, an STFS package (PIRS). Its nxeslot.jpg is the game's tile, its logo on its
// background (420x320, "SKYLANDERS GIANTS" ...), and nxebg.jpg the dashboard's background
// (1920x1080). They are read from the owner's disc at run time and kept as files next to the
// games' own pictures, so nothing of them is in the repository.

#include <filesystem>
#include <string>
#include <vector>

struct ImDrawList;

namespace coreoflight {

// The art file `name` ("nxeslot.jpg", "nxebg.jpg") from the disc in `rom_dir`, written to
// `cache_dir` the first time (and again when the package is newer); its path there, or empty if
// the disc has no such package or file.
std::filesystem::path NxeArt(const std::filesystem::path& rom_dir, const std::filesystem::path& cache_dir,
                             const std::string& name);

// The screens' background: `picture` (a 16:9 dashboard background) filling the window, darkened,
// or the launcher's deep blue without one. UI thread only.
void DrawBackdrop(ImDrawList* dl, float width, float height, const std::filesystem::path& picture);

// Each game's dashboard background (by its index in AllGames), as the launcher found them, for
// the screens that belong to one game (its settings, its achievements); empty if none.
void SetGameBackgrounds(std::vector<std::filesystem::path> pictures);
std::filesystem::path GameBackground(size_t game);

}  // namespace coreoflight
