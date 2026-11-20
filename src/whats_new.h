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
    {"Wii U figures (new): from your own extracted Wii U SuperChargers or Imaginators, SkyP4tcher's converter adds "
     "the Nintendo figures to your game (here in the launcher settings).",
     "Wii-U-Figuren (neu): aus deinem eigenen entpackten Wii-U-SuperChargers oder -Imaginators bringt SkyP4tchers "
     "Konverter die Nintendo-Figuren in dein Spiel (hier in den Launcher-Einstellungen)."},
    {"The disc search finds unpacked discs too (a folder with default.xex), not only ISO files (mitch030504).",
     "Die Disc-Suche findet auch entpackte Discs (ein Ordner mit default.xex), nicht nur ISO-Dateien (mitch030504)."},
    {"The start screen: the bar at the bottom is sharp, and the cards' edges and the chosen card's light are clean.",
     "Der Startbildschirm: die Leiste unten ist scharf, die Kartenr\xC3\xA4nder und das Licht der gew\xC3\xA4hlten "
     "Karte sind sauber."},
};

}  // namespace coreoflight
