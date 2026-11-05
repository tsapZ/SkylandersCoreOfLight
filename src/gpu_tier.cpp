#include "gpu_tier.h"

#include <windows.h>
#include <dxgi.h>

#include <algorithm>
#include <cctype>
#include <string_view>

#include "hub_log.h"

namespace coreoflight {

namespace {

std::string Narrow(const wchar_t* text) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) return {};
  std::string out(size_t(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
  return out;
}

bool Has(const std::string& upper, std::string_view part) { return upper.find(part) != std::string::npos; }

// A model number not followed by another digit: "RX 570" in "RX 570" and "RX 570 4GB", not in "RX 5700".
bool HasModel(const std::string& upper, std::string_view model) {
  for (size_t at = upper.find(model); at != std::string::npos; at = upper.find(model, at + 1)) {
    const size_t end = at + model.size();
    if (end == upper.size() || !std::isdigit(static_cast<unsigned char>(upper[end]))) return true;
  }
  return false;
}

}  // namespace

std::string WeakGpuReason(const GpuInfo& gpu) {
  std::string upper = gpu.name;
  std::transform(upper.begin(), upper.end(), upper.begin(),
                 [](unsigned char c) { return char(std::toupper(c)); });
  // Integrated graphics share the main memory and have little or none of their own (AMD's APUs
  // report 512 MB): the Ryzen and Core processors' graphics, Intel's UHD / Iris.
  if (gpu.vram_mb < 1024) return "integrated graphics";
  if (gpu.vendor == 0x8086) {
    // Intel: everything but Arc is integrated; the small Arc A310 / A380 are weak too.
    if (!Has(upper, "ARC")) return "Intel integrated graphics";
    if (Has(upper, "A310") || Has(upper, "A380")) return "an entry-level Arc card";
    return {};
  }
  if (gpu.vendor == 0x10DE) {
    // NVIDIA: the GTX cards (up to the 16 series), GT and MX, and the smallest RTX.
    if (Has(upper, "GTX") || Has(upper, " GT ") || Has(upper, " MX") ||
        (Has(upper, "QUADRO") && !Has(upper, "RTX"))) {
      return "a GTX-class card or older";
    }
    if (Has(upper, "RTX 3050") || Has(upper, "RTX 2050")) return "an entry-level RTX card";
  }
  if (gpu.vendor == 0x1002) {
    // AMD: the cards before the RX 5700, Vega, and the small RX 6000s.
    for (std::string_view old : {"RX 4", "RX 5300", "RX 5500", "RX 5600", "RX 6300", "RX 6400", "RX 6500", "VEGA",
                                 "R9 ", "R7 ", "R5 ", "HD "}) {
      if (Has(upper, old)) return "an older or entry-level Radeon";
    }
    for (std::string_view old : {"RX 550", "RX 560", "RX 570", "RX 580", "RX 590"}) {
      if (HasModel(upper, old)) return "an older or entry-level Radeon";
    }
  }
  // Too little memory for the games' targets at 2x either way.
  if (gpu.vram_mb < 6 * 1024) return "less than 6 GB of graphics memory";
  return {};
}

namespace {

GpuInfo Detect() {
  GpuInfo gpu;
  IDXGIFactory1* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return gpu;
  IDXGIAdapter1* adapter = nullptr;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 d{};
    const bool ok = SUCCEEDED(adapter->GetDesc1(&d));
    adapter->Release();
    if (!ok || (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
    gpu.name = Narrow(d.Description);
    gpu.vendor = d.VendorId;
    gpu.vram_mb = uint64_t(d.DedicatedVideoMemory) >> 20;
    break;
  }
  factory->Release();
  if (gpu.name.empty()) return gpu;  // none found (Wine without a driver): leave the scale as it was
  gpu.reason = WeakGpuReason(gpu);
  gpu.weak = !gpu.reason.empty();
  HubLog("Graphics card: " + gpu.name + " (" + std::to_string(gpu.vram_mb) + " MB)" +
         (gpu.weak ? ", weak at 2x (" + gpu.reason + "): new setups get 1x" : ""));
  return gpu;
}

}  // namespace

const GpuInfo& DetectGpu() {
  static const GpuInfo gpu = Detect();
  return gpu;
}

}  // namespace coreoflight
