#pragma once

// The button hints at the bottom of every screen follow the input in use: the controller's buttons
// after a pad button or stick, the keyboard's keys after a key, a mouse move or a click, with a
// short cross-fade between the two (the owner's wish, 2026-10-23).

#include <algorithm>

#include "imgui.h"

namespace coreoflight {

// How much bigger every screen is drawn than its size against 1280x720 says: 1, or more in
// handheld mode (handheld.h, #39). Set by LauncherUi::ApplyHubSettings.
inline float g_ui_zoom = 1.0f;

// A screen's size unit: the window against 1280x720, times g_ui_zoom.
inline float UiScale(float width, float height) { return std::min(width / 1280.0f, height / 720.0f) * g_ui_zoom; }

// A row of hints that gets smaller when it doesn't fit (a narrow window, handheld mode): Size()
// gives this frame's text size, End() takes where the row started and ended and the right edge it
// must stay inside; the next frame's size follows. Everything in a row scales with its size.
struct HintRowFit {
  float fit = 1.0f;
  float Size(float size) const { return size * fit; }
  void End(float start, float end, float limit) {
    const float full = (end - start) / fit;
    if (full > 0) fit = std::clamp((limit - start) / full, 0.6f, 1.0f);
  }
};

// Once a frame, after ImGui::NewFrame: notes which input was used last.
void UpdateHintInput();

// The kind of controller that pressed a button last (#29): the hints draw its buttons, PlayStation's
// symbols (cross, circle, ...) and L1 / R1, Nintendo's letters in their places and L / R / ZL / ZR.
// An XInput pad (Xbox, Steam Input, a Steam Deck) counts as Xbox. Set by UpdatePads.
enum class HintPad { kXbox, kPlayStation, kNintendo };
void SetHintPad(HintPad pad);
// That kind for the whole run, whatever pad is used (--hint_pad=playstation / nintendo).
void ForceHintPad(HintPad pad);

// Draws a hint's glyph at `pos` (the text line's top left at `size`): the controller button
// (`button`, "A", "LB/RB", ... in `col`) or the keyboard key (`key`; nullptr: the key the launcher
// uses for that button everywhere, A Enter, B Esc, LB Q, ...; "": the button only). Returns its
// width.
float HintGlyph(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const char* button, ImU32 col,
                const char* key = nullptr);

// The width HintGlyph draws this frame, without drawing (to centre a button before it is drawn).
float HintGlyphWidth(ImFont* font, float size, const char* button, const char* key = nullptr);

// A hint's area (its glyph and label) and its controller button ("A", "LB/RB", ...): a click there
// next frame presses that button for one frame, as the controller would (UpdateHintInput). Every
// screen's Hint() leaves its area; a button that handles its own clicks doesn't.
void HintClickArea(ImVec2 a, ImVec2 b, const char* button);
// Before ImGui::NewFrame, after the pads (UpdatePads): keeps a click's press from being cleared
// when no controller is connected.
void KeepClickedButton(ImGuiIO& io);

}  // namespace coreoflight
