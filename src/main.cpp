// Skylanders: Core of Light: the launcher window (Win32 + Direct3D 11 + Dear ImGui, as Dear
// ImGui's own Win32 / DirectX 11 example sets them up). The screen itself is launcher_ui.cpp.

#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>

#include <filesystem>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "launcher_ui.h"
#include "textures.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swap_chain = nullptr;
ID3D11RenderTargetView* g_target = nullptr;
UINT g_resize_width = 0, g_resize_height = 0;
coreoflight::LauncherUi* g_ui = nullptr;  // for a file dropped on the window

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
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
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
  coreoflight::LauncherUi ui;
  ui.Init(LauncherDir(), regular, bold);
  g_ui = &ui;
  // An ISO dragged onto coreoflight.exe (or named on its command line) is set up at once.
  int argc = 0;
  if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
    if (argc > 1) ui.AddDisc(argv[1]);
    LocalFree(argv);
  }
  DragAcceptFiles(hwnd, TRUE);

  bool running = true;
  HANDLE game = nullptr;
  while (running) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      if (msg.message == WM_QUIT) running = false;
    }
    if (!running) break;
    if (game) {
      // A game is running: the launcher waits hidden and comes back when it ends.
      if (WaitForSingleObject(game, 250) == WAIT_TIMEOUT) continue;
      CloseHandle(game);
      game = nullptr;
      ui.Refresh();
      ShowWindow(hwnd, SW_SHOW);
      SetForegroundWindow(hwnd);
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
    ImGui::NewFrame();
    if (!ui.Draw(io.DisplaySize.x, io.DisplaySize.y)) running = false;
    ImGui::Render();
    const float clear[4] = {0.01f, 0.03f, 0.09f, 1.0f};
    g_context->OMSetRenderTargets(1, &g_target, nullptr);
    g_context->ClearRenderTargetView(g_target, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_swap_chain->Present(1, 0);
    if (void* started = ui.TakeStartedProcess()) {
      game = static_cast<HANDLE>(started);
      ShowWindow(hwnd, SW_HIDE);
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
