#include "key_names.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace coreoflight {

const std::vector<KeyBinding>& KeyBindings() {
  // The labels as the games' F4 > Keyboard page has them (skylanders-portal settings_menu.cpp kKeyRows), the
  // defaults as the SDK defines the settings (rexglue-sdk mnk_input_driver.cpp; no game sets its own).
  static const std::vector<KeyBinding> bindings = {
      {"keybind_a", "A button", "A-Taste", "Semicolon,Space"},
      {"keybind_b", "B button", "B-Taste", "Quote,Backspace"},
      {"keybind_x", "X button", "X-Taste", "L"},
      {"keybind_y", "Y button", "Y-Taste", "P"},
      {"keybind_lstick_up", "Move up", "Laufen: hoch", "W"},
      {"keybind_lstick_down", "Move down", "Laufen: runter", "S"},
      {"keybind_lstick_left", "Move left", "Laufen: links", "A"},
      {"keybind_lstick_right", "Move right", "Laufen: rechts", "D"},
      {"keybind_rstick_up", "Camera up", "Kamera: hoch", "Up"},
      {"keybind_rstick_down", "Camera down", "Kamera: runter", "Down"},
      {"keybind_rstick_left", "Camera left", "Kamera: links", "Left"},
      {"keybind_rstick_right", "Camera right", "Kamera: rechts", "Right"},
      {"keybind_left_trigger", "Left trigger (LT)", "Linker Trigger (LT)", "Q,I"},
      {"keybind_right_trigger", "Right trigger (RT)", "Rechter Trigger (RT)", "E,O"},
      {"keybind_left_shoulder", "Left shoulder (LB)", "Linke Schultertaste (LB)", "1"},
      {"keybind_right_shoulder", "Right shoulder (RB)", "Rechte Schultertaste (RB)", "3"},
      {"keybind_lstick_press", "Left stick press (L3)", "Linken Stick dr\xC3\xBC" "cken (L3)", "F"},
      {"keybind_rstick_press", "Right stick press (R3)", "Rechten Stick dr\xC3\xBC" "cken (R3)", "K"},
      {"keybind_dpad_up", "D-pad up", "Steuerkreuz hoch", "Shift+Up"},
      {"keybind_dpad_down", "D-pad down", "Steuerkreuz runter", "Shift+Down"},
      {"keybind_dpad_left", "D-pad left", "Steuerkreuz links", "Shift+Left"},
      {"keybind_dpad_right", "D-pad right", "Steuerkreuz rechts", "Shift+Right"},
      {"keybind_back", "Back", "Back", "Z,Tab"},
      {"keybind_start", "Start", "Start", "X,Return"},
  };
  return bindings;
}

std::string SdkKeyName(ImGuiKey key) {
  if (key >= ImGuiKey_A && key <= ImGuiKey_Z) return std::string(1, char('A' + (key - ImGuiKey_A)));
  if (key >= ImGuiKey_0 && key <= ImGuiKey_9) return std::string(1, char('0' + (key - ImGuiKey_0)));
  if (key >= ImGuiKey_F1 && key <= ImGuiKey_F24) return "F" + std::to_string(1 + (key - ImGuiKey_F1));
  if (key >= ImGuiKey_Keypad0 && key <= ImGuiKey_Keypad9) return "Numpad" + std::to_string(key - ImGuiKey_Keypad0);
  switch (key) {
    case ImGuiKey_Space: return "Space";
    case ImGuiKey_Enter: case ImGuiKey_KeypadEnter: return "Return";
    case ImGuiKey_Tab: return "Tab";
    case ImGuiKey_Backspace: return "Backspace";
    case ImGuiKey_Insert: return "Insert";
    case ImGuiKey_Home: return "Home";
    case ImGuiKey_End: return "End";
    case ImGuiKey_PageUp: return "PageUp";
    case ImGuiKey_PageDown: return "PageDown";
    case ImGuiKey_LeftArrow: return "Left";
    case ImGuiKey_RightArrow: return "Right";
    case ImGuiKey_UpArrow: return "Up";
    case ImGuiKey_DownArrow: return "Down";
    case ImGuiKey_LeftShift: case ImGuiKey_RightShift: return "Shift";
    case ImGuiKey_LeftCtrl: case ImGuiKey_RightCtrl: return "Control";
    case ImGuiKey_LeftAlt: case ImGuiKey_RightAlt: return "Alt";
    case ImGuiKey_CapsLock: return "CapsLock";
    case ImGuiKey_GraveAccent: return "Backtick";
    case ImGuiKey_Minus: return "Minus";
    case ImGuiKey_Equal: return "Plus";
    case ImGuiKey_LeftBracket: return "LBracket";
    case ImGuiKey_RightBracket: return "RBracket";
    case ImGuiKey_Backslash: return "Backslash";
    case ImGuiKey_Semicolon: return "Semicolon";
    case ImGuiKey_Apostrophe: return "Quote";
    case ImGuiKey_Comma: return "Comma";
    case ImGuiKey_Period: return "Period";
    case ImGuiKey_Slash: return "Slash";
    case ImGuiKey_KeypadAdd: return "NumpadPlus";
    case ImGuiKey_KeypadSubtract: return "NumpadMinus";
    case ImGuiKey_KeypadMultiply: return "NumpadStar";
    case ImGuiKey_KeypadDivide: return "NumpadSlash";
    default: return {};
  }
}

