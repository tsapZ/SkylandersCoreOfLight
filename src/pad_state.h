#pragma once

// The pads for the launcher: every XInput slot and every DirectInput pad, and whether a button is still held right now: any connected pad's buttons or triggers (XInput,
// loaded by hand like Dear ImGui's Win32 backend does), or Enter, Space or Esc on the keyboard.
// For the moment a game ends and the launcher comes back (main.cpp).

#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "hints.h"
#include "hub_log.h"
#include "imgui.h"

namespace coreoflight {

// Which XInput slots have a pad, as the pad watch (StartPadWatch) saw them last, every two seconds.
inline std::atomic<bool> g_pad_connected[XUSER_MAX_COUNT] = {};

// XInputGetState, loaded by hand as Dear ImGui's Win32 backend does (or null without XInput).
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
inline XInputGetStateFn PadGetState() {
  static XInputGetStateFn get_state = [] {
    for (const wchar_t* dll : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
      if (HMODULE m = LoadLibraryW(dll)) {
        return reinterpret_cast<XInputGetStateFn>(GetProcAddress(m, "XInputGetState"));
      }
    }
    return XInputGetStateFn(nullptr);
  }();
  return get_state;
}

// Pads that aren't XInput ones, DirectInput pads such as the Logitech Dual Action (the owner's
// player 1 pad): XInput doesn't see them, so the launcher reads them through Windows' joystick API
// (winmm) with the usual layout of such pads, SDL's mapping for the Dual Action: buttons 1-4 = X,
// A, B, Y; 5 / 6 the shoulders, 7 / 8 the triggers, 9 Back, 10 Start, 11 / 12 the stick clicks,
// the hat the d-pad, X / Y the left stick, Z / R the right one. An XInput pad shows up there as
// well, with another layout, so a device with the vendor and product of an XInput device ("IG_"
// in its HID path, as Microsoft's XInput-and-DirectInput note tells them apart) is left out.
struct GenericPad {
  bool connected = false, skip = false;
  unsigned long vendor = 0;  // its USB vendor id (Sony, Nintendo: their button pictures in the hints)
  DWORD min[4] = {0, 0, 0, 0}, max[4] = {65535, 65535, 65535, 65535};  // X, Y, Z, R
};
inline GenericPad g_generic_pads[16];
inline std::mutex g_generic_mutex;  // the pad watch writes g_generic_pads, the frame copies it

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

// The real Portal of Power on USB, as the pad watch saw it last (every two seconds, #42): 0 none,
// 1 the PS3 / Wii / Wii U portal (1430:0150, every game's portal from Spyro's Adventure on), 2 the
// Xbox 360 portal (1430:1F17). The ids skylanders-portal's UsbPortal opens (kKnownPortals). Only
// looked at in the HID list, never opened: a game may be using it.
enum class UsbPortalKind { kNone, kPs3WiiU, kXbox360 };
inline std::atomic<UsbPortalKind> g_usb_portal{UsbPortalKind::kNone};

inline UsbPortalKind FindUsbPortal() {
  UINT count = 0;
  if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || !count) return UsbPortalKind::kNone;
  std::vector<RAWINPUTDEVICELIST> list(count);
  if (GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST)) == UINT(-1)) return UsbPortalKind::kNone;
  UsbPortalKind found = UsbPortalKind::kNone;
  for (UINT i = 0; i < count; ++i) {
    if (list[i].dwType != RIM_TYPEHID) continue;
    wchar_t name[512] = L"";
    UINT size = 512;  // characters for RIDI_DEVICENAME
    if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICENAME, name, &size) == UINT(-1)) continue;
    std::wstring path(name);
    for (wchar_t& c : path) c = wchar_t(std::towupper(c));
    if (path.find(L"VID_1430&PID_0150") != std::wstring::npos) return UsbPortalKind::kPs3WiiU;
    if (path.find(L"VID_1430&PID_1F17") != std::wstring::npos) found = UsbPortalKind::kXbox360;
  }
  return found;
}

