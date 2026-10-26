#include "handheld.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string_view>

#include "hub_log.h"

namespace coreoflight {

namespace {

// The main screen's diagonal in inches from its raw DPI (shcore's GetDpiForMonitor, MDT_RAW_DPI =
// 2: the panel's real pixels per inch from its EDID), loaded by hand so nothing more is linked;
// 0 when Windows doesn't know it.
float ScreenInches() {
  using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
  HMODULE shcore = LoadLibraryW(L"shcore.dll");
  if (!shcore) return 0;
  auto get_dpi = reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor"));
  HMONITOR monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
  UINT dpi_x = 0, dpi_y = 0;
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  float inches = 0;
  if (get_dpi && SUCCEEDED(get_dpi(monitor, 2, &dpi_x, &dpi_y)) && dpi_x && dpi_y &&
      GetMonitorInfoW(monitor, &info)) {
    // The mode's pixels are the panel's own on a handheld (it runs at its native size).
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
      const float w = float(mode.dmPelsWidth) / float(dpi_x), h = float(mode.dmPelsHeight) / float(dpi_y);
      inches = std::sqrt(w * w + h * h);
    }
  }
  FreeLibrary(shcore);
  return inches;
}

std::string CpuName() {
  wchar_t name[256] = L"";
  DWORD size = sizeof(name);
  if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString",
                   RRF_RT_REG_SZ, nullptr, name, &size) != ERROR_SUCCESS) {
    return {};
  }
  std::string out;
  for (const wchar_t* c = name; *c; ++c) out += *c < 128 ? char(*c) : '?';
  out.erase(0, out.find_first_not_of(' '));
  out.erase(out.find_last_not_of(' ') + 1);  // the name is padded with spaces
  return out;
}

}  // namespace

const HandheldInfo& DetectHandheld() {
  static const HandheldInfo info = [] {
    HandheldInfo i;
    i.screen_inches = ScreenInches();
    i.cpu = CpuName();
    std::string lower = i.cpu;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    // The chips made for handhelds: AMD's Ryzen Z1 / Z2 (ROG Ally, Legion Go, ...) and the Steam
    // Deck's (LCD and OLED).
    bool chip = false;
    for (const std::string_view name : {"ryzen z1", "ryzen z2", "custom apu 0405", "custom apu 0932"}) {
      chip = chip || lower.find(name) != std::string::npos;
    }
    i.detected = chip || (i.screen_inches > 0 && i.screen_inches <= 9.0f);
    char line[300];
    std::snprintf(line, sizeof(line), "handheld: screen %.1f\", cpu \"%s\" -> %s", double(i.screen_inches),
                  i.cpu.c_str(), i.detected ? "handheld" : "not a handheld");
    HubLog(line);
    return i;
  }();
  return info;
}

bool HandheldActive(const std::string& setting) {
  if (setting == "on") return true;
  if (setting == "off") return false;
  return DetectHandheld().detected;
}

}  // namespace coreoflight
