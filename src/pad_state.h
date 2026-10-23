#pragma once

// The pads for the launcher: every XInput slot and every DirectInput pad, and whether a button is still held right now: any connected pad's buttons or triggers (XInput,
// loaded by hand like Dear ImGui's Win32 backend does), or Enter, Space or Esc on the keyboard.
// For the moment a game ends and the launcher comes back (main.cpp).

#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "hub_log.h"
#include "imgui.h"

namespace coreoflight {

// Which XInput slots have a pad, as UpdatePads saw them last (it looks every two seconds).
inline bool g_pad_connected[XUSER_MAX_COUNT] = {};

// Pads that aren't XInput ones, DirectInput pads such as the Logitech Dual Action (the owner's
// player 1 pad): XInput doesn't see them, so the launcher reads them through Windows' joystick API
// (winmm) with the usual layout of such pads, SDL's mapping for the Dual Action: buttons 1-4 = X,
// A, B, Y; 5 / 6 the shoulders, 7 / 8 the triggers, 9 Back, 10 Start, 11 / 12 the stick clicks,
// the hat the d-pad, X / Y the left stick, Z / R the right one. An XInput pad shows up there as
// well, with another layout, so a device with the vendor and product of an XInput device ("IG_"
// in its HID path, as Microsoft's XInput-and-DirectInput note tells them apart) is left out.
struct GenericPad {
  bool connected = false, skip = false;
  DWORD min[4] = {0, 0, 0, 0}, max[4] = {65535, 65535, 65535, 65535};  // X, Y, Z, R
};
inline GenericPad g_generic_pads[16];

// The vendor and product ids of the XInput devices (from the raw input HID list).
inline std::set<std::pair<unsigned long, unsigned long>> XInputVidPids() {
  std::set<std::pair<unsigned long, unsigned long>> out;
  UINT count = 0;
  if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || !count) return out;
  std::vector<RAWINPUTDEVICELIST> list(count);
  if (GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST)) == UINT(-1)) return out;
  for (UINT i = 0; i < count; ++i) {
    if (list[i].dwType != RIM_TYPEHID) continue;
    wchar_t name[512] = L"";
    UINT size = 512;  // characters for RIDI_DEVICENAME
    if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICENAME, name, &size) == UINT(-1)) continue;
    std::wstring path(name);
    for (wchar_t& c : path) c = wchar_t(std::towupper(c));
    const size_t ig = path.find(L"IG_"), vid = path.find(L"VID_"), pid = path.find(L"PID_");
    if (ig == std::wstring::npos || vid == std::wstring::npos || pid == std::wstring::npos) continue;
    out.emplace(std::wcstoul(path.c_str() + vid + 4, nullptr, 16), std::wcstoul(path.c_str() + pid + 4, nullptr, 16));
  }
  return out;
}

// Looks again which joystick ids have a pad that isn't an XInput one (every two seconds).
inline void FindGenericPads() {
  const auto xinput = XInputVidPids();
  const UINT n = std::min<UINT>(joyGetNumDevs(), 16);
  for (UINT id = 0; id < 16; ++id) {
    GenericPad& pad = g_generic_pads[id];
    JOYINFOEX info = {sizeof(info), JOY_RETURNALL};
    pad.connected = id < n && joyGetPosEx(id, &info) == JOYERR_NOERROR;
    if (!pad.connected) continue;
    JOYCAPSW caps = {};
    pad.skip = joyGetDevCapsW(id, &caps, sizeof(caps)) != JOYERR_NOERROR ||
               xinput.count({caps.wMid, caps.wPid}) != 0;
    if (pad.skip) continue;
    const DWORD mins[4] = {caps.wXmin, caps.wYmin, caps.wZmin, caps.wRmin};
    const DWORD maxs[4] = {caps.wXmax, caps.wYmax, caps.wZmax, caps.wRmax};
    for (int a = 0; a < 4; ++a) {
      pad.min[a] = mins[a];
      pad.max[a] = maxs[a] > mins[a] ? maxs[a] : mins[a] + 65535;
    }
  }
}

