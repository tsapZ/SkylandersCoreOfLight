#pragma once

// What's new in this version of the launcher, a few short lines in English and German, shown at
// the bottom right of the launcher settings (LB / Q, #44). Rewritten for each release: only what a
// player notices, newest version only.

namespace coreoflight {

struct WhatsNewLine {
  const char* en;
  const char* de;
};

inline constexpr WhatsNewLine kWhatsNew[] = {
    {"Every Skylander: Series 2 Double Trouble is listed under Giants as \"Double Trouble (Series 2)\", not as a chase variant.",
     "Alle Skylander: Double Trouble der Serie 2 steht unter Giants als \"Double Trouble (Series 2)\", nicht mehr bei den seltenen Varianten."},
    {"Barrel Blaster and Clown Cruiser have their own elements, Tech and Air.",
     "Barrel Blaster und Clown Cruiser haben ihre eigenen Elemente, Technik und Luft."},
    {"Linux on ARM64 (a Steam Frame, for example): the launcher builds and packages there too and looks for the matching downloads (mitch030504).",
     "Linux auf ARM64 (etwa ein Steam Frame): der Launcher l\xC3\xA4sst sich dort bauen und packen und sucht die passenden Downloads (mitch030504)."},
};

}  // namespace coreoflight
