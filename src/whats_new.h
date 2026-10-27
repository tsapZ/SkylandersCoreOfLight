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
    {"Also as a portable ZIP without a setup; it updates itself.",
     "Auch als portable ZIP ohne Setup; aktualisiert sich selbst."},
    {"Arrows show when a settings list goes on above or below.",
     "Pfeile zeigen, wenn eine Einstellungsliste oben oder unten weitergeht."},
};

}  // namespace coreoflight
