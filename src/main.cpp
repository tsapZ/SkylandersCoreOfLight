// Skylanders: Core of Light: the launcher window (Win32 + Direct3D 11 + Dear ImGui, as Dear
// ImGui's own Win32 / DirectX 11 example sets them up). The screen itself is launcher_ui.cpp.

#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "launcher_ui.h"
#include "pad_state.h"
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

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
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

ImFont* LoadFont(ImGuiIO& io, const wchar_t* file) {
  wchar_t windows[MAX_PATH];
  GetWindowsDirectoryW(windows, MAX_PATH);
  const std::filesystem::path path = std::filesystem::path(windows) / "Fonts" / file;
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return nullptr;
  return io.Fonts->AddFontFromFileTTF(path.string().c_str(), 32.0f);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  ImGui_ImplWin32_EnableDpiAwareness();
  WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr,
                    L"CoreOfLight", nullptr};
  // The launcher's own icon (coreoflight.rc), in the window's title and the taskbar.
  wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
  wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                             GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
  RegisterClassExW(&wc);
  const float dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY));
  HWND hwnd = CreateWindowW(wc.lpszClassName, L"Skylanders: Core of Light", WS_OVERLAPPEDWINDOW, 100, 100,
                            int(1280 * dpi), int(720 * dpi), nullptr, nullptr, instance, nullptr);
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
  ImFont* regular = LoadFont(io, L"segoeui.ttf");
  ImFont* bold = LoadFont(io, L"segoeuib.ttf");
  if (!regular) regular = io.Fonts->AddFontDefault();
  if (!bold) bold = regular;

  coreoflight::SetTextureDevice(g_device);
  // --documents=<folder>: that folder in place of Documents (figures, pictures, profiles, the
  // games' user data), to try the launcher on a copy. An ISO dragged onto coreoflight.exe (or
  // named on its command line) is set up at once.
  std::wstring iso;
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
        // Screenshots to show: none of the games' pictures (textures.h).
        coreoflight::SetNoGameArt(true);
      } else if (iso.empty()) {
        iso = arg;
      }
    }
    LocalFree(argv);
  }
  coreoflight::LauncherUi ui;
  ui.Init(LauncherDir(), regular, bold);
  g_ui = &ui;
  if (!iso.empty()) ui.AddDisc(iso);
  DragAcceptFiles(hwnd, TRUE);

  bool running = true;
  HANDLE game = nullptr;
  bool game_shown = false;  // the game's window is up and the launcher hidden
  while (running) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      if (msg.message == WM_QUIT) running = false;
    }
    if (!running) break;
    if (game && !game_shown) {
      // The launch screen until the game's window is up (or 30 s, or the game ended at once).
      static ULONGLONG since = 0;
      if (!since) since = GetTickCount64();
      if (HasVisibleWindow(GetProcessId(game)) || GetTickCount64() - since > 30000 ||
          WaitForSingleObject(game, 0) == WAIT_OBJECT_0) {
        since = 0;
        game_shown = true;
        ui.GameWindowShown();
        ShowWindow(hwnd, SW_HIDE);
      }
    }
    if (game && game_shown) {
      // A game is running: the launcher waits hidden and comes back when it ends.
      if (WaitForSingleObject(game, 250) == WAIT_TIMEOUT) continue;
      DWORD code = 0;
      GetExitCodeProcess(game, &code);
      CloseHandle(game);
      game = nullptr;
      game_shown = false;
      ui.GameEnded();
      ui.GameExitCode(code);
      ui.Refresh();
      ShowWindow(hwnd, SW_SHOW);
      SetForegroundWindow(hwnd);
      // A button still held from the game (the press that left it) must not count as a press here
      // (A would start the game again, B ask to quit): wait until the pads' buttons and Enter,
      // Space and Esc are up (at most 3 s), and forget the keys' states from before the game (their
      // releases went to the game's window).
      for (int i = 0; i < 120 && coreoflight::InputHeld(); ++i) Sleep(25);
      ImGui::GetIO().ClearInputKeys();
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
    SetFullscreen(hwnd, ui.fullscreen());
    ImGui::NewFrame();
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
