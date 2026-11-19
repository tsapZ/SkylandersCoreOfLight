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
    {"Core of Light and the six games now come for Linux too (new; mitch030504's port): on the release pages.",
     "Core of Light und die sechs Spiele gibt es jetzt auch f\xC3\xBCr Linux (neu; die Portierung von mitch030504): "
     "auf den Release-Seiten."},
    {"On Windows nothing changes in this version.",
     "Unter Windows \xC3\xA4ndert sich in dieser Version nichts."},
};

}  // namespace coreoflight
