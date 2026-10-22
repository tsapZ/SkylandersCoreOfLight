#pragma once

// A disc's NXE dashboard art (nxe_art.cpp): the Xbox 360 dashboard's pictures of the game, in the
// disc's rom\nxeart, an STFS package (PIRS). Its nxeslot.jpg is the game's tile, its logo on its
// background (420x320, "SKYLANDERS GIANTS" ...), and nxebg.jpg the dashboard's background
// (1920x1080). They are read from the owner's disc at run time and kept as files next to the
// games' own pictures, so nothing of them is in the repository.

#include <filesystem>
#include <string>

namespace coreoflight {

// The art file `name` ("nxeslot.jpg", "nxebg.jpg") from the disc in `rom_dir`, written to
// `cache_dir` the first time (and again when the package is newer); its path there, or empty if
// the disc has no such package or file.
std::filesystem::path NxeArt(const std::filesystem::path& rom_dir, const std::filesystem::path& cache_dir,
                             const std::string& name);

}  // namespace coreoflight
