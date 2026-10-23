#include "hints.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

namespace coreoflight {
namespace {

// 0: the controller's buttons are shown, 1: the keyboard's keys; `shown` follows `wanted`.
float g_wanted = 0.0f;
float g_shown = 0.0f;
constexpr float kFadeSeconds = 0.25f;

// The keys the launcher's screens take for a controller button (each screen's input code checks
// both, e.g. Start or F for the figures); a screen with other keys names them itself.
const char* DefaultKey(const char* button) {
  static const struct {
    const char* button;
    const char* key;
  } kKeys[] = {
      {"A", "Enter"},   {"B", "Esc"},     {"X", "X"},          {"Y", "Y"},
      {"LB", "Q"},      {"RB", "E"},      {"LT", "PgUp"},      {"RT", "S"},
      {"Start", "F"},   {"Back", "R"},    {"LB/RB", "Q/E"},    {"LT/RT", "PgUp/PgDn"},
  };
  for (const auto& k : kKeys) {
    if (std::strcmp(k.button, button) == 0) return k.key;
  }
  return "";
}

ImU32 WithAlpha(ImU32 col, float alpha) {
  const ImU32 a = ImU32(float((col >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f));
  return (col & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

float Width(ImFont* font, float size, const char* text) { return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x; }

// Clickable hints: each hint drawn leaves its area and its controller button (this frame's list);
// a click on one of the last frame's areas presses that button for one frame, so the screen's own
// handling of it runs (a colleague's wish, 2026-10-23: "Speichern", "Zurück", "Seite" with the
// mouse). "LB/RB": the left half the first button, the right half the second.
struct ClickArea {
  ImVec2 a, b;
  ImGuiKey first, second;
};
std::vector<ClickArea> g_areas, g_last_areas;
ImGuiKey g_clicked = ImGuiKey_None;  // pressed by a click, released the frame after

ImGuiKey PadKey(std::string_view button) {
  static const std::pair<std::string_view, ImGuiKey> kButtons[] = {
      {"A", ImGuiKey_GamepadFaceDown}, {"B", ImGuiKey_GamepadFaceRight}, {"X", ImGuiKey_GamepadFaceLeft},
      {"Y", ImGuiKey_GamepadFaceUp},   {"LB", ImGuiKey_GamepadL1},       {"RB", ImGuiKey_GamepadR1},
      {"LT", ImGuiKey_GamepadL2},      {"RT", ImGuiKey_GamepadR2},       {"Start", ImGuiKey_GamepadStart},
      {"Back", ImGuiKey_GamepadBack},  {"L3", ImGuiKey_GamepadL3},       {"R3", ImGuiKey_GamepadR3},
  };
  for (const auto& [name, key] : kButtons) {
    if (name == button) return key;
  }
  return ImGuiKey_None;
}

// The controller button: a pill (a circle for one letter) in the button's colour.
float PadWidth(ImFont* font, float size, const char* button) {
  return std::max(Width(font, size * 0.8f, button) + size * 0.5f, size * 1.24f);
}

// The keyboard key: a key cap, dark with a light edge.
float KeyWidth(ImFont* font, float size, const char* key) {
  return std::max(Width(font, size * 0.72f, key) + size * 0.7f, size * 1.3f);
}

}  // namespace

void UpdateHintInput() {
  ImGuiIO& io = ImGui::GetIO();
  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
    const ImGuiKey key = ImGuiKey(k);
    if (!ImGui::IsKeyPressed(key, false) || key == g_clicked) continue;  // a click's press: still the mouse
    if (key >= ImGuiKey_GamepadStart && key <= ImGuiKey_GamepadRStickDown) {
      g_wanted = 0.0f;
    } else if (key < ImGuiKey_MouseLeft || key > ImGuiKey_MouseWheelY) {
      g_wanted = 1.0f;
    }
  }
  // The mouse counts once it is moved on purpose; the window coming up (or moving) under a resting
  // cursor in the first second doesn't make a controller player's hints keys.
  const bool moved = ImGui::GetTime() > 1.0 && io.MouseDelta.x * io.MouseDelta.x + io.MouseDelta.y * io.MouseDelta.y > 4.0f;
  if (moved || io.MouseWheel != 0.0f || ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
      ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    g_wanted = 1.0f;
  }
  const float step = io.DeltaTime / kFadeSeconds;
  g_shown = g_wanted > g_shown ? std::min(g_wanted, g_shown + step) : std::max(g_wanted, g_shown - step);
  // A click's press (queued last frame, down this one) is let go; then this frame's click on one
  // of the last frame's hints queues its button (down from the next frame on).
  if (g_clicked != ImGuiKey_None) {
    io.AddKeyEvent(g_clicked, false);
    g_clicked = ImGuiKey_None;
  }
  g_last_areas.swap(g_areas);
  g_areas.clear();
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const ImVec2 m = io.MousePos;
    for (const ClickArea& area : g_last_areas) {
      if (m.x < area.a.x || m.x >= area.b.x || m.y < area.a.y || m.y >= area.b.y) continue;
      const bool right = area.second != ImGuiKey_None && m.x >= (area.a.x + area.b.x) * 0.5f;
      g_clicked = right ? area.second : area.first;
      io.AddKeyEvent(g_clicked, true);
      break;
    }
  }
}

void KeepClickedButton(ImGuiIO& io) {
  // Dear ImGui clears the controller's buttons each frame while the backend has no controller (no
  // pad connected: a mouse user, whom this is for); the frame a click's press is down it has one.
  if (g_clicked != ImGuiKey_None) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
}

void HintClickArea(ImVec2 a, ImVec2 b, const char* button) {
  const std::string_view name = button;
  const size_t slash = name.find('/');
  const ImGuiKey first = PadKey(name.substr(0, slash));
  const ImGuiKey second = slash == std::string_view::npos ? ImGuiKey_None : PadKey(name.substr(slash + 1));
  if (first != ImGuiKey_None) g_areas.push_back({a, b, first, second});
}

float HintGlyph(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const char* button, ImU32 col,
                const char* key) {
  if (!key) key = DefaultKey(button);
  const float t = *key ? g_shown : 0.0f;
  const float pad_w = PadWidth(font, size, button), key_w = *key ? KeyWidth(font, size, key) : pad_w;
  const float top = pos.y + size * 0.55f - size * 0.62f, h = size * 1.24f;
  if (t < 1.0f) {
    dl->AddRectFilled({pos.x, top}, {pos.x + pad_w, top + h}, WithAlpha(col, 1.0f - t), size * 0.62f);
    dl->AddText(font, size * 0.8f, {pos.x + (pad_w - Width(font, size * 0.8f, button)) * 0.5f, pos.y + size * 0.13f},
                WithAlpha(IM_COL32_WHITE, 1.0f - t), button);
  }
  if (t > 0.0f) {
    dl->AddRectFilled({pos.x, top}, {pos.x + key_w, top + h}, WithAlpha(IM_COL32(34, 40, 72, 255), t), size * 0.22f);
    dl->AddRect({pos.x, top}, {pos.x + key_w, top + h}, WithAlpha(IM_COL32(200, 206, 228, 255), t), size * 0.22f, 0,
                std::max(1.0f, size * 0.07f));
    dl->AddText(font, size * 0.72f, {pos.x + (key_w - Width(font, size * 0.72f, key)) * 0.5f, pos.y + size * 0.17f},
                WithAlpha(IM_COL32_WHITE, t), key);
  }
  return pad_w + (key_w - pad_w) * t;
}

float HintGlyphWidth(ImFont* font, float size, const char* button, const char* key) {
  if (!key) key = DefaultKey(button);
  const float t = *key ? g_shown : 0.0f;
  const float pad_w = PadWidth(font, size, button), key_w = *key ? KeyWidth(font, size, key) : pad_w;
  return pad_w + (key_w - pad_w) * t;
}

}  // namespace coreoflight
