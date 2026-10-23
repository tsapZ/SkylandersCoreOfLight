#pragma once

// The pads for the launcher: every XInput slot, and whether a button is still held right now: any connected pad's buttons or triggers (XInput,
// loaded by hand like Dear ImGui's Win32 backend does), or Enter, Space or Esc on the keyboard.
// For the moment a game ends and the launcher comes back (main.cpp).

#include <windows.h>
#include <xinput.h>

#include <algorithm>
#include <cstdlib>

#include "imgui.h"

namespace coreoflight {

// Which XInput slots have a pad, as UpdatePads saw them last (it looks every two seconds).
inline bool g_pad_connected[XUSER_MAX_COUNT] = {};

// A connected pad's battery for the start screen: 0 empty .. 3 full, or -1 for a wired pad (or
// one that doesn't say). Asked at most every ten seconds per pad.
inline int PadBattery(DWORD pad) {
  using GetBattery = DWORD(WINAPI*)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
  static GetBattery get_battery = [] {
    for (const wchar_t* dll : {L"xinput1_4.dll", L"xinput1_3.dll"}) {
      if (HMODULE m = LoadLibraryW(dll)) {
        if (auto f = reinterpret_cast<GetBattery>(GetProcAddress(m, "XInputGetBatteryInformation"))) return f;
      }
    }
    return GetBattery(nullptr);
  }();
  static int level[XUSER_MAX_COUNT] = {-1, -1, -1, -1};
  static ULONGLONG asked[XUSER_MAX_COUNT] = {};
  if (pad >= XUSER_MAX_COUNT || !get_battery) return -1;
  const ULONGLONG now = GetTickCount64();
  if (!asked[pad] || now - asked[pad] > 10000) {
    asked[pad] = now;
    XINPUT_BATTERY_INFORMATION info = {};
    level[pad] = -1;
    if (get_battery(pad, BATTERY_DEVTYPE_GAMEPAD, &info) == ERROR_SUCCESS &&
        (info.BatteryType == BATTERY_TYPE_ALKALINE || info.BatteryType == BATTERY_TYPE_NIMH)) {
      level[pad] = info.BatteryLevel;  // BATTERY_LEVEL_EMPTY .. BATTERY_LEVEL_FULL, 0 .. 3
    }
  }
  return level[pad];
}

// Every connected XInput pad (slots 0-3) as Dear ImGui's gamepad keys, merged: its Win32 backend
// reads only slot 0, so a second pad, or one that came back in another slot, did nothing
// (IMGUI_IMPL_WIN32_DISABLE_GAMEPAD is set for it). Which slots are connected is looked at every
// two seconds; polling an empty slot every frame is slow. Call after ImGui_ImplWin32_NewFrame.
inline void UpdatePads(ImGuiIO& io) {
  using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
  static GetState get_state = [] {
    for (const wchar_t* dll : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
      if (HMODULE m = LoadLibraryW(dll)) {
        return reinterpret_cast<GetState>(GetProcAddress(m, "XInputGetState"));
      }
    }
    return GetState(nullptr);
  }();
  bool* connected = g_pad_connected;
  static ULONGLONG checked = 0;
  const ULONGLONG now = GetTickCount64();
  const bool recheck = now - checked > 2000;
  if (recheck) checked = now;
  XINPUT_GAMEPAD merged = {};
  bool any = false;
  for (DWORD pad = 0; get_state && pad < XUSER_MAX_COUNT; ++pad) {
    if (!connected[pad] && !recheck) continue;
    XINPUT_STATE state = {};
    connected[pad] = get_state(pad, &state) == ERROR_SUCCESS;
    if (!connected[pad]) continue;
    any = true;
    const XINPUT_GAMEPAD& g = state.Gamepad;
    merged.wButtons |= g.wButtons;
    merged.bLeftTrigger = std::max(merged.bLeftTrigger, g.bLeftTrigger);
    merged.bRightTrigger = std::max(merged.bRightTrigger, g.bRightTrigger);
    // The stick pushed furthest counts.
    auto further = [](SHORT a, SHORT b) { return std::abs(int(b)) > std::abs(int(a)) ? b : a; };
    merged.sThumbLX = further(merged.sThumbLX, g.sThumbLX);
    merged.sThumbLY = further(merged.sThumbLY, g.sThumbLY);
    merged.sThumbRX = further(merged.sThumbRX, g.sThumbRX);
    merged.sThumbRY = further(merged.sThumbRY, g.sThumbRY);
  }
  io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
  if (!any) return;
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  // As Dear ImGui's Win32 backend maps them (imgui_impl_win32.cpp ImGui_ImplWin32_UpdateGamepads).
  auto button = [&](ImGuiKey key, WORD mask) { io.AddKeyEvent(key, (merged.wButtons & mask) != 0); };
  auto analog = [&](ImGuiKey key, float value, float v0, float v1) {
    const float v = std::clamp((value - v0) / (v1 - v0), 0.0f, 1.0f);
    io.AddKeyAnalogEvent(key, v > 0.10f, v);
  };
  button(ImGuiKey_GamepadStart, XINPUT_GAMEPAD_START);
  button(ImGuiKey_GamepadBack, XINPUT_GAMEPAD_BACK);
  button(ImGuiKey_GamepadFaceLeft, XINPUT_GAMEPAD_X);
  button(ImGuiKey_GamepadFaceRight, XINPUT_GAMEPAD_B);
  button(ImGuiKey_GamepadFaceUp, XINPUT_GAMEPAD_Y);
  button(ImGuiKey_GamepadFaceDown, XINPUT_GAMEPAD_A);
  button(ImGuiKey_GamepadDpadLeft, XINPUT_GAMEPAD_DPAD_LEFT);
  button(ImGuiKey_GamepadDpadRight, XINPUT_GAMEPAD_DPAD_RIGHT);
  button(ImGuiKey_GamepadDpadUp, XINPUT_GAMEPAD_DPAD_UP);
  button(ImGuiKey_GamepadDpadDown, XINPUT_GAMEPAD_DPAD_DOWN);
  button(ImGuiKey_GamepadL1, XINPUT_GAMEPAD_LEFT_SHOULDER);
  button(ImGuiKey_GamepadR1, XINPUT_GAMEPAD_RIGHT_SHOULDER);
  button(ImGuiKey_GamepadL3, XINPUT_GAMEPAD_LEFT_THUMB);
  button(ImGuiKey_GamepadR3, XINPUT_GAMEPAD_RIGHT_THUMB);
  analog(ImGuiKey_GamepadL2, merged.bLeftTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD, 255);
  analog(ImGuiKey_GamepadR2, merged.bRightTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD, 255);
  constexpr float kDead = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
  analog(ImGuiKey_GamepadLStickLeft, merged.sThumbLX, -kDead, -32768);
  analog(ImGuiKey_GamepadLStickRight, merged.sThumbLX, +kDead, +32767);
  analog(ImGuiKey_GamepadLStickUp, merged.sThumbLY, +kDead, +32767);
  analog(ImGuiKey_GamepadLStickDown, merged.sThumbLY, -kDead, -32768);
  analog(ImGuiKey_GamepadRStickLeft, merged.sThumbRX, -kDead, -32768);
  analog(ImGuiKey_GamepadRStickRight, merged.sThumbRX, +kDead, +32767);
  analog(ImGuiKey_GamepadRStickUp, merged.sThumbRY, +kDead, +32767);
  analog(ImGuiKey_GamepadRStickDown, merged.sThumbRY, -kDead, -32768);
}

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
