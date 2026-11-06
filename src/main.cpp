// Skylanders: Core of Light: the launcher window (Win32 + Direct3D 11 + Dear ImGui, as Dear
// ImGui's own Win32 / DirectX 11 example sets them up). The screen itself is launcher_ui.cpp.

#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <string>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "gpu_tier.h"
#include "hints.h"
#include "hub_log.h"
#include "hub_settings.h"
#include "launcher_ui.h"
#include "pad_state.h"
#include "portable.h"
#include "portal/data_folder.h"
#include "textures.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swap_chain = nullptr;
ID3D11RenderTargetView* g_target = nullptr;
UINT g_resize_width = 0, g_resize_height = 0;
coreoflight::LauncherUi* g_ui = nullptr;  // for a file dropped on the window

// Borderless fullscreen on the window's monitor, or back to the window it was.
void SetFullscreen(HWND hwnd, bool on) {
  static WINDOWPLACEMENT windowed = {sizeof(windowed)};
  const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
  const bool is_full = !(style & WS_OVERLAPPEDWINDOW);
  if (on == is_full) return;
  if (on) {
    MONITORINFO mi = {sizeof(mi)};
    if (!GetWindowPlacement(hwnd, &windowed) ||
        !GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
      return;
    }
    SetWindowLongW(hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
    SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  } else {
    SetWindowLongW(hwnd, GWL_STYLE, (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
    SetWindowPlacement(hwnd, &windowed);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  }
}

// Whether the process has a visible top-level window (the game's, once it opened).
// Brings the launcher's window to the front and gives it the keyboard focus. Without the focus
// Dear ImGui drops every input each frame (io.AppFocusLost: the keys are cleared), the controllers'
// too, and Windows lets a program take the foreground only in some cases: after a game closed with
// Alt+F4 the launcher often isn't one of them (a colleague's report: the pad didn't steer it any
// more). Joining the foreground window's input thread for the call is the usual way through.
void TakeForeground(HWND hwnd) {
  const HWND front = GetForegroundWindow();
  if (front == hwnd) return;
  const DWORD front_thread = front ? GetWindowThreadProcessId(front, nullptr) : 0;
  const DWORD own_thread = GetCurrentThreadId();
  const bool joined = front_thread && front_thread != own_thread && AttachThreadInput(front_thread, own_thread, TRUE);
  ShowWindow(hwnd, SW_RESTORE);
  BringWindowToTop(hwnd);
  SetForegroundWindow(hwnd);
  SetFocus(hwnd);
  if (joined) AttachThreadInput(front_thread, own_thread, FALSE);
}

bool HasVisibleWindow(DWORD pid) {
  struct Find {
    DWORD pid;
    bool found;
  } find{pid, false};
  EnumWindows(
      [](HWND w, LPARAM p) -> BOOL {
        auto* f = reinterpret_cast<Find*>(p);
        DWORD owner = 0;
        GetWindowThreadProcessId(w, &owner);
        if (owner == f->pid && IsWindowVisible(w)) {
          f->found = true;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&find));
  return find.found;
}

// Whether the game still has its window, shown or not: the games draw into an SDL window (class
// "SDL_app"). Not just a visible one: a hidden or minimized window is still a game that runs.
bool HasGameWindow(DWORD pid) {
  struct Find {
    DWORD pid;
    bool found;
  } find{pid, false};
  EnumWindows(
      [](HWND w, LPARAM p) -> BOOL {
        auto* f = reinterpret_cast<Find*>(p);
        DWORD owner = 0;
        GetWindowThreadProcessId(w, &owner);
        wchar_t name[32] = L"";
        if (owner == f->pid && GetClassNameW(w, name, 32) && std::wcscmp(name, L"SDL_app") == 0) {
          f->found = true;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&find));
  return find.found;
}

void CreateTarget() {
  ID3D11Texture2D* back = nullptr;
  g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back));
  g_device->CreateRenderTargetView(back, nullptr, &g_target);
  back->Release();
}

void ReleaseTarget() {
  if (g_target) {
    g_target->Release();
    g_target = nullptr;
  }
}

bool CreateDevice(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC sd = {};
  sd.BufferCount = 2;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;  // the flip model, as the games present
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL level;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                             D3D11_SDK_VERSION, &sd, &g_swap_chain, &g_device, &level, &g_context);
  if (hr == DXGI_ERROR_UNSUPPORTED) {  // no GPU driver: WARP
    hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                       &sd, &g_swap_chain, &g_device, &level, &g_context);
  }
  if (hr != S_OK) return false;
  CreateTarget();
  return true;
}

void ReleaseDevice() {
  ReleaseTarget();
  if (g_swap_chain) g_swap_chain->Release();
  if (g_context) g_context->Release();
  if (g_device) g_device->Release();
}

// While a game runs and for 30 s after it: the window's focus goes into the log then (a report from
// a PC where the pad stopped after a game). Otherwise every switch to another program wrote two
// lines and buried the rest (the owner, reading a report, 2026-10-31).
bool g_game_running = false;
ULONGLONG g_focus_log_until = 0;

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if ((msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_ACTIVATEAPP) &&
      (g_game_running || GetTickCount64() < g_focus_log_until)) {
    coreoflight::HubLog(msg == WM_SETFOCUS    ? "window: focus"
                        : msg == WM_KILLFOCUS ? "window: focus lost"
                        : wparam              ? "window: app active"
                                              : "window: app inactive");
  }
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return true;
  switch (msg) {
    case WM_SIZE:
      if (wparam != SIZE_MINIMIZED) {
        g_resize_width = LOWORD(lparam);
        g_resize_height = HIWORD(lparam);
      }
      return 0;
    case WM_SYSCOMMAND:
      if ((wparam & 0xfff0) == SC_KEYMENU) return 0;  // no ALT menu
      break;
    case WM_SYSKEYDOWN:
      // Alt+Enter: fullscreen on / off, as in Big Picture and many games.
      if (wparam == VK_RETURN && (HIWORD(lparam) & KF_ALTDOWN) && g_ui) {
        g_ui->ToggleFullscreen();
        return 0;
      }
      break;
    case WM_KEYDOWN:
      if (wparam == VK_F11 && g_ui && !(HIWORD(lparam) & KF_REPEAT)) g_ui->ToggleFullscreen();
      break;
    case WM_DROPFILES: {
      // An ISO dropped on the window: set up like one picked with X.
      const HDROP drop = reinterpret_cast<HDROP>(wparam);
      wchar_t path[MAX_PATH * 4] = L"";
      if (g_ui && DragQueryFileW(drop, 0, path, UINT(std::size(path)))) g_ui->AddDisc(path);
      DragFinish(drop);
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

std::filesystem::path LauncherDir() {
  wchar_t path[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
  return std::filesystem::path(std::wstring(path, n)).parent_path();
}

// Windows' own font (Segoe UI), else the one in the launcher's fonts folder (Noto Sans, SIL OFL):
// under Wine / Proton there is no Segoe UI, and Dear ImGui's built-in pixel font made the cards'
// lines run into each other (a colleague's Bazzite, 2026-10-24).
bool g_own_font = false;  // --own_font: the launcher's own font even where Segoe UI is (to see it)
std::string g_fonts;      // the fonts LoadFont took, for the start line

ImFont* LoadFont(ImGuiIO& io, const wchar_t* windows_file, const wchar_t* own_file) {
  wchar_t windows[MAX_PATH];
  GetWindowsDirectoryW(windows, MAX_PATH);
  for (const std::filesystem::path& path :
       {std::filesystem::path(windows) / "Fonts" / windows_file, LauncherDir() / "fonts" / own_file}) {
    std::error_code ec;
    if (g_own_font && path.parent_path().filename() == "Fonts") continue;
    if (!std::filesystem::exists(path, ec)) continue;
    // The atlas reads the file by its UTF-8 name (the launcher folder may have any characters).
    const std::u8string u8 = path.u8string();
    if (ImFont* font = io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(u8.c_str()), 32.0f)) {
      g_fonts += (g_fonts.empty() ? "" : " ") + std::string(reinterpret_cast<const char*>(path.filename().u8string().c_str()));
      return font;
    }
  }
  g_fonts += g_fonts.empty() ? "none (Dear ImGui's own)" : " none";
  return nullptr;
}

// The launcher ending by a crash says so in its log (it vanished without a word before): an
// exception nobody caught, or a fault with its code and address.
void LogTerminate() {
  std::string what = "unknown";
  if (const std::exception_ptr e = std::current_exception()) {
    try {
      std::rethrow_exception(e);
    } catch (const std::exception& ex) {
      what = ex.what();
    } catch (...) {
    }
  }
  coreoflight::HubError("launcher: ended by an uncaught exception: " + what);
  std::abort();
}

DWORD g_main_thread = 0;

// "name.dll+0x1234": the module an address is in and the offset in it (only the file name, never
// the folder), or the bare address when no module holds it. A fault in a driver or a DLL Windows or
// another program put into the launcher was "exe+0x827802E74" before, useless (a player's report,
// 2026-11-06).
void DescribeAddress(DWORD64 address, char* out, size_t size) {
  HMODULE module = nullptr;
  wchar_t path[MAX_PATH] = L"";
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(address), &module) &&
      GetModuleFileNameW(module, path, MAX_PATH)) {
    const wchar_t* name = std::wcsrchr(path, L'\\');
    std::snprintf(out, size, "%ls+0x%llX", name ? name + 1 : path,
                  static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)));
  } else {
    std::snprintf(out, size, "0x%llX", static_cast<unsigned long long>(address));
  }
}

