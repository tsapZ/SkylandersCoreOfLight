// Skylanders: Core of Light: the launcher window (Win32 + D3D11 on Windows, SDL3 + OpenGL 3 on
// Linux, with Dear ImGui). The screen itself is launcher_ui.cpp.

#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>
#else
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <ctime>
#endif

#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <cwchar>
#endif
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#ifdef _WIN32
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#else
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#endif
#include "gpu_tier.h"
#include "hints.h"
#include "hub_log.h"
#include "hub_settings.h"
#include "language_import.h"
#include "wiiu_figures.h"
#include "launcher_ui.h"
#include "pad_state.h"
#include "platform.h"
#include "portable.h"
#include "portal/data_folder.h"
#include "textures.h"

#ifdef _WIN32
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

namespace {

bool g_own_font = false;  // --own_font: the launcher's own font even where Segoe UI is (to see it)
std::string g_fonts;      // the fonts LoadFont took, for the start line
#ifdef _WIN32
bool g_game_running = false;
#endif
coreoflight::LauncherUi* g_ui = nullptr;  // for a file dropped on the window

std::filesystem::path LauncherDir() { return coreoflight::SelfExePath().parent_path(); }

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

struct LaunchOptions {
  std::filesystem::path iso;
  std::filesystem::path portable_update;
  std::string sc_language;
  std::string im_language;
  std::string wiiu_figures;
  bool fullscreen_arg = false;
  bool test_crash = false;
};

void ParseArgs(const std::vector<std::string>& args, LaunchOptions& opts) {
  for (size_t i = 1; i < args.size(); ++i) {
    const std::string& arg = args[i];
    if (arg.starts_with("--documents=")) {
      coreoflight::SetDocumentsDir(coreoflight::PathFromUtf8(arg.substr(12)));
    } else if (arg.starts_with("--install_dir=")) {
      // A library folder for games installed from the launcher (games.h StartGameInstall).
      coreoflight::SetInstallDir(coreoflight::PathFromUtf8(arg.substr(14)));
    } else if (arg == "--no_game_art") {
      // Screenshots to show: none of the games' pictures (textures.h), and the hints stay the
      // controller's while the screenshot script presses keys (hints.h).
      coreoflight::SetNoGameArt(true);
      coreoflight::KeepControllerHints();
    } else if (arg == "--start_backdrop=game") {
      coreoflight::SetStartBackdropFromGame(true);  // launcher_ui.h (#78: to compare the two)
    } else if (arg == "--own_font") {
      g_own_font = true;  // the fallback font (Noto Sans) as under Wine
    } else if (arg == "--offline") {
      // No releases, no check, nothing downloaded or installed from GitHub (scripted tests).
      coreoflight::SetOffline(true);
    } else if (arg == "--hint_pad=playstation" || arg == "--hint_pad=nintendo") {
      // The hints as that controller's from the start, whatever pad is used (#29; to see them
      // without such a pad).
      coreoflight::ForceHintPad(arg.ends_with("playstation") ? coreoflight::HintPad::kPlayStation
                                                           : coreoflight::HintPad::kNintendo);
    } else if (arg.starts_with("--portable_update=")) {
      // A test of the portable launcher's update without a release (portable.h): this archive's
      // launcher in this one's place, then started, as the update dialog does after its download.
      opts.portable_update = coreoflight::PathFromUtf8(arg.substr(18));
    } else if (arg.starts_with("--sc_language=")) {
      // A test of SuperChargers' language import without the window (language_import.h).
      opts.sc_language = arg.substr(14);
    } else if (arg.starts_with("--im_language=")) {
      // The same for Imaginators' import from its PS3 disc.
      opts.im_language = arg.substr(14);
    } else if (arg.starts_with("--wiiu_figures=")) {
      // A test of the Wii U figures (wiiu_figures.h) without the window: "<sc|im>|<Wii U folder>|<game folder>" or
      // "<sc|im>|restore|<game folder>".
      opts.wiiu_figures = arg.substr(15);
    } else if (arg == "--fullscreen") {
      // Fullscreen for this start, whatever the setting says (Steam's launch options on a
      // handheld or a TV setup such as Bazzite; a colleague's wish, 2026-10-23).
      opts.fullscreen_arg = true;
    } else if (arg.starts_with("--ds3_package_zip=")) {
      // The 3DS screen's game package from a local ZIP (ds3_games.h, read there through LaunchArgs).
    } else if (arg == "--test_crash") {
      opts.test_crash = true;
    } else if (opts.iso.empty()) {
      opts.iso = coreoflight::PathFromUtf8(arg);
    }
  }
}

int RunHeadlessTools(const LaunchOptions& opts) {
  if (!opts.portable_update.empty()) {
    const std::filesystem::path exe = coreoflight::SelfExePath();
#ifdef _WIN32
    std::string why = "not a portable launcher (no portable.txt next to it)";
    const bool allowed = coreoflight::IsPortable(exe.parent_path());
#else
    std::string why;
    const bool allowed = true;
#endif
    const bool done =
        allowed &&
        coreoflight::ApplyPortableUpdate(opts.portable_update, exe.parent_path(), exe.filename(), false, why) &&
        coreoflight::StartLauncher(exe);
    coreoflight::HubLog(done ? "portable update test: done, the new launcher started"
                             : "portable update test: " + why);
    return done ? 0 : 1;
  }
  if (!opts.sc_language.empty() || !opts.im_language.empty()) {
    // "<code>|<source disc>|<rom folder>", or "restore|<rom folder>": the import (or its undoing) on
    // its thread, waited for; its summary goes to the log.
    const bool im = opts.sc_language.empty();
    const std::string& spec = im ? opts.im_language : opts.sc_language;
    const std::string tag = im ? "im language test: " : "sc language test: ";
    std::vector<std::filesystem::path> parts;
    for (size_t at = 0;;) {
      const size_t bar = spec.find('|', at);
      parts.push_back(coreoflight::PathFromUtf8(
          std::string_view(spec).substr(at, bar == std::string::npos ? std::string::npos : bar - at)));
      if (bar == std::string::npos) break;
      at = bar + 1;
    }
    const std::string code = coreoflight::PathUtf8(parts[0]);
    coreoflight::LanguageImport job;
    if (parts.size() == 2 && code == "restore") {
      job.StartRestore(parts[1], false, im ? "Imaginators" : "SuperChargers");
    } else if (parts.size() == 3 && im) {
      job.Start(parts[1], parts[2], code, false);
    } else if (parts.size() == 3) {
      job.StartSuperChargers(parts[1], parts[2], code, false);
    } else {
      coreoflight::HubLog(tag + (im ? "--im_language" : "--sc_language") +
                          "=\"<code>|<source disc>|<rom folder>\" or \"restore|<rom folder>\"");
      return 1;
    }
    while (job.running()) {
#ifdef _WIN32
      Sleep(200);
#else
      SDL_Delay(200);
#endif
    }
    const std::string summary = job.TakeSummary();
    coreoflight::HubLog(tag + summary);
    return summary.empty() ? 1 : 0;
  }
  if (!opts.wiiu_figures.empty()) {
    const std::string& spec = opts.wiiu_figures;
    const size_t a = spec.find('|'), b = a == std::string::npos ? a : spec.find('|', a + 1);
    const std::string game = spec.substr(0, a);
    if (b == std::string::npos || (game != "sc" && game != "im")) {
      coreoflight::HubLog("wiiu figures test: --wiiu_figures=\"<sc|im>|<Wii U folder>|<game folder>\" or "
                          "\"<sc|im>|restore|<game folder>\"");
      return 1;
    }
    const std::string source = spec.substr(a + 1, b - a - 1);
    const std::filesystem::path game_dir = coreoflight::PathFromUtf8(spec.substr(b + 1));
    const auto which = game == "sc" ? coreoflight::WiiuFigures::Game::kSuperChargers
                                    : coreoflight::WiiuFigures::Game::kImaginators;
    coreoflight::WiiuFigures job;
    if (source == "restore") {
      job.StartRestore(which, game_dir / "rom", game_dir, false);
    } else {
      job.Start(which, coreoflight::PathFromUtf8(source), game_dir / "rom", game_dir, false);
    }
    while (job.running()) {
#ifdef _WIN32
      Sleep(200);
#else
      SDL_Delay(200);
#endif
    }
    const std::string summary = job.TakeSummary();
    coreoflight::HubLog("wiiu figures test: " + summary);
    return summary.empty() ? 1 : 0;
  }
  return -1;
}

void LogDataFolderKind() {
  std::error_code ec;
  coreoflight::HubLog(std::string("data folder: ") +
                      (!skylanders::PortableFolder().empty() ? "portable, next to the launcher"
                       : std::filesystem::equivalent(skylanders::DataFolder(), skylanders::DefaultDataFolder(), ec) ||
                                 skylanders::DataFolder() == skylanders::DefaultDataFolder()
                             ? "standard"
                             : "moved"));
}

}  // namespace

