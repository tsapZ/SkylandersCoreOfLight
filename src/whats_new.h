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
    {"The Imaginators language row names your disc's own languages (an English disc says English).",
     "Die Imaginators-Sprachzeile nennt die Sprachen deiner Disc (eine englische Disc sagt Englisch)."},
    {"A games folder on a network share (\\\\server\\share) works now.",
     "Ein Spiele-Ordner auf einer Netzwerkfreigabe (\\\\server\\freigabe) geht jetzt."},
};

}  // namespace coreoflight
