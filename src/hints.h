#pragma once

// The button hints at the bottom of every screen follow the input in use: the controller's buttons
// after a pad button or stick, the keyboard's keys after a key, a mouse move or a click, with a
// short cross-fade between the two (the owner's wish, 2026-10-23).

#include "imgui.h"

namespace coreoflight {

// Once a frame, after ImGui::NewFrame: notes which input was used last.
void UpdateHintInput();

// Draws a hint's glyph at `pos` (the text line's top left at `size`): the controller button
// (`button`, "A", "LB/RB", ... in `col`) or the keyboard key (`key`; nullptr: the key the launcher
// uses for that button everywhere, A Enter, B Esc, LB Q, ...; "": the button only). Returns its
// width.
float HintGlyph(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const char* button, ImU32 col,
                const char* key = nullptr);

}  // namespace coreoflight