#ifdef _WIN32

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swap_chain = nullptr;
ID3D11RenderTargetView* g_target = nullptr;
UINT g_resize_width = 0, g_resize_height = 0;

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
      // A disc dropped on the window (an ISO or an unpacked folder): set up like one picked with X.
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

// Windows' own font (Segoe UI), else the one in the launcher's fonts folder (Noto Sans, SIL OFL):
// under Wine / Proton there is no Segoe UI, and Dear ImGui's built-in pixel font made the cards'
// lines run into each other (a colleague's Bazzite, 2026-10-24).
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
  LaunchOptions opts;
  int argc = 0;
  if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
    std::vector<std::string> utf8_args;
    std::vector<char*> argv_ptrs;
    utf8_args.reserve(size_t(argc));
    for (int i = 0; i < argc; ++i) utf8_args.push_back(coreoflight::PathUtf8(std::filesystem::path(argv[i])));
    for (std::string& s : utf8_args) argv_ptrs.push_back(s.data());
    LocalFree(argv);
    coreoflight::SetLaunchArgs(argc, argv_ptrs.data());
    ParseArgs(utf8_args, opts);
  }
  if (opts.test_crash) {
    // A fault inside ntdll.dll on purpose (a string at address 0x10), to see the crash lines
    // LogFault writes: the module and offset, the thread, the calls back into the launcher.
    using InitString = void(WINAPI*)(void*, const wchar_t*);
    if (auto init = reinterpret_cast<InitString>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlInitUnicodeString"))) {
      alignas(16) unsigned char text[16] = {};
      init(text, reinterpret_cast<const wchar_t*>(0x10));
    }
  }
  if (const int code = RunHeadlessTools(opts); code >= 0) return code;
  const float dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY));
  HWND hwnd = CreateWindowW(wc.lpszClassName, L"Skylanders: Core of Light", WS_OVERLAPPEDWINDOW, 100, 100,
                            int(1280 * dpi), int(720 * dpi), nullptr, nullptr, instance, nullptr);
  // In fullscreen from the first frame (the setting, or --fullscreen), not a window that jumps.
  const bool start_fullscreen = opts.fullscreen_arg || coreoflight::LoadHubSettings().fullscreen;
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
    LogDataFolderKind();
  }
  coreoflight::LauncherUi ui;
  ui.Init(LauncherDir(), regular, bold);
  if (opts.fullscreen_arg) ui.StartFullscreen();
  g_ui = &ui;
  if (!opts.iso.empty()) ui.AddDisc(opts.iso);
  DragAcceptFiles(hwnd, TRUE);

  bool running = true;
  HANDLE game = nullptr;
  bool game_shown = false;  // the game's window is up and the launcher hidden
  bool window_seen = false;  // the game's SDL window was there once (only then can it be gone)
  ULONGLONG took_back_until = 0;  // after a game: until then a pad press brings the launcher to the front
  bool input_logged = true;       // the first press after a game went into the log
  double last_mouse_use = 0.0;    // ImGui's clock
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
    // The cursor goes away while the mouse isn't used, as in the games (#53).
    if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.MouseWheel != 0.0f ||
        ImGui::IsAnyMouseDown()) {
      last_mouse_use = ImGui::GetTime();
    }
    if (ImGui::GetTime() - last_mouse_use > 3.0 && GetForegroundWindow() == hwnd) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }
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

