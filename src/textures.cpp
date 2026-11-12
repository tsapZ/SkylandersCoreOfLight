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

std::unordered_map<int, Entry> g_ui_textures;

bool Wic() {
  return g_wic || SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic)));
}

// The first frame of `decoder` as RGBA pixels (at most 4096 a side); false if it can't be read.
bool PixelsFrom(IWICBitmapDecoder* decoder, UINT& w, UINT& h, std::vector<uint8_t>& rgba) {
  IWICBitmapFrameDecode* frame = nullptr;
  IWICFormatConverter* converter = nullptr;
  bool ok = false;
  w = h = 0;
  if (SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(g_wic->CreateFormatConverter(&converter)) &&
      SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                      WICBitmapPaletteTypeCustom)) &&
      SUCCEEDED(converter->GetSize(&w, &h)) && w && h && w <= 4096 && h <= 4096) {
    rgba.resize(size_t(w) * h * 4);
    ok = SUCCEEDED(converter->CopyPixels(nullptr, w * 4, UINT(rgba.size()), rgba.data()));
  }
  if (converter) converter->Release();
  if (frame) frame->Release();
  return ok;
}

ID3D11ShaderResourceView* TextureOf(const std::vector<uint8_t>& rgba, UINT w, UINT h) {
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
  ID3D11ShaderResourceView* view = nullptr;
  if (SUCCEEDED(g_device->CreateTexture2D(&desc, &data, &texture))) {
    g_device->CreateShaderResourceView(texture, nullptr, &view);
    texture->Release();
  }
  return view;
}

// The first frame of `decoder` as a texture, or nullptr.
ID3D11ShaderResourceView* TextureFrom(IWICBitmapDecoder* decoder) {
  UINT w = 0, h = 0;
  std::vector<uint8_t> rgba;
  return PixelsFrom(decoder, w, h, rgba) ? TextureOf(rgba, w, h) : nullptr;
}

ID3D11ShaderResourceView* Decode(const std::filesystem::path& path) {
  if (!g_device || !Wic()) return nullptr;
  IWICBitmapDecoder* decoder = nullptr;
  ID3D11ShaderResourceView* view = nullptr;
  if (SUCCEEDED(g_wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                 &decoder))) {
    view = TextureFrom(decoder);
  }
  if (decoder) decoder->Release();
  return view;
}

// A PNG built into the exe (RCDATA), through a WIC stream over the resource's memory.
ID3D11ShaderResourceView* DecodeResource(int id) {
  if (!g_device || !Wic()) return nullptr;
  HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
  HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
  void* data = mem ? LockResource(mem) : nullptr;
  const DWORD size = res ? SizeofResource(nullptr, res) : 0;
  if (!data || !size) return nullptr;
  IWICStream* stream = nullptr;
  IWICBitmapDecoder* decoder = nullptr;
  ID3D11ShaderResourceView* view = nullptr;
  if (SUCCEEDED(g_wic->CreateStream(&stream)) &&
      SUCCEEDED(stream->InitializeFromMemory(static_cast<BYTE*>(data), size)) &&
      SUCCEEDED(g_wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder))) {
    view = TextureFrom(decoder);
  }
  if (decoder) decoder->Release();
  if (stream) stream->Release();
  return view;
}

}  // namespace

void SetTextureDevice(ID3D11Device* device) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  g_device = device;
}

bool g_no_game_art = false;

void SetNoGameArt(bool on) { g_no_game_art = on; }

ImTextureID LoadTexture(const std::filesystem::path& path) {
  if (g_no_game_art) return ImTextureID{};
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

ImTextureID LoadTextureKeyed(const std::filesystem::path& path, int x, int y, int w, int h, uint32_t key_rgb,
                             int low, int high) {
  if (g_no_game_art || path.empty() || w <= 0 || h <= 0 || high <= low) return ImTextureID{};
  const std::wstring id = path.wstring() + L"|" + std::to_wstring(x) + L"," + std::to_wstring(y) + L"," +
                          std::to_wstring(w) + L"," + std::to_wstring(h) + L"," + std::to_wstring(key_rgb);
  auto [it, added] = g_textures.try_emplace(id);
  Entry& e = it->second;
  const auto now = std::chrono::steady_clock::now();
  if (!e.view && (added || now - e.tried > std::chrono::seconds(10)) && g_device && Wic()) {
    e.tried = now;
    IWICBitmapDecoder* decoder = nullptr;
    UINT pw = 0, ph = 0;
    std::vector<uint8_t> rgba;
    std::error_code ec;
    if (std::filesystem::exists(path, ec) &&
        SUCCEEDED(g_wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                   &decoder)) &&
        PixelsFrom(decoder, pw, ph, rgba) && UINT(x + w) <= pw && UINT(y + h) <= ph && x >= 0 && y >= 0) {
      const int kr = int(key_rgb >> 16 & 0xFF), kg = int(key_rgb >> 8 & 0xFF), kb = int(key_rgb & 0xFF);
      std::vector<uint8_t> out(size_t(w) * h * 4);
      for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
          const uint8_t* p = &rgba[(size_t(y + row) * pw + size_t(x + col)) * 4];
          uint8_t* q = &out[(size_t(row) * w + size_t(col)) * 4];
          const int distance = std::abs(p[0] - kr) + std::abs(p[1] - kg) + std::abs(p[2] - kb);
          const int alpha = distance <= low ? 0 : distance >= high ? 255 : (distance - low) * 255 / (high - low);
          q[0] = p[0];
          q[1] = p[1];
          q[2] = p[2];
          q[3] = uint8_t(alpha);
        }
      }
      e.view = TextureOf(out, UINT(w), UINT(h));
    }
    if (decoder) decoder->Release();
  }
  return e.view ? reinterpret_cast<ImTextureID>(e.view) : ImTextureID{};
}

ImTextureID LoadUiTexture(int resource_id) {
  Entry& e = g_ui_textures[resource_id];
  // Built in: one try (once the device is there) is enough.
  if (!e.view && e.tried == std::chrono::steady_clock::time_point{} && g_device) {
    e.tried = std::chrono::steady_clock::now();
    e.view = DecodeResource(resource_id);
  }
  return e.view ? reinterpret_cast<ImTextureID>(e.view) : ImTextureID{};
}

void ReleaseTextures() {
  for (auto& [path, e] : g_textures) {
    if (e.view) e.view->Release();
  }
  g_textures.clear();
  for (auto& [id, e] : g_ui_textures) {
    if (e.view) e.view->Release();
  }
  g_ui_textures.clear();
  if (g_wic) g_wic->Release();
  g_wic = nullptr;
}

}  // namespace coreoflight
