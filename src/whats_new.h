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
    {"A game that can't start because a file is missing: the box names the file and where to get it back.",
     "Ein Spiel startet nicht, weil eine Datei fehlt: der Kasten nennt die Datei und wo man sie zur\xC3\xBC" "ckholt."},
    {"Achievements: a long description stays inside its info box.",
     "Erfolge: eine lange Beschreibung bleibt in ihrem Infokasten."},
    {"Figure files ending .dmp (the plain 1 KB dump some tools write) count in the profiles like .dump files.",
     "Figurendateien mit der Endung .dmp (der reine 1-KB-Dump mancher Tools) z\xC3\xA4hlen in den Profilen wie .dump-Dateien."},
};

}  // namespace coreoflight
