#include "textures.h"

#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace coreoflight {
namespace {

ID3D11Device* g_device = nullptr;
IWICImagingFactory* g_wic = nullptr;

struct Entry {
  ID3D11ShaderResourceView* view = nullptr;
  std::chrono::steady_clock::time_point tried;
};
std::unordered_map<std::wstring, Entry> g_textures;

ID3D11ShaderResourceView* Decode(const std::filesystem::path& path) {
  if (!g_device) return nullptr;
  if (!g_wic && FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic)))) {
    return nullptr;
  }
  IWICBitmapDecoder* decoder = nullptr;
  IWICBitmapFrameDecode* frame = nullptr;
  IWICFormatConverter* converter = nullptr;
  ID3D11ShaderResourceView* view = nullptr;
  UINT w = 0, h = 0;
  if (SUCCEEDED(g_wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                 &decoder)) &&
      SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(g_wic->CreateFormatConverter(&converter)) &&
      SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                      WICBitmapPaletteTypeCustom)) &&
      SUCCEEDED(converter->GetSize(&w, &h)) && w && h && w <= 4096 && h <= 4096) {
    std::vector<uint8_t> rgba(size_t(w) * h * 4);
    if (SUCCEEDED(converter->CopyPixels(nullptr, w * 4, UINT(rgba.size()), rgba.data()))) {
      D3D11_TEXTURE2D_DESC desc = {};
      desc.Width = w;
      desc.Height = h;
      desc.MipLevels = 1;
      desc.ArraySize = 1;
      desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      desc.SampleDesc.Count = 1;
      desc.Usage = D3D11_USAGE_IMMUTABLE;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      const D3D11_SUBRESOURCE_DATA data = {rgba.data(), w * 4, 0};
      ID3D11Texture2D* texture = nullptr;
      if (SUCCEEDED(g_device->CreateTexture2D(&desc, &data, &texture))) {
        g_device->CreateShaderResourceView(texture, nullptr, &view);
        texture->Release();
      }
    }
  }
  if (converter) converter->Release();
  if (frame) frame->Release();
  if (decoder) decoder->Release();
  return view;
}

}  // namespace

void SetTextureDevice(ID3D11Device* device) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  g_device = device;
}

ImTextureID LoadTexture(const std::filesystem::path& path) {
  const auto now = std::chrono::steady_clock::now();
  auto [it, added] = g_textures.try_emplace(path.wstring());
  Entry& e = it->second;
  if (!e.view && (added || now - e.tried > std::chrono::seconds(10))) {
    e.tried = now;
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) e.view = Decode(path);
  }
  return e.view ? reinterpret_cast<ImTextureID>(e.view) : ImTextureID{};
}

void ReleaseTextures() {
  for (auto& [path, e] : g_textures) {
    if (e.view) e.view->Release();
  }
  g_textures.clear();
  if (g_wic) g_wic->Release();
  g_wic = nullptr;
}

}  // namespace coreoflight
