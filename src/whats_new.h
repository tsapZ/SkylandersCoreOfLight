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
    {"The 3DS games (new): the five 3DS Skylanders, recompiled for Windows. 3 / R3 on the start screen; each one is set up from your own cartridge dump (the European editions for now).",
     "Die 3DS-Spiele (neu): die f\xC3\xBCnf 3DS-Skylanders, f\xC3\xBCr Windows \xC3\xBC" "bersetzt. 3 / R3 auf dem Startbildschirm; jedes wird aus deinem eigenen Modul-Dump eingerichtet (vorerst die europ\xC3\xA4ischen Ausgaben)."},
    {"A disc of another build of one of the games (the Russian Spyro's Adventure, for example) is named with its version instead of \"unknown\" (mitch030504).",
     "Eine Disc eines anderen Builds eines der Spiele (etwa das russische Spyro's Adventure) wird mit ihrer Version genannt statt \"unbekannt\" (mitch030504)."},
    {"Imaginators' Wii U figures take the language you imported: put them in again once and Bowser's and Donkey Kong's upgrades follow it.",
     "Die Wii-U-Figuren von Imaginators \xC3\xBC" "bernehmen die importierte Sprache: einmal neu einbauen, dann folgen ihr die Verbesserungen von Bowser und Donkey Kong."},
    {"Linux: the launcher comes back after a game on Wayland desktops.",
     "Linux: der Launcher kommt nach einem Spiel auf Wayland-Desktops wieder."},
};

}  // namespace coreoflight
