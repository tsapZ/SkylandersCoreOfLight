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
    {"The mouse cursor goes away while you don't move the mouse.",
     "Der Mauszeiger verschwindet, solange du die Maus nicht bewegst."},
};

}  // namespace coreoflight
