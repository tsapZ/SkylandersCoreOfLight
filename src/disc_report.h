#pragma once

// The disc check without a window (main.cpp's --check_disc, disc_report.cpp): what a disc is, by
// its default.xex, as one JSON line for the tests and for a bug report.

#include <filesystem>

namespace coreoflight {

// Prints one JSON line (path, game, name, match, sha256, code_pin, header, bytes) for `disc`, an
// ISO image or a folder with an unpacked disc. 0 when one of the games takes it (its exact
// pressing, or another pressing with the same code), 1 for another build, an unknown disc or one
// that can't be read.
int CheckDiscReport(const std::filesystem::path& disc);

}  // namespace coreoflight