LONG WINAPI LogFault(EXCEPTION_POINTERS* info) {
  const EXCEPTION_RECORD& record = *info->ExceptionRecord;
  char where[192], line[512];
  DescribeAddress(reinterpret_cast<DWORD64>(record.ExceptionAddress), where, sizeof(where));
  // An access violation also says what it touched: a read of 0x0 is a null pointer.
  char touched[64] = "";
  if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
    const ULONG_PTR kind = record.ExceptionInformation[0];
    std::snprintf(touched, sizeof(touched), ", %s 0x%llX", kind == 0 ? "reading" : kind == 1 ? "writing" : "running",
                  static_cast<unsigned long long>(record.ExceptionInformation[1]));
  }
  std::snprintf(line, sizeof(line), "launcher: crashed, exception 0x%08lX in %s%s, %s thread", record.ExceptionCode,
                where, touched, GetCurrentThreadId() == g_main_thread ? "main" : "another");
  coreoflight::HubError(line);  // first, in case the walk below faults on a broken stack
#if defined(_M_X64)
  // The calls that led there, unwound with the modules' own x64 unwind data from the faulting
  // frame up: module and offset each, enough to look them up in the build's .pdb.
  CONTEXT context = *info->ContextRecord;
  ULONG_PTR low = 0, high = 0;
  GetCurrentThreadStackLimits(&low, &high);
  std::string calls = "launcher: called from";
  int frames = 0;
  for (int i = 0; i < 12; ++i) {
    DWORD64 base = 0;
    PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &base, nullptr);
    if (function) {
      void* handler_data = nullptr;
      DWORD64 frame = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context.Rip, function, &context, &handler_data, &frame, nullptr);
    } else {
      // A leaf function: the return address is on top of the stack.
      if (context.Rsp < low || context.Rsp + 8 > high) break;
      context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
      context.Rsp += 8;
    }
    if (!context.Rip || context.Rsp < low || context.Rsp >= high) break;
    DescribeAddress(context.Rip, where, sizeof(where));
    calls += std::string(frames++ ? ", " : " ") + where;
  }
  if (frames) coreoflight::HubError(calls);