#else  // !_WIN32

namespace {

char g_crash_log_path[1024] = {};
pthread_t g_main_pthread{};

void AppendCrashBytes(int fd, const char* data, size_t len) {
  while (len > 0) {
    const ssize_t n = ::write(fd, data, len);
    if (n <= 0) break;
    data += n;
    len -= size_t(n);
  }
}

void AppendCrashStr(int fd, const char* s) {
  if (s) AppendCrashBytes(fd, s, std::strlen(s));
}

void AppendCrashHex(int fd, uintptr_t v) {
  char buf[2 + sizeof(uintptr_t) * 2 + 1];
  buf[0] = '0';
  buf[1] = 'x';
  int digits = 0;
  uintptr_t t = v;
  do {
    ++digits;
    t >>= 4;
  } while (t != 0);
  for (int i = digits - 1; i >= 0; --i) {
    const unsigned nib = unsigned(v & 0xFu);
    buf[2 + i] = char(nib < 10 ? ('0' + nib) : ('A' + nib - 10));
    v >>= 4;
  }
  buf[2 + digits] = '\0';
  AppendCrashBytes(fd, buf, size_t(2 + digits));
}

void AppendCrashDec(int fd, unsigned long v) {
  char buf[32];
  int len = 0;
  do {
    buf[len++] = char('0' + (v % 10));
    v /= 10;
  } while (v != 0 && len < 31);
  for (int i = 0; i < len / 2; ++i) std::swap(buf[i], buf[len - 1 - i]);
  AppendCrashBytes(fd, buf, size_t(len));
}

void AppendCrashAddress(int fd, void* addr) {
  Dl_info info{};
  if (dladdr(addr, &info) && info.dli_fname && info.dli_fbase) {
    const char* slash = std::strrchr(info.dli_fname, '/');
    AppendCrashStr(fd, slash ? slash + 1 : info.dli_fname);
    AppendCrashStr(fd, "+");
    AppendCrashHex(fd, uintptr_t(addr) - uintptr_t(info.dli_fbase));
  } else {
    AppendCrashHex(fd, uintptr_t(addr));
  }
}

void SignalCrashHandler(int sig, siginfo_t* si, void*) {
  const int fd = g_crash_log_path[0] ? ::open(g_crash_log_path, O_WRONLY | O_CREAT | O_APPEND, 0644) : STDERR_FILENO;
  if (fd >= 0) {
    AppendCrashStr(fd, "ERROR: launcher: crashed, signal ");
    AppendCrashDec(fd, static_cast<unsigned long>(sig));
    if (si && si->si_addr) {
      AppendCrashStr(fd, " at ");
      AppendCrashAddress(fd, si->si_addr);
    }
    AppendCrashStr(fd, pthread_equal(pthread_self(), g_main_pthread) ? ", main thread\n" : ", another thread\n");
    void* frames[12]{};
    const int nframes = ::backtrace(frames, 12);
    if (nframes > 0) {
      AppendCrashStr(fd, "ERROR: launcher: called from ");
      for (int i = 0; i < nframes; ++i) {
        if (i) AppendCrashStr(fd, ", ");
        AppendCrashAddress(fd, frames[i]);
      }
      AppendCrashStr(fd, "\n");
    }
    if (fd != STDERR_FILENO) ::close(fd);
  }
  std::raise(sig);
}

void InstallCrashHandler() {
  g_main_pthread = pthread_self();
  const std::filesystem::path log_file = coreoflight::HubLogFile();
  std::error_code ec;
  if (!log_file.empty()) std::filesystem::create_directories(log_file.parent_path(), ec);
  const std::string path = coreoflight::PathUtf8(log_file);
  if (path.size() < sizeof(g_crash_log_path)) std::memcpy(g_crash_log_path, path.c_str(), path.size() + 1);
  struct sigaction sa{};
  sa.sa_sigaction = SignalCrashHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
  for (const int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) sigaction(sig, &sa, nullptr);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void TriggerTestCrash() {
  volatile uintptr_t* bad = reinterpret_cast<volatile uintptr_t*>(0x10);
  *bad = 1;
}

ImFont* LoadLinuxFont(ImGuiIO& io, const char* own_file, bool bold) {
  std::vector<std::filesystem::path> candidates = {LauncherDir() / "fonts" / own_file};
  if (!g_own_font) {
    if (bold) {
      candidates.insert(candidates.end(),
                        {"/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf", "/usr/share/fonts/noto/NotoSans-Bold.ttf",
                         "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
                         "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                         "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
                         "/usr/share/fonts/liberation-sans/LiberationSans-Bold.ttf"});
    } else {
      candidates.insert(
          candidates.end(),
          {"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf",
           "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
           "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
           "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf"});
    }
  }
  for (const std::filesystem::path& path : candidates) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) continue;
    const std::string utf8 = coreoflight::PathUtf8(path);
    if (ImFont* font = io.Fonts->AddFontFromFileTTF(utf8.c_str(), 32.0f)) {
      g_fonts += (g_fonts.empty() ? "" : " ") + coreoflight::PathUtf8(path.filename());
      return font;
    }
  }
  g_fonts += g_fonts.empty() ? "none (Dear ImGui's own)" : " none";
  return nullptr;
}

}  // namespace

