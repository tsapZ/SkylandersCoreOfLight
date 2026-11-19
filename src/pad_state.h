#pragma once

// The pads for the launcher: every XInput slot and every DirectInput pad, and whether a button is still held right now: any connected pad's buttons or triggers (XInput,
// loaded by hand like Dear ImGui's Win32 backend does), or Enter, Space or Esc on the keyboard.
// For the moment a game ends and the launcher comes back (main.cpp).

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <setupapi.h>
#include <xinput.h>
#pragma comment(lib, "setupapi.lib")
#else
#include <SDL3/SDL.h>
#include <libusb.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <cwchar>
#include <cwctype>
#endif
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "hints.h"
#include "hub_log.h"
#include "imgui.h"
#include "platform.h"

namespace coreoflight {

inline constexpr size_t kMaxPads = 4;

// A physical Portal of Power plugged into USB (VID 0x1430: PID 0x0150 PS3/Wii/Wii U/PC, PID 0x1F17 Xbox 360).
enum class UsbPortalKind { kNone, kPs3WiiU, kXbox360 };
inline std::atomic<UsbPortalKind> g_usb_portal{UsbPortalKind::kNone};

#ifdef _WIN32

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
//
// Not every such device is a pad: a keyboard can bring a joystick part of its own (a Keychron one,
// 3434:D030, #50), with no hat, whose missing hat reads as "up", or axes that sit at an edge, and the
// menus moved by themselves. So only the axes and the hat a device says it has are read, and each
// only after it has been seen at rest once (a stick in the middle, the hat let go): a real pad is at
// rest when it comes, a stuck axis never is.
struct GenericPad {
  bool connected = false, skip = false;
  unsigned long vendor = 0;  // its USB vendor id (Sony, Nintendo: their button pictures in the hints)
  DWORD min[4] = {0, 0, 0, 0}, max[4] = {65535, 65535, 65535, 65535};  // X, Y, Z, R
  bool has_axis[4] = {true, true, false, false};  // X, Y, Z, R as the device's caps tell them
  bool has_pov = false;
};
inline GenericPad g_generic_pads[16];
// Per joystick id, which of X, Y, Z, R (0-3) and the hat (4) have been seen at rest since the device
// came (the frame thread sets them, the pad watch clears them when a device comes).
inline std::atomic<bool> g_generic_rested[16][5] = {};
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
// looked at in the HID list and, for a portal on the WinUSB driver (Zadig, for Dolphin or Cemu, #58),
// in the USB device list; never opened: a game may be using it.
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
  if (found != UsbPortalKind::kNone) return found;
  // Not a HID device: a portal on WinUSB, which the games open too.
  HDEVINFO devices = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
  if (devices == INVALID_HANDLE_VALUE) return found;
  SP_DEVINFO_DATA info{};
  info.cbSize = sizeof(info);
  for (DWORD i = 0; SetupDiEnumDeviceInfo(devices, i, &info); ++i) {
    wchar_t ids[512] = {};
    wchar_t service[64] = {};
    if (!SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_HARDWAREID, nullptr, reinterpret_cast<PBYTE>(ids),
                                           sizeof(ids) - 2 * sizeof(wchar_t), nullptr) ||
        !SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_SERVICE, nullptr, reinterpret_cast<PBYTE>(service),
                                           sizeof(service) - sizeof(wchar_t), nullptr) ||
        _wcsicmp(service, L"WinUSB") != 0) {
      continue;
    }
    if (wcsstr(ids, L"VID_1430&PID_0150")) {
      found = UsbPortalKind::kPs3WiiU;
      break;
    }
    if (wcsstr(ids, L"VID_1430&PID_1F17")) found = UsbPortalKind::kXbox360;
  }
  SetupDiDestroyDeviceInfoList(devices);
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
    if (!was) {
      for (auto& rested : g_generic_rested[id]) rested = false;
    }
    pad.vendor = caps.wMid;
    pad.has_axis[0] = pad.has_axis[1] = caps.wNumAxes >= 2;
    pad.has_axis[2] = (caps.wCaps & JOYCAPS_HASZ) != 0;
    pad.has_axis[3] = (caps.wCaps & JOYCAPS_HASR) != 0;
    pad.has_pov = (caps.wCaps & JOYCAPS_HASPOV) != 0;
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
  // The hat in hundredths of a degree, 0 up, clockwise; a diagonal gives both directions. Only one
  // the device has, once it has been let go (see GenericPad).
  const DWORD pov = info.dwPOV;
  const bool pov_centered = LOWORD(pov) == 0xFFFF || pov >= 36000;
  if (pad.has_pov && pov_centered) g_generic_rested[id][4] = true;
  if (pad.has_pov && g_generic_rested[id][4] && !pov_centered) {
    if (pov >= 31500 || pov <= 4500) out.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
    if (pov >= 4500 && pov <= 13500) out.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
    if (pov >= 13500 && pov <= 22500) out.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
    if (pov >= 22500 && pov <= 31500) out.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
  }
  // Axes to XInput's -32768..32767, y up; one the device hasn't, or that hasn't been seen near the
  // middle yet, reads 0 (see GenericPad).
  auto axis = [&](int a, DWORD v, bool invert) {
    if (!pad.has_axis[a]) return SHORT(0);
    const double t = (double(v) - double(pad.min[a])) / double(pad.max[a] - pad.min[a]);
    const int s = int(std::clamp(t * 65535.0 - 32768.0, -32768.0, 32767.0));
    if (std::abs(s) < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) g_generic_rested[id][a] = true;
    if (!g_generic_rested[id][a]) return SHORT(0);
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
inline int PadBattery(uint32_t pad) {
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
      if (get_battery(DWORD(pad), BATTERY_DEVTYPE_GAMEPAD, &info) == ERROR_SUCCESS &&
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

inline bool SetPadVibration(uint32_t pad, WORD speed) {
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
  return set_state && set_state(DWORD(pad), &v) == ERROR_SUCCESS;
}

inline void PadBuzz(uint32_t pad, int percent) {
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

inline size_t ExtraPadCount() {
  GenericPad generic[16];
  GenericPadsNow(generic);
  size_t count = 0;
  for (const GenericPad& pad : generic) {
    if (pad.connected && !pad.skip) ++count;
  }
  return count;
}

#else

inline std::atomic<bool> g_pad_connected[kMaxPads] = {};

struct SdlPadSlot {
  SDL_JoystickID id = 0;
  SDL_Gamepad* handle = nullptr;
};
inline SdlPadSlot g_sdl_pads[kMaxPads] = {};
inline std::mutex g_sdl_pad_mutex;
inline uint64_t g_buzz_until[kMaxPads] = {};

// Passively enumerates USB device descriptors via libusb without opening or claiming interfaces.
inline UsbPortalKind FindUsbPortal() {
  libusb_context* ctx = nullptr;
  if (libusb_init(&ctx) != 0) return UsbPortalKind::kNone;
  libusb_device** list = nullptr;
  const ssize_t count = libusb_get_device_list(ctx, &list);
  UsbPortalKind result = UsbPortalKind::kNone;
  for (ssize_t i = 0; i < count; ++i) {
    libusb_device_descriptor desc{};
    if (libusb_get_device_descriptor(list[i], &desc) != 0 || desc.idVendor != 0x1430) continue;
    if (desc.idProduct == 0x0150) {
      result = UsbPortalKind::kPs3WiiU;
      break;
    }
    if (desc.idProduct == 0x1F17) {
      result = UsbPortalKind::kXbox360;
      break;
    }
  }
  if (list) libusb_free_device_list(list, 1);
  libusb_exit(ctx);
  return result;
}

inline void StartPadWatch() {
  static const bool started = [] {
    std::thread([] {
      for (;;) {
        const UsbPortalKind portal = FindUsbPortal();
        if (g_usb_portal.exchange(portal) != portal) {
          HubLog(std::string("usb: Portal of Power ") +
                 (portal == UsbPortalKind::kXbox360 ? "Xbox 360"
                  : portal == UsbPortalKind::kPs3WiiU ? "PS3/Wii/PC"
                                                      : "gone"));
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
      }
    }).detach();
    return true;
  }();
  (void)started;
}

inline void SyncSdlPads() {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  std::lock_guard lock(g_sdl_pad_mutex);
  for (size_t s = 0; s < kMaxPads; ++s) {
    if (!g_sdl_pads[s].handle) continue;
    bool still_here = false;
    for (int i = 0; i < count && ids; ++i) {
      if (ids[i] == g_sdl_pads[s].id) {
        still_here = true;
        break;
      }
    }
    if (!still_here) {
      HubLog("pad " + std::to_string(s + 1) + ": gone");
      SDL_CloseGamepad(g_sdl_pads[s].handle);
      g_sdl_pads[s] = {};
      g_buzz_until[s] = 0;
    }
  }
  for (int i = 0; i < count && ids; ++i) {
    bool tracked = false;
    for (size_t s = 0; s < kMaxPads; ++s) {
      if (g_sdl_pads[s].handle && g_sdl_pads[s].id == ids[i]) {
        tracked = true;
        break;
      }
    }
    if (tracked) continue;
    for (size_t s = 0; s < kMaxPads; ++s) {
      if (g_sdl_pads[s].handle) continue;
      if (SDL_Gamepad* gp = SDL_OpenGamepad(ids[i])) {
        g_sdl_pads[s] = {ids[i], gp};
        const char* name = SDL_GetGamepadName(gp);
        HubLog("pad " + std::to_string(s + 1) + ": " + (name ? name : "controller"));
      }
      break;
    }
  }
  SDL_free(ids);
  for (size_t s = 0; s < kMaxPads; ++s) g_pad_connected[s].store(g_sdl_pads[s].handle != nullptr);
}

inline int PadBattery(uint32_t pad) {
  std::lock_guard lock(g_sdl_pad_mutex);
  if (pad >= kMaxPads || !g_sdl_pads[pad].handle) return -1;
  int pct = -1;
  const SDL_PowerState st = SDL_GetGamepadPowerInfo(g_sdl_pads[pad].handle, &pct);
  if ((st == SDL_POWERSTATE_ON_BATTERY || st == SDL_POWERSTATE_CHARGING || st == SDL_POWERSTATE_CHARGED) &&
      pct >= 0) {
    if (pct >= 75) return 3;
    if (pct >= 45) return 2;
    if (pct >= 15) return 1;
    return 0;
  }
  return -1;
}

inline void PadBuzz(uint32_t pad, int percent) {
  std::lock_guard lock(g_sdl_pad_mutex);
  if (pad >= kMaxPads || !g_sdl_pads[pad].handle) return;
  const uint16_t speed = uint16_t(65535u * unsigned(std::clamp(percent, 0, 100)) / 100u);
  SDL_RumbleGamepad(g_sdl_pads[pad].handle, speed, speed, 250);
  g_buzz_until[pad] = speed ? MonotonicMs() + 250 : 0;
}

inline void PadBuzzUpdate() {
  const uint64_t now = MonotonicMs();
  std::lock_guard lock(g_sdl_pad_mutex);
  for (size_t pad = 0; pad < kMaxPads; ++pad) {
    if (g_buzz_until[pad] && now >= g_buzz_until[pad]) {
      g_buzz_until[pad] = 0;
      if (g_sdl_pads[pad].handle) SDL_RumbleGamepad(g_sdl_pads[pad].handle, 0, 0, 0);
    }
  }
}

inline HintPad HintPadForSdlType(SDL_GamepadType type) {
  switch (type) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
      return HintPad::kPlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
      return HintPad::kNintendo;
    default:
      return HintPad::kXbox;
  }
}

inline void UpdatePads(ImGuiIO& io) {
  StartPadWatch();
  SyncSdlPads();
  PadBuzzUpdate();

  bool any = false;
  bool buttons[SDL_GAMEPAD_BUTTON_COUNT] = {};
  int16_t lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
  auto pick = [](int16_t& dst, int16_t src) {
    if (std::abs(int(src)) > std::abs(int(dst))) dst = src;
  };

  {
    std::lock_guard lock(g_sdl_pad_mutex);
    for (size_t s = 0; s < kMaxPads; ++s) {
      SDL_Gamepad* gp = g_sdl_pads[s].handle;
      if (!gp) continue;
      any = true;
      bool pressed_here = false;
      for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
        if (SDL_GetGamepadButton(gp, SDL_GamepadButton(b))) {
          buttons[b] = true;
          pressed_here = true;
        }
      }
      const int16_t glt = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
      const int16_t grt = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
      if (glt > lt) lt = glt;
      if (grt > rt) rt = grt;
      pick(lx, SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX));
      pick(ly, SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY));
      pick(rx, SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTX));
      pick(ry, SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTY));
      if (pressed_here || glt > 3800 || grt > 3800) {
        SetHintPad(HintPadForSdlType(SDL_GetGamepadType(gp)));
      }
    }
  }

  io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
  if (!any) return;
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

  auto analog = [&](ImGuiKey key, float value, float v0, float v1) {
    const float v = std::clamp((value - v0) / (v1 - v0), 0.0f, 1.0f);
    io.AddKeyAnalogEvent(key, v > 0.10f, v);
  };
  io.AddKeyEvent(ImGuiKey_GamepadStart, buttons[SDL_GAMEPAD_BUTTON_START]);
  io.AddKeyEvent(ImGuiKey_GamepadBack, buttons[SDL_GAMEPAD_BUTTON_BACK]);
  io.AddKeyEvent(ImGuiKey_GamepadFaceLeft, buttons[SDL_GAMEPAD_BUTTON_WEST]);
  io.AddKeyEvent(ImGuiKey_GamepadFaceRight, buttons[SDL_GAMEPAD_BUTTON_EAST]);
  io.AddKeyEvent(ImGuiKey_GamepadFaceUp, buttons[SDL_GAMEPAD_BUTTON_NORTH]);
  io.AddKeyEvent(ImGuiKey_GamepadFaceDown, buttons[SDL_GAMEPAD_BUTTON_SOUTH]);
  io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, buttons[SDL_GAMEPAD_BUTTON_DPAD_LEFT]);
  io.AddKeyEvent(ImGuiKey_GamepadDpadRight, buttons[SDL_GAMEPAD_BUTTON_DPAD_RIGHT]);
  io.AddKeyEvent(ImGuiKey_GamepadDpadUp, buttons[SDL_GAMEPAD_BUTTON_DPAD_UP]);
  io.AddKeyEvent(ImGuiKey_GamepadDpadDown, buttons[SDL_GAMEPAD_BUTTON_DPAD_DOWN]);
  io.AddKeyEvent(ImGuiKey_GamepadL1, buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER]);
  io.AddKeyEvent(ImGuiKey_GamepadR1, buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER]);
  io.AddKeyEvent(ImGuiKey_GamepadL3, buttons[SDL_GAMEPAD_BUTTON_LEFT_STICK]);
  io.AddKeyEvent(ImGuiKey_GamepadR3, buttons[SDL_GAMEPAD_BUTTON_RIGHT_STICK]);
  analog(ImGuiKey_GamepadL2, float(lt), 3800.0f, 32767.0f);
  analog(ImGuiKey_GamepadR2, float(rt), 3800.0f, 32767.0f);
  constexpr float kDead = 7849.0f;
  analog(ImGuiKey_GamepadLStickLeft, float(lx), -kDead, -32768.0f);
  analog(ImGuiKey_GamepadLStickRight, float(lx), +kDead, +32767.0f);
  // SDL Y axes are negative up and positive down.
  analog(ImGuiKey_GamepadLStickUp, float(ly), -kDead, -32768.0f);
  analog(ImGuiKey_GamepadLStickDown, float(ly), +kDead, +32767.0f);
  analog(ImGuiKey_GamepadRStickLeft, float(rx), -kDead, -32768.0f);
  analog(ImGuiKey_GamepadRStickRight, float(rx), +kDead, +32767.0f);
  analog(ImGuiKey_GamepadRStickUp, float(ry), -kDead, -32768.0f);
  analog(ImGuiKey_GamepadRStickDown, float(ry), +kDead, +32767.0f);
}