namespace {

// The Windows key code of an SDK name that prints a character (letters, digits, the OEM keys), else 0.
uint16_t PrintableVk(const std::string& name) {
  if (name.size() == 1 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9'))) {
    return uint16_t(name[0]);
  }
  static const struct {
    const char* name;
    uint16_t vk;
  } kOem[] = {{"Semicolon", 0xBA}, {"Plus", 0xBB},  {"Comma", 0xBC},    {"Minus", 0xBD},
              {"Period", 0xBE},    {"Slash", 0xBF}, {"Backtick", 0xC0}, {"LBracket", 0xDB},
              {"Backslash", 0xDC}, {"RBracket", 0xDD}, {"Quote", 0xDE}};
  for (const auto& o : kOem) {
    if (name == o.name) return o.vk;
  }
  return 0;
}

// A key that prints a character, named as this PC's keyboard prints it (skylanders-portal settings_menu.cpp
// LayoutKeyName, the owner's SwapForceRecomp #66: on a German keyboard "Semicolon" is the \xC3\x96 key). Empty
// for other keys.
std::string LayoutKeyName(const std::string& name) {
  const uint16_t vk = PrintableVk(name);
  if (!vk) return {};
#ifdef _WIN32
  static HKL us = LoadKeyboardLayoutW(L"00000409", KLF_NOTELLSHELL);
  const UINT scan = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, us ? us : GetKeyboardLayout(0));
  wchar_t text[32] = {};
  const int length = scan ? GetKeyNameTextW(LONG(scan) << 16, text, 32) : 0;
  if (length <= 0) return {};
  if (length == 1) CharUpperBuffW(text, 1);  // a letter as printed on the key: a capital
  char utf8[96] = {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), nullptr, nullptr);
  return n > 1 ? std::string(utf8) : std::string();
#else
  if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) return {};
  SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
  if (vk == '0') sc = SDL_SCANCODE_0;
  else if (vk >= '1' && vk <= '9') sc = static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (vk - '1'));
  else if (vk >= 'A' && vk <= 'Z') sc = static_cast<SDL_Scancode>(SDL_SCANCODE_A + (vk - 'A'));
  else if (vk == 0xBA) sc = SDL_SCANCODE_SEMICOLON;
  else if (vk == 0xBB) sc = SDL_SCANCODE_EQUALS;
  else if (vk == 0xBC) sc = SDL_SCANCODE_COMMA;
  else if (vk == 0xBD) sc = SDL_SCANCODE_MINUS;
  else if (vk == 0xBE) sc = SDL_SCANCODE_PERIOD;
  else if (vk == 0xBF) sc = SDL_SCANCODE_SLASH;
  else if (vk == 0xC0) sc = SDL_SCANCODE_GRAVE;
  else if (vk == 0xDB) sc = SDL_SCANCODE_LEFTBRACKET;
  else if (vk == 0xDC) sc = SDL_SCANCODE_BACKSLASH;
  else if (vk == 0xDD) sc = SDL_SCANCODE_RIGHTBRACKET;
  else if (vk == 0xDE) sc = SDL_SCANCODE_APOSTROPHE;
  if (sc == SDL_SCANCODE_UNKNOWN) return {};
  const SDL_Keycode key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
  const char* text = key ? SDL_GetKeyName(key) : "";
  if (text == nullptr || *text == '\0') return {};
  std::string out(text);
  if (out.size() == 1 && out[0] >= 'a' && out[0] <= 'z') out[0] = char(out[0] - ('a' - 'A'));
  return out;
#endif
}

std::string KeyLabel(const std::string& key, bool german) {
  if (const std::string printed = LayoutKeyName(key); !printed.empty()) return printed;
  if (key == "Space") return german ? "Leertaste" : "Space";
  if (key == "Return") return "Enter";
  if (key == "Backspace") return german ? "R\xC3\xBC" "cktaste" : "Backspace";
  if (key == "Up") return german ? "Pfeil hoch" : "Up";
  if (key == "Down") return german ? "Pfeil runter" : "Down";
  if (key == "Left") return german ? "Pfeil links" : "Left";
  if (key == "Right") return german ? "Pfeil rechts" : "Right";
  if (key == "Control") return german ? "Strg" : "Ctrl";
  if (key == "Shift") return german ? "Umschalt" : "Shift";
  return key;
}

}  // namespace

std::string KeysLabel(const std::string& value, bool german) {
  std::string out;
  size_t start = 0;
  while (start <= value.size()) {
    const size_t comma = std::min(value.find(',', start), value.size());
    std::string key;
    for (char c : value.substr(start, comma - start)) {
      if (c != ' ') key.push_back(c);
    }
    start = comma + 1;
    if (key.empty()) continue;
    std::string name;
    size_t from = 0;
    for (size_t plus; (plus = key.find('+', from)) != std::string::npos; from = plus + 1) {
      name += KeyLabel(key.substr(from, plus - from), german) + "+";
    }
    name += KeyLabel(key.substr(from), german);
    out += (out.empty() ? "" : " / ") + name;
  }
  return out.empty() ? std::string(german ? "keine" : "none") : out;
}

}  // namespace coreoflight