int main(int argc, char* argv[]) {
  std::set_terminate(LogTerminate);
  coreoflight::SetLaunchArgs(argc, argv);

  std::vector<std::string> args;
  args.reserve(size_t(argc));
  for (int i = 0; i < argc; ++i) args.emplace_back(argv[i] ? argv[i] : "");
  LaunchOptions opts;
  ParseArgs(args, opts);

  InstallCrashHandler();
  if (opts.test_crash) TriggerTestCrash();

  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    coreoflight::HubError(std::string("launcher: SDL_Init failed: ") + SDL_GetError());
    return 1;
  }

  if (const int code = RunHeadlessTools(opts); code >= 0) {
    SDL_Quit();
    return code;
  }

  // OpenGL 3.2 Core (#version 150) first, falling back to 3.0 (#version 130).
  auto create_gl = [](SDL_Window** out_win, SDL_GLContext* out_ctx, const char*& out_glsl, bool core32,
                      bool fullscreen) -> bool {
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    if (core32) {
      out_glsl = "#version 150";
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    } else {
      out_glsl = "#version 130";
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    }
    const float scale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    SDL_Window* win = SDL_CreateWindow("Skylanders: Core of Light", int(1280 * scale), int(720 * scale), flags);
    if (!win) return false;
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
      SDL_DestroyWindow(win);
      return false;
    }
    *out_win = win;
    *out_ctx = ctx;
    return true;
  };

  const bool start_fullscreen = opts.fullscreen_arg || coreoflight::LoadHubSettings().fullscreen;
  SDL_Window* window = nullptr;
  SDL_GLContext gl_context = nullptr;
  const char* glsl_version = "#version 150";
  if (!create_gl(&window, &gl_context, glsl_version, true, start_fullscreen) &&
      !create_gl(&window, &gl_context, glsl_version, false, start_fullscreen)) {
    coreoflight::HubError(std::string("launcher: OpenGL window setup failed: ") + SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_GL_MakeCurrent(window, gl_context);
  SDL_GL_SetSwapInterval(1);
  SDL_ShowWindow(window);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
  ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
  ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
  ImGui_ImplOpenGL3_Init(glsl_version);

  ImFont* regular = LoadLinuxFont(io, "NotoSans-Regular.ttf", false);
  ImFont* bold = LoadLinuxFont(io, "NotoSans-Bold.ttf", true);
  if (!regular) regular = io.Fonts->AddFontDefault();
  if (!bold) bold = regular;

  coreoflight::SetTextureDevice(gl_context);
  {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    const long off_s = tm.tm_gmtoff;
    const long abs_s = std::labs(off_s);
    char offset[32];
    std::snprintf(offset, sizeof(offset), "UTC%c%02ld:%02ld", off_s >= 0 ? '+' : '-', abs_s / 3600,
                  (abs_s % 3600) / 60);
    coreoflight::HubLog("launcher: start, " COREOFLIGHT_VERSION ", " + coreoflight::OsVersionSummary() +
                        (start_fullscreen ? ", fullscreen" : ", window") + ", time zone " + offset + ", fonts " +
                        g_fonts);
    coreoflight::DetectGpu();
    LogDataFolderKind();
  }

  // Destroy before ImGui/OpenGL/SDL teardown: ~MusicPlayer destroys its SDL_AudioStream, which
  // SDL_Quit would otherwise free first.
  auto ui = std::make_unique<coreoflight::LauncherUi>();
  ui->Init(LauncherDir(), regular, bold);
  if (opts.fullscreen_arg) ui->StartFullscreen();
  g_ui = ui.get();
  if (!opts.iso.empty()) ui->AddDisc(opts.iso);

  bool running = true;
  bool is_fullscreen = start_fullscreen;
  void* game = nullptr;
  bool game_shown = false;
  uint64_t game_wait_since = 0;
  double last_mouse_use = 0.0;

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) running = false;
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) {
        running = false;
      }
      if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (event.key.scancode == SDL_SCANCODE_F11 ||
            ((event.key.mod & SDL_KMOD_ALT) && event.key.scancode == SDL_SCANCODE_RETURN)) {
          ui->ToggleFullscreen();
        }
      }
      if (event.type == SDL_EVENT_DROP_FILE && event.drop.data && g_ui) {
        g_ui->AddDisc(coreoflight::PathFromUtf8(event.drop.data));
      }
    }
    if (!running) break;

    if (game && !game_shown) {
      if (!game_wait_since) game_wait_since = coreoflight::MonotonicMs();
      if (!coreoflight::IsProcessRunning(game, 0) || coreoflight::MonotonicMs() - game_wait_since >= 1500) {
        game_wait_since = 0;
        game_shown = true;
        coreoflight::HubLog("game: launcher hidden while game runs");
        ui->GameWindowShown();
        SDL_MinimizeWindow(window);
        SDL_HideWindow(window);
      }
    }
    if (game && game_shown) {
      int code = 0;
      if (coreoflight::IsProcessRunning(game, 100, &code)) {
        ui->GameRunning();
        continue;
      }
      char line[64];
      std::snprintf(line, sizeof(line), "game: ended, exit code 0x%08X", unsigned(code));
      coreoflight::HubLog(line);
      coreoflight::CloseProcessHandle(game);
      game_shown = false;
      ui->GameEnded();
      ui->GameExitCode(static_cast<unsigned long>(code));
      ui->Refresh();
      SDL_ShowWindow(window);
      SDL_RestoreWindow(window);
      SDL_RaiseWindow(window);
      for (int i = 0; i < 120 && coreoflight::InputHeld(); ++i) {
        SDL_PumpEvents();
        SDL_Delay(25);
      }
      ImGui::GetIO().ClearEventsQueue();
      ImGui::GetIO().ClearInputKeys();
    }

    if (SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) {
      SDL_Delay(50);
      continue;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    coreoflight::UpdatePads(io);
    coreoflight::KeepClickedButton(io);
    if (ui->fullscreen() != is_fullscreen) {
      is_fullscreen = ui->fullscreen();
      SDL_SetWindowFullscreen(window, is_fullscreen);
    }
    ImGui::NewFrame();
    coreoflight::UpdateHintInput();
    if (!ui->Draw(io.DisplaySize.x, io.DisplaySize.y)) running = false;

    if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.MouseWheel != 0.0f || ImGui::IsAnyMouseDown()) {
      last_mouse_use = ImGui::GetTime();
    }
    if (ImGui::GetTime() - last_mouse_use > 3.0 && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

    ImGui::Render();
    int fb_w = 0, fb_h = 0;
    SDL_GetWindowSizeInPixels(window, &fb_w, &fb_h);
    glViewport(0, 0, fb_w, fb_h);
    glClearColor(0.01f, 0.03f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);

    if (void* started = ui->TakeStartedProcess()) {
      game = started;
      game_shown = false;
      game_wait_since = 0;
      coreoflight::HubLog("game: started, process " + std::to_string(coreoflight::ProcessHandleId(game)));
    }
  }

  g_ui = nullptr;
  ui.reset();
  if (game) coreoflight::CloseProcessHandle(game);
  coreoflight::ReleaseTextures();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  coreoflight::ShutdownSdlPads();
  SDL_GL_DestroyContext(gl_context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

#endif  // _WIN32
