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
    {"Figure library: R3 (F) makes a figure a favourite, the same favourites as in the games' portal menu, with a "
     "Favourites tab of their own.",
     "Figurenbibliothek: R3 (F) macht eine Figur zum Favoriten, dieselben Favoriten wie im Portalmen\xC3\xBC der Spiele, "
     "mit einem eigenen Favoriten-Tab."},
};

}  // namespace coreoflight