#endif
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  std::set_terminate(LogTerminate);
  g_main_thread = GetCurrentThreadId();
  SetUnhandledExceptionFilter(LogFault);
  ImGui_ImplWin32_EnableDpiAwareness();
  WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr,
                    L"CoreOfLight", nullptr};
  // The launcher's own icon (coreoflight.rc), in the window's title and the taskbar.
  wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
  wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                             GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
  RegisterClassExW(&wc);
  // --documents=<folder>: that folder in place of Documents (figures, pictures, profiles, the
  // games' user data), to try the launcher on a copy. An ISO dragged onto coreoflight.exe (or
  // named on its command line) is set up at once. Read before the window: --documents decides
  // which settings file says whether it starts in fullscreen.
  std::wstring iso;
  std::wstring portable_update;
  bool fullscreen_arg = false;
  int argc = 0;
  if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
    for (int i = 1; i < argc; ++i) {
      const std::wstring arg = argv[i];
      if (arg.starts_with(L"--documents=")) {
        coreoflight::SetDocumentsDir(arg.substr(12));
      } else if (arg.starts_with(L"--install_dir=")) {
        // A library folder for games installed from the launcher (games.h StartGameInstall).
        coreoflight::SetInstallDir(arg.substr(14));
      } else if (arg == L"--no_game_art") {
        // Screenshots to show: none of the games' pictures (textures.h), and the hints stay the
        // controller's while the screenshot script presses keys (hints.h).
        coreoflight::SetNoGameArt(true);
        coreoflight::KeepControllerHints();
      } else if (arg == L"--own_font") {
        g_own_font = true;  // the fallback font (Noto Sans) as under Wine
      } else if (arg == L"--offline") {
        // No releases, no check, nothing downloaded or installed from GitHub (scripted tests).
        coreoflight::SetOffline(true);
      } else if (arg == L"--hint_pad=playstation" || arg == L"--hint_pad=nintendo") {
        // The hints as that controller's from the start, whatever pad is used (#29; to see them
        // without such a pad).
        coreoflight::ForceHintPad(arg.ends_with(L"playstation") ? coreoflight::HintPad::kPlayStation
                                                                : coreoflight::HintPad::kNintendo);
      } else if (arg.starts_with(L"--portable_update=")) {
        // A test of the portable launcher's update without a release (portable.h): this ZIP's
        // launcher in this one's place, then started, as the update dialog does after its download.
        portable_update = arg.substr(18);
      } else if (arg == L"--fullscreen") {
        // Fullscreen for this start, whatever the setting says (Steam's launch options on a
        // handheld or a TV setup such as Bazzite; a colleague's wish, 2026-10-23).
        fullscreen_arg = true;
      } else if (arg == L"--test_crash") {
        // A fault inside ntdll.dll on purpose (a string at address 0x10), to see the crash lines
        // LogFault writes: the module and offset, the thread, the calls back into the launcher.
        using InitString = void(WINAPI*)(void*, const wchar_t*);
        if (auto init = reinterpret_cast<InitString>(
                GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlInitUnicodeString"))) {
          alignas(16) unsigned char text[16] = {};
          init(text, reinterpret_cast<const wchar_t*>(0x10));
        }
      } else if (iso.empty()) {
        iso = arg;
      }
    }
    LocalFree(argv);
  }
  if (!portable_update.empty()) {
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::filesystem::path exe(self);
    std::string why = "not a portable launcher (no portable.txt next to it)";
    const bool done = coreoflight::IsPortable(exe.parent_path()) &&
                      coreoflight::ApplyPortableUpdate(portable_update, exe.parent_path(), exe.filename(), false, why) &&
                      coreoflight::StartLauncher(exe);
    coreoflight::HubLog(done ? "portable update test: done, the new launcher started" : "portable update test: " + why);
    return done ? 0 : 1;
  }
  const float dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY));
  HWND hwnd = CreateWindowW(wc.lpszClassName, L"Skylanders: Core of Light", WS_OVERLAPPEDWINDOW, 100, 100,
                            int(1280 * dpi), int(720 * dpi), nullptr, nullptr, instance, nullptr);
  // In fullscreen from the first frame (the setting, or --fullscreen), not a window that jumps.
  const bool start_fullscreen = fullscreen_arg || coreoflight::LoadHubSettings().fullscreen;
  if (start_fullscreen) SetFullscreen(hwnd, true);
  if (!CreateDevice(hwnd)) {
    ReleaseDevice();
    UnregisterClassW(wc.lpszClassName, instance);
    return 1;
  }
  ShowWindow(hwnd, SW_SHOWDEFAULT);
  UpdateWindow(hwnd);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // nothing to remember between runs
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(g_device, g_context);
  ImFont* regular = LoadFont(io, L"segoeui.ttf", L"NotoSans-Regular.ttf");
  ImFont* bold = LoadFont(io, L"segoeuib.ttf", L"NotoSans-Bold.ttf");
  if (!regular) regular = io.Fonts->AddFontDefault();
  if (!bold) bold = regular;

  coreoflight::SetTextureDevice(g_device);
  {
    const std::string wine = coreoflight::WineVersion();
    // The local time's offset from UTC: the log's times are local.
    TIME_ZONE_INFORMATION zone{};
    const DWORD zone_id = GetTimeZoneInformation(&zone);
    const long bias = zone.Bias + (zone_id == TIME_ZONE_ID_DAYLIGHT ? zone.DaylightBias : zone.StandardBias);
    char offset[32];
    std::snprintf(offset, sizeof(offset), "UTC%c%02ld:%02ld", bias <= 0 ? '+' : '-', std::labs(bias) / 60, std::labs(bias) % 60);
    coreoflight::HubLog("launcher: start, " COREOFLIGHT_VERSION ", Windows " + coreoflight::WindowsVersion() +
                        (wine.empty() ? std::string() : ", Wine " + wine) +
                        (start_fullscreen ? ", fullscreen" : ", window") + ", time zone " + offset + ", fonts " + g_fonts);
    // The graphics card and whether new setups get 1x for it (gpu_tier.h), read once and logged.
    coreoflight::DetectGpu();
    // Where the player's data is, by kind only: the log goes along with a problem report.
    std::error_code ec;
    coreoflight::HubLog(std::string("data folder: ") +
                        (!skylanders::PortableFolder().empty() ? "portable, next to the launcher"
                         : std::filesystem::equivalent(skylanders::DataFolder(), skylanders::DefaultDataFolder(), ec) ||
                                   skylanders::DataFolder() == skylanders::DefaultDataFolder()
                               ? "standard"
                               : "moved"));
  }
  coreoflight::LauncherUi ui;
  ui.Init(LauncherDir(), regular, bold);
  if (fullscreen_arg) ui.StartFullscreen();
  g_ui = &ui;
  if (!iso.empty()) ui.AddDisc(iso);
  DragAcceptFiles(hwnd, TRUE);

  bool running = true;
  HANDLE game = nullptr;
  bool game_shown = false;  // the game's window is up and the launcher hidden
  bool window_seen = false;  // the game's SDL window was there once (only then can it be gone)
  ULONGLONG took_back_until = 0;  // after a game: until then a pad press brings the launcher to the front
  bool input_logged = true;       // the first press after a game went into the log
  while (running) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      if (msg.message == WM_QUIT) running = false;
    }
    if (!running) break;
    if (game && !game_shown) {
      // The launch screen (it takes no input) until the game's window is up (or 30 s, or the game
      // ended at once).
      static ULONGLONG since = 0;
      if (!since) since = GetTickCount64();
      const bool visible = HasVisibleWindow(GetProcessId(game));
      if (visible || GetTickCount64() - since > 30000 || WaitForSingleObject(game, 0) == WAIT_OBJECT_0) {
        since = 0;
        game_shown = true;
        window_seen = HasGameWindow(GetProcessId(game));
        coreoflight::HubLog(std::string(visible ? "game: window up" : "game: no window after 30 s (or ended at once)") +
                            ", game window " + (window_seen ? "found" : "not found") + ", launcher hidden");
        ui.GameWindowShown();
        // Minimized and hidden: off the screen and the taskbar, and no frame drawn, so no pad press
        // reaches it while the game runs (one controller drives the game only).
        ShowWindow(hwnd, SW_MINIMIZE);
        ShowWindow(hwnd, SW_HIDE);
      }
    }
    if (game && game_shown) {
      // A game is running: the launcher waits hidden and comes back when it ends, or when the game's
      // window has been gone for 5 s while its process stays (under Wine a game can hang on its
      // way out: the launcher waited for it, took no input, and had to be started again). Gone means
      // no SDL window at all, shown or not, after one was there: a colleague's Bazzite got the
      // launcher back while the game still ran (2026-10-24), when only a visible window counted.
      DWORD code = 0;
      if (WaitForSingleObject(game, 250) == WAIT_TIMEOUT) {
        static ULONGLONG gone_since = 0;
        if (HasGameWindow(GetProcessId(game))) {
          if (!window_seen) coreoflight::HubLog("game: game window found");
          window_seen = true;
          gone_since = 0;
          ui.GameRunning();
          continue;
        }
        if (!window_seen) {  // its window never found: the game's end is its process's end
          ui.GameRunning();
          continue;
        }
        if (!gone_since) gone_since = GetTickCount64();
        if (GetTickCount64() - gone_since < 5000) {
          ui.GameRunning();
          continue;
        }
        gone_since = 0;
        coreoflight::HubLog("game: its window is gone, its process still runs after 5 s: back to the launcher");
      } else {
        GetExitCodeProcess(game, &code);
        char line[64];
        std::snprintf(line, sizeof(line), "game: ended, exit code 0x%08lX", code);
        coreoflight::HubLog(line);
      }
      CloseHandle(game);
      game = nullptr;
      game_shown = false;
      g_game_running = false;
      g_focus_log_until = GetTickCount64() + 30000;
      ui.GameEnded();
      ui.GameExitCode(code);
      ui.Refresh();
      ShowWindow(hwnd, SW_SHOW);
      if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
      TakeForeground(hwnd);
      coreoflight::HubLog(std::string("launcher: shown again, in front ") + (GetForegroundWindow() == hwnd ? "yes" : "no") +
                          ", focus " + (GetFocus() == hwnd ? "yes" : "no") + ", minimized " +
                          (IsIconic(hwnd) ? "yes" : "no"));
      input_logged = false;
      took_back_until = GetTickCount64() + 15000;
      // A button still held from the game (the press that left it) must not count as a press here
      // (A would start the game again, B ask to quit): wait until the pads' buttons and Enter,
      // Space and Esc are up (at most 3 s), and forget the keys' states from before the game (their
      // releases went to the game's window).
      for (int i = 0; i < 120 && coreoflight::InputHeld(); ++i) Sleep(25);
      ImGui::GetIO().ClearEventsQueue();
      ImGui::GetIO().ClearInputKeys();
    }
    // For a while after a game, a controller press brings the launcher back to the front should it
    // not have got there (someone else's window in front, the pad's presses would be dropped).
    // Only a controller: keys go to the window in front anyway, and Space / Enter typed in another
    // program took the focus away from it again and again (the owner, Core of Light #31).
    if (took_back_until && GetTickCount64() < took_back_until && GetForegroundWindow() != hwnd &&
        coreoflight::InputHeld(/*keys=*/false)) {
      TakeForeground(hwnd);
    }
    // The first press after a game, once: whether the launcher hears the pads again (the log).
    if (!input_logged && took_back_until && coreoflight::InputHeld()) {
      input_logged = true;
      coreoflight::HubLog(std::string("launcher: first input after the game, in front ") +
                          (GetForegroundWindow() == hwnd ? "yes" : "no"));
    }
    if (IsIconic(hwnd)) {
      Sleep(50);
      continue;
    }
    if (g_resize_width != 0 && g_resize_height != 0) {
      ReleaseTarget();
      g_swap_chain->ResizeBuffers(0, g_resize_width, g_resize_height, DXGI_FORMAT_UNKNOWN, 0);
      g_resize_width = g_resize_height = 0;
      CreateTarget();
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    coreoflight::UpdatePads(io);
    coreoflight::KeepClickedButton(io);  // a hint clicked last frame: its button counts without a pad
    SetFullscreen(hwnd, ui.fullscreen());
    ImGui::NewFrame();
    coreoflight::UpdateHintInput();
    if (!ui.Draw(io.DisplaySize.x, io.DisplaySize.y)) running = false;
    ImGui::Render();
    const float clear[4] = {0.01f, 0.03f, 0.09f, 1.0f};
    g_context->OMSetRenderTargets(1, &g_target, nullptr);
    g_context->ClearRenderTargetView(g_target, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_swap_chain->Present(1, 0);
    if (void* started = ui.TakeStartedProcess()) {
      game = static_cast<HANDLE>(started);  // hidden once its window is up (above)
      game_shown = false;
      g_game_running = true;
      coreoflight::HubLog("game: started, process " + std::to_string(GetProcessId(game)));
    }
  }

  coreoflight::ReleaseTextures();
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  ReleaseDevice();
  DestroyWindow(hwnd);
  UnregisterClassW(wc.lpszClassName, instance);
  return 0;
}