// One generic pad's state as an XInput one, or false if it is gone.
inline bool ReadGenericPad(UINT id, XINPUT_GAMEPAD& out) {
  GenericPad& pad = g_generic_pads[id];
  JOYINFOEX info = {sizeof(info), JOY_RETURNALL};
  if (joyGetPosEx(id, &info) != JOYERR_NOERROR) {
    pad.connected = false;
    return false;
  }
  out = {};
  const DWORD b = info.dwButtons;
  auto on = [&](int bit, WORD mask) {
    if (b & (1u << bit)) out.wButtons |= mask;
  };
  on(0, XINPUT_GAMEPAD_X);
  on(1, XINPUT_GAMEPAD_A);
  on(2, XINPUT_GAMEPAD_B);
  on(3, XINPUT_GAMEPAD_Y);
  on(4, XINPUT_GAMEPAD_LEFT_SHOULDER);
  on(5, XINPUT_GAMEPAD_RIGHT_SHOULDER);
  on(8, XINPUT_GAMEPAD_BACK);
  on(9, XINPUT_GAMEPAD_START);
  on(10, XINPUT_GAMEPAD_LEFT_THUMB);
  on(11, XINPUT_GAMEPAD_RIGHT_THUMB);
  if (b & (1u << 6)) out.bLeftTrigger = 255;
  if (b & (1u << 7)) out.bRightTrigger = 255;
  // The hat in hundredths of a degree, 0 up, clockwise; a diagonal gives both directions.
  const DWORD pov = info.dwPOV;
  if (LOWORD(pov) != 0xFFFF && pov < 36000) {
    if (pov >= 31500 || pov <= 4500) out.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
    if (pov >= 4500 && pov <= 13500) out.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
    if (pov >= 13500 && pov <= 22500) out.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
    if (pov >= 22500 && pov <= 31500) out.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
  }
  // Axes to XInput's -32768..32767, y up.
  auto axis = [&](int a, DWORD v, bool invert) {
    const double t = (double(v) - double(pad.min[a])) / double(pad.max[a] - pad.min[a]);
    const int s = int(std::clamp(t * 65535.0 - 32768.0, -32768.0, 32767.0));
    return SHORT(invert ? std::clamp(-s, -32768, 32767) : s);
  };
  out.sThumbLX = axis(0, info.dwXpos, false);
  out.sThumbLY = axis(1, info.dwYpos, true);
  out.sThumbRX = axis(2, info.dwZpos, false);
  out.sThumbRY = axis(3, info.dwRpos, true);
  return true;
}

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
  auto merge = [&](const XINPUT_GAMEPAD& g) {
    any = true;
    merged.wButtons |= g.wButtons;
    merged.bLeftTrigger = std::max(merged.bLeftTrigger, g.bLeftTrigger);
    merged.bRightTrigger = std::max(merged.bRightTrigger, g.bRightTrigger);
    // The stick pushed furthest counts.
    auto further = [](SHORT a, SHORT b) { return std::abs(int(b)) > std::abs(int(a)) ? b : a; };
    merged.sThumbLX = further(merged.sThumbLX, g.sThumbLX);
    merged.sThumbLY = further(merged.sThumbLY, g.sThumbLY);
    merged.sThumbRX = further(merged.sThumbRX, g.sThumbRX);
    merged.sThumbRY = further(merged.sThumbRY, g.sThumbRY);
  };
  for (DWORD pad = 0; get_state && pad < XUSER_MAX_COUNT; ++pad) {
    if (!connected[pad] && !recheck) continue;
    XINPUT_STATE state = {};
    const bool was = connected[pad];
    connected[pad] = get_state(pad, &state) == ERROR_SUCCESS;
    if (connected[pad] != was) {
      HubLog("pads: XInput slot " + std::to_string(pad) + (connected[pad] ? " connected" : " gone"));
    }
    if (connected[pad]) merge(state.Gamepad);
  }
  // The DirectInput pads, merged the same way.
  if (recheck) FindGenericPads();
  for (UINT id = 0; id < 16; ++id) {
    if (!g_generic_pads[id].connected || g_generic_pads[id].skip) continue;
    XINPUT_GAMEPAD g;
    if (ReadGenericPad(id, g)) merge(g);
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
  for (UINT id = 0; id < 16; ++id) {
    if (!g_generic_pads[id].connected || g_generic_pads[id].skip) continue;
    XINPUT_GAMEPAD g;
    if (ReadGenericPad(id, g) && (g.wButtons || g.bLeftTrigger || g.bRightTrigger)) return true;
  }
  for (const int key : {VK_RETURN, VK_SPACE, VK_ESCAPE}) {
    if (GetAsyncKeyState(key) & 0x8000) return true;
  }
  return false;
}

}  // namespace coreoflight
