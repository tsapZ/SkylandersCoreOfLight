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
    {"The figure library shows the games' own element symbols.",
     "Die Figuren-Bibliothek zeigt die Element-Symbole aus den Spielen."},
    {"A real USB portal shows in the top bar.", "Ein echtes USB-Portal erscheint oben in der Leiste."},
    {"Figures: collection progress, as an option here.", "Figuren: Sammlungsstand, als Option hier."},
    {"This little \"what's new\".", "Dieses kleine \xE2\x80\x9EWas ist neu\xE2\x80\x9C."},
};

}  // namespace coreoflight
