#pragma once

// The games' keyboard keys as Core of Light shows and sets them (#156: one key page for all six games).
// The games keep them in their settings files as the SDK's keybind_* settings ("Input/Keybinds/Controller",
// rexglue-sdk src/input/mnk/mnk_input_driver.cpp), each a list of key names ("Semicolon,Space") from the
// SDK's src/ui/keybinds.cpp kKeyNames: names by the key's place on a US keyboard.

#include <string>
#include <vector>

#include "imgui.h"

namespace coreoflight {

// One controller button the keyboard plays: its setting, its row label and the SDK's default keys.
struct KeyBinding {
  const char* cvar;
  const char* en;
  const char* de;
  const char* sdk_default;
};

// The rows of the games' F4 > Keyboard page (skylanders-portal settings_menu.cpp kKeyRows), in its order.
const std::vector<KeyBinding>& KeyBindings();

// The settings file section the games keep them in.
inline constexpr char kKeyBindingCategory[] = "Input/Keybinds/Controller";

// The SDK's name for a key ImGui reports, as the games' own key page takes it (skylanders-portal
// settings_menu.cpp VirtualKeyOf, then the SDK's VirtualKeyToString); empty for a key the games can't bind
// (a controller's or the mouse's, Escape, Delete).
std::string SdkKeyName(ImGuiKey key);

// A key setting's value as shown: "Semicolon,Space" -> "\xC3\x96 / Leertaste" on a German keyboard (the key's
// legend from this PC's layout, as the games' page shows it), "Shift+Up" -> "Umschalt+Pfeil hoch".
std::string KeysLabel(const std::string& value, bool german);

}  // namespace coreoflight