// Looks again which joystick ids have a pad that isn't an XInput one (every two seconds).
// Under Wine / Proton the "IG_" test doesn't hold: Steam Input's virtual Xbox pad (and the real pad
// behind it) shows up through winmm as well, in the Xbox layout, which the Dual Action layout below
// turns into other buttons: B also pressed A, so leaving a game's settings changed its first row and
// "save the changes?" came up with changes nobody made (a colleague's Bazzite, #18, his fix). There
// every pad is an XInput one (Steam Input gives each pad as one): with an XInput pad connected winmm
// isn't read, nor Microsoft pads under Wine, nor Valve's virtual pads anywhere.
inline void FindGenericPads(bool xinput_connected) {
  const auto xinput = XInputVidPids();
  const bool wine = !WineVersion().empty();
  const UINT n = std::min<UINT>(joyGetNumDevs(), 16);
  GenericPad pads[16];
  {
    std::lock_guard lock(g_generic_mutex);
    std::copy(std::begin(g_generic_pads), std::end(g_generic_pads), pads);
  }
  for (UINT id = 0; id < 16; ++id) {
    GenericPad& pad = pads[id];
    JOYINFOEX info = {sizeof(info), JOY_RETURNALL};
    const bool was = pad.connected && !pad.skip;
    pad.connected = id < n && joyGetPosEx(id, &info) == JOYERR_NOERROR;
    if (!pad.connected) continue;
    JOYCAPSW caps = {};
    constexpr unsigned long kValve = 0x28DE, kMicrosoft = 0x045E;
    pad.skip = joyGetDevCapsW(id, &caps, sizeof(caps)) != JOYERR_NOERROR ||
               xinput.count({caps.wMid, caps.wPid}) != 0 || caps.wMid == kValve ||
               (wine && (xinput_connected || caps.wMid == kMicrosoft));
    if (!pad.skip && !was) {
      char line[160];
      std::snprintf(line, sizeof(line), "pads: joystick %u read as a DirectInput pad (vendor %04X, product %04X)", id,
                    unsigned(caps.wMid), unsigned(caps.wPid));
      HubLog(line);
    }
    if (pad.skip) continue;
    pad.vendor = caps.wMid;
    const DWORD mins[4] = {caps.wXmin, caps.wYmin, caps.wZmin, caps.wRmin};
    const DWORD maxs[4] = {caps.wXmax, caps.wYmax, caps.wZmax, caps.wRmax};
    for (int a = 0; a < 4; ++a) {
      pad.min[a] = mins[a];
      pad.max[a] = maxs[a] > mins[a] ? maxs[a] : mins[a] + 65535;
    }
  }
  std::lock_guard lock(g_generic_mutex);
  std::copy(std::begin(pads), std::end(pads), g_generic_pads);
}

// The pads that are there: every XInput slot and the winmm joysticks, every two seconds on a thread
// of its own, from the first frame on. Asking an empty XInput slot and 16 joysticks takes a while,
// and on the frame it showed as a hitch every two seconds (the sparks round the chosen card, the
// owner, 2026-10-25).
inline void StartPadWatch() {
  static std::once_flag once;
  std::call_once(once, [] {
    std::thread([] {
      for (;;) {
        bool any = false;
        for (DWORD pad = 0; PadGetState() && pad < XUSER_MAX_COUNT; ++pad) {
          XINPUT_STATE state = {};
          const bool now = PadGetState()(pad, &state) == ERROR_SUCCESS;
          if (g_pad_connected[pad].exchange(now) != now) {
            HubLog("pads: XInput slot " + std::to_string(pad) + (now ? " connected" : " gone"));
          }
          any = any || now;
        }
        FindGenericPads(any);
        const UsbPortalKind portal = FindUsbPortal();
        if (g_usb_portal.exchange(portal) != portal) {
          HubLog(portal == UsbPortalKind::kNone ? "portal: USB portal gone"
                                                : portal == UsbPortalKind::kXbox360 ? "portal: Xbox 360 USB portal plugged in"
                                                                                    : "portal: USB portal (PS3 / Wii / Wii U) plugged in");
        }
        Sleep(2000);
      }
    }).detach();
  });
}

// The generic pads as they are now (a copy: the pad watch may change them meanwhile).
inline void GenericPadsNow(GenericPad (&out)[16]) {
  std::lock_guard lock(g_generic_mutex);
  std::copy(std::begin(g_generic_pads), std::end(g_generic_pads), out);
}

// One generic pad's state as an XInput one, or false if it is gone.
inline bool ReadGenericPad(UINT id, const GenericPad& pad, XINPUT_GAMEPAD& out) {
  JOYINFOEX info = {sizeof(info), JOY_RETURNALL};
  if (joyGetPosEx(id, &info) != JOYERR_NOERROR) return false;
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
// one that doesn't say). Asked at most every ten seconds per pad, on a thread of its own (a wireless
// pad can take a moment to answer, a hitch on the frame).
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
  static std::atomic<int> level[XUSER_MAX_COUNT] = {-1, -1, -1, -1};
  static std::atomic<ULONGLONG> asked[XUSER_MAX_COUNT] = {};
  if (pad >= XUSER_MAX_COUNT || !get_battery) return -1;
  const ULONGLONG now = GetTickCount64();
  ULONGLONG last = asked[pad];
  if ((!last || now - last > 10000) && asked[pad].compare_exchange_strong(last, now)) {
    std::thread([pad] {
      XINPUT_BATTERY_INFORMATION info = {};
      int value = -1;
      if (get_battery(pad, BATTERY_DEVTYPE_GAMEPAD, &info) == ERROR_SUCCESS &&
          (info.BatteryType == BATTERY_TYPE_ALKALINE || info.BatteryType == BATTERY_TYPE_NIMH)) {
        value = info.BatteryLevel;  // BATTERY_LEVEL_EMPTY .. BATTERY_LEVEL_FULL, 0 .. 3
      }
      level[pad] = value;
    }).detach();
  }
  return level[pad];
}