inline bool InputHeld(bool keys = true) {
  {
    std::lock_guard lock(g_sdl_pad_mutex);
    for (size_t s = 0; s < kMaxPads; ++s) {
      SDL_Gamepad* gp = g_sdl_pads[s].handle;
      if (!gp) continue;
      for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
        if (SDL_GetGamepadButton(gp, SDL_GamepadButton(b))) return true;
      }
      if (SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 3800 ||
          SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 3800) {
        return true;
      }
    }
  }
  if (keys) {
    int count = 0;
    const bool* state = SDL_GetKeyboardState(&count);
    for (const SDL_Scancode sc : {SDL_SCANCODE_RETURN, SDL_SCANCODE_SPACE, SDL_SCANCODE_ESCAPE}) {
      if (sc < count && state && state[sc]) return true;
    }
  }
  return false;
}

inline size_t ExtraPadCount() { return 0; }

// Closes SDL gamepad handles before SDL_Quit so teardown does not leave open devices.
inline void ShutdownSdlPads() {
  std::lock_guard lock(g_sdl_pad_mutex);
  for (size_t s = 0; s < kMaxPads; ++s) {
    if (g_sdl_pads[s].handle) {
      SDL_CloseGamepad(g_sdl_pads[s].handle);
      g_sdl_pads[s] = {};
    }
    g_buzz_until[s] = 0;
    g_pad_connected[s].store(false);
  }
}

#endif  // _WIN32

inline bool AnyControllerConnected() {
  for (const auto& c : g_pad_connected) {
    if (c.load()) return true;
  }
  return ExtraPadCount() > 0;
}

}  // namespace coreoflight
