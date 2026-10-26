#pragma once

// Handheld mode (#39, handheld.cpp): on a handheld PC (ROG Ally, Legion Go, a Ryzen Z1 / Z2
// machine, a Steam Deck with Windows) the launcher's screens are drawn bigger, and a game set up
// there gets the lighter resolution scale 1x instead of 2x. The launcher setting "Handheld mode"
// is Automatic (what Windows says about this PC), On or Off.

#include <string>

namespace coreoflight {

// What Windows says about this PC, read once: the main screen's size from its own data (its EDID,
// through the raw DPI), the processor's name, and whether either says handheld (a screen up to 9"
// across, or a handheld chip).
struct HandheldInfo {
  float screen_inches = 0;  // 0: not known
  std::string cpu;
  bool detected = false;
};
const HandheldInfo& DetectHandheld();

// Whether the launcher works in handheld mode: the setting ("auto", "on", "off") with the detection.
bool HandheldActive(const std::string& setting);

// Whether handheld mode is on right now (set with the launcher's settings, LauncherUi::
// ApplyHubSettings): a game's setup then gets resolution scale 1x.
inline bool g_handheld_active = false;

// How much bigger the screens are drawn in handheld mode (hints.h UiScale).
inline constexpr float kHandheldZoom = 1.1f;

}  // namespace coreoflight