// A short buzz on an XInput pad at `percent` of full strength: the controller page (#35) lets the
// new vibration strength be felt while it changes. PadBuzzUpdate (from UpdatePads, every frame)
// stops it after a quarter second.
inline ULONGLONG g_buzz_until[XUSER_MAX_COUNT] = {};

inline bool SetPadVibration(DWORD pad, WORD speed) {
  using SetState = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
  static SetState set_state = [] {
    for (const wchar_t* dll : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
      if (HMODULE m = LoadLibraryW(dll)) {
        if (auto f = reinterpret_cast<SetState>(GetProcAddress(m, "XInputSetState"))) return f;
      }
    }
    return SetState(nullptr);
  }();
  XINPUT_VIBRATION v = {speed, speed};
  return set_state && set_state(pad, &v) == ERROR_SUCCESS;
}

inline void PadBuzz(DWORD pad, int percent) {
  if (pad >= XUSER_MAX_COUNT) return;
  const WORD speed = WORD(65535u * unsigned(std::clamp(percent, 0, 100)) / 100u);
  if (SetPadVibration(pad, speed)) g_buzz_until[pad] = speed ? GetTickCount64() + 250 : 0;
}

inline void PadBuzzUpdate() {
  const ULONGLONG now = GetTickCount64();
  for (DWORD pad = 0; pad < XUSER_MAX_COUNT; ++pad) {
    if (g_buzz_until[pad] && now >= g_buzz_until[pad]) {
      g_buzz_until[pad] = 0;
      SetPadVibration(pad, 0);
    }
  }
}

// Every connected XInput pad (slots 0-3) as Dear ImGui's gamepad keys, merged: its Win32 backend
// reads only slot 0, so a second pad, or one that came back in another slot, did nothing
// (IMGUI_IMPL_WIN32_DISABLE_GAMEPAD is set for it). Which slots are connected is looked at every
// two seconds; polling an empty slot every frame is slow. Call after ImGui_ImplWin32_NewFrame.
inline void UpdatePads(ImGuiIO& io) {
  StartPadWatch();
  PadBuzzUpdate();
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
  for (DWORD pad = 0; PadGetState() && pad < XUSER_MAX_COUNT; ++pad) {
    if (!g_pad_connected[pad]) continue;
    XINPUT_STATE state = {};
    if (PadGetState()(pad, &state) != ERROR_SUCCESS) continue;
    merge(state.Gamepad);
    if (state.Gamepad.wButtons) SetHintPad(HintPad::kXbox);
  }
  // The DirectInput pads, merged the same way; the hints follow the maker of the one pressed (#29).
  GenericPad generic[16];
  GenericPadsNow(generic);
  for (UINT id = 0; id < 16; ++id) {
    if (!generic[id].connected || generic[id].skip) continue;
    XINPUT_GAMEPAD g;
    if (!ReadGenericPad(id, generic[id], g)) continue;
    merge(g);
    if (g.wButtons) {
      constexpr unsigned long kSony = 0x054C, kNintendo = 0x057E;
      SetHintPad(generic[id].vendor == kSony       ? HintPad::kPlayStation
                 : generic[id].vendor == kNintendo ? HintPad::kNintendo
                                                   : HintPad::kXbox);
    }
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

// A controller button held, or (with `keys`) Enter, Space or Esc.
inline bool InputHeld(bool keys = true) {
  for (DWORD pad = 0; PadGetState() && pad < XUSER_MAX_COUNT; ++pad) {
    XINPUT_STATE state = {};
    if (!g_pad_connected[pad] || PadGetState()(pad, &state) != ERROR_SUCCESS) continue;
    const XINPUT_GAMEPAD& g = state.Gamepad;
    if (g.wButtons || g.bLeftTrigger > 30 || g.bRightTrigger > 30) return true;
  }
  GenericPad generic[16];
  GenericPadsNow(generic);
  for (UINT id = 0; id < 16; ++id) {
    if (!generic[id].connected || generic[id].skip) continue;
    XINPUT_GAMEPAD g;
    if (ReadGenericPad(id, generic[id], g) && (g.wButtons || g.bLeftTrigger || g.bRightTrigger)) return true;
  }
  for (const int key : {VK_RETURN, VK_SPACE, VK_ESCAPE}) {
    if (keys && (GetAsyncKeyState(key) & 0x8000)) return true;
  }
  return false;
}

}  // namespace coreoflight
