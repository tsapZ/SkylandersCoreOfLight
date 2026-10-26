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
    {"Move to a new PC: export and import your data here.",
     "Neuer PC: deine Daten hier exportieren und importieren."},
    {"Check a game's files in its settings (Y / Tab).",
     "Spieldateien pr\xC3\xBC" "fen in den Einstellungen des Spiels (Y / Tab)."},
    {"Handheld mode: bigger screens on a handheld.", "Handheld-Modus: gr\xC3\xB6\xC3\x9F" "ere Bildschirme auf Handhelds."},
    {"The games' own element symbols in the figure library.",
     "Die Element-Symbole aus den Spielen in der Figuren-Bibliothek."},
    {"A real USB portal shows in the top bar.", "Ein echtes USB-Portal erscheint oben in der Leiste."},
    {"Figures: collection progress, as an option here.", "Figuren: Sammlungsstand, als Option hier."},
};

}  // namespace coreoflight
