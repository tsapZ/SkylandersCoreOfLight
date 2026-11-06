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
    {"The settings only ask to save after a real change.",
     "Die Einstellungen fragen nur noch nach echten \xC3\x84nderungen, ob du speichern willst."},
    {"After a crash, the log says where it happened.",
     "Nach einem Absturz steht im Log, wo er passiert ist."},
};

}  // namespace coreoflight
