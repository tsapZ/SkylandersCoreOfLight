#pragma once

// Whether a button is still held right now: any connected pad's buttons or triggers (XInput,
// loaded by hand like Dear ImGui's Win32 backend does), or Enter, Space or Esc on the keyboard.
// For the moment a game ends and the launcher comes back (main.cpp).

#include <windows.h>
#include <xinput.h>

namespace coreoflight {

inline bool InputHeld() {
  using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
  static GetState get_state = [] {
    for (const wchar_t* dll : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
      if (HMODULE m = LoadLibraryW(dll)) {
        return reinterpret_cast<GetState>(GetProcAddress(m, "XInputGetState"));
      }
    }
    return GetState(nullptr);
  }();
  for (DWORD pad = 0; get_state && pad < XUSER_MAX_COUNT; ++pad) {
    XINPUT_STATE state = {};
    if (get_state(pad, &state) != ERROR_SUCCESS) continue;
    const XINPUT_GAMEPAD& g = state.Gamepad;
    if (g.wButtons || g.bLeftTrigger > 30 || g.bRightTrigger > 30) return true;
  }
  for (const int key : {VK_RETURN, VK_SPACE, VK_ESCAPE}) {
    if (GetAsyncKeyState(key) & 0x8000) return true;
  }
  return false;
}

}  // namespace coreoflight
