#include "textures.h"

#include "platform.h"

#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#else
#include <SDL3/SDL_opengl.h>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <stb_image.h>
#endif

#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace coreoflight {
namespace {

#ifdef _WIN32
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
#else
bool g_gl_ready = false;

struct Entry {
  GLuint tex = 0;
  std::chrono::steady_clock::time_point tried;
};
std::unordered_map<std::string, Entry> g_textures;

GLuint TextureOf(const uint8_t* rgba, int w, int h) {
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  return tex;
}

constexpr size_t kMaxTextureFileBytes = size_t(64) << 20;
constexpr int kMaxTextureSide = 4096;

stbi_uc* DecodeRgba(const std::filesystem::path& path, int& w, int& h) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return nullptr;
  const std::streamoff end = in.tellg();
  if (end <= 0 || end > std::streamoff(kMaxTextureFileBytes)) return nullptr;
  const auto size = size_t(end);
  std::vector<stbi_uc> file(size);
  in.seekg(0, std::ios::beg);
  in.read(reinterpret_cast<char*>(file.data()), std::streamsize(size));
  if (in.gcount() != std::streamsize(size)) return nullptr;
  int channels = 0;
  // Check the dimensions in the header before the decoder allocates the RGBA pixels.
  int info_w = 0, info_h = 0;
  if (!stbi_info_from_memory(file.data(), int(file.size()), &info_w, &info_h, &channels) || info_w <= 0 ||
      info_h <= 0 || info_w > kMaxTextureSide || info_h > kMaxTextureSide) {
    return nullptr;
  }
  stbi_uc* rgba = stbi_load_from_memory(file.data(), int(file.size()), &w, &h, &channels, 4);
  if (!rgba) return nullptr;
  if (w <= 0 || h <= 0 || w > kMaxTextureSide || h > kMaxTextureSide) {
    stbi_image_free(rgba);
    return nullptr;
  }
  return rgba;
}

GLuint Decode(const std::filesystem::path& path) {
  if (!g_gl_ready) return 0;
  int w = 0, h = 0;
  stbi_uc* rgba = DecodeRgba(path, w, h);
  if (!rgba) return 0;
  const GLuint tex = TextureOf(rgba, w, h);
  stbi_image_free(rgba);
  return tex;
}
#endif

}  // namespace

#ifdef _WIN32
void SetTextureDevice(ID3D11Device* device) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  g_device = device;
}
#else
void SetTextureDevice(void* device) { g_gl_ready = device != nullptr; }
#endif

bool g_no_game_art = false;

void SetNoGameArt(bool on) { g_no_game_art = on; }

ImTextureID LoadTexture(const std::filesystem::path& path) {
  if (g_no_game_art) return ImTextureID{};
  const auto now = std::chrono::steady_clock::now();
#ifdef _WIN32
  auto [it, added] = g_textures.try_emplace(path.wstring());
  Entry& e = it->second;
  if (!e.view && (added || now - e.tried > std::chrono::seconds(10))) {
    e.tried = now;
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) e.view = Decode(path);
  }
  return e.view ? reinterpret_cast<ImTextureID>(e.view) : ImTextureID{};
#else
  auto [it, added] = g_textures.try_emplace(PathUtf8(path));
  Entry& e = it->second;
  if (!e.tex && (added || now - e.tried > std::chrono::seconds(10))) {
    e.tried = now;
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) e.tex = Decode(path);
  }
  return e.tex ? ImTextureID(uintptr_t(e.tex)) : ImTextureID{};
#endif
}

std::vector<uint8_t> KeyedCrop(const uint8_t* rgba, int pw, int x, int y, int w, int h, uint32_t key_rgb, int low,
                               int high) {
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
  return out;
}

ImTextureID LoadTextureKeyed(const std::filesystem::path& path, int x, int y, int w, int h, uint32_t key_rgb,
                             int low, int high) {
  if (g_no_game_art || path.empty() || w <= 0 || h <= 0 || high <= low) return ImTextureID{};
  const auto now = std::chrono::steady_clock::now();
#ifdef _WIN32
  const std::wstring id = path.wstring() + L"|" + std::to_wstring(x) + L"," + std::to_wstring(y) + L"," +
                          std::to_wstring(w) + L"," + std::to_wstring(h) + L"," + std::to_wstring(key_rgb);
  auto [it, added] = g_textures.try_emplace(id);
  Entry& e = it->second;
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
      e.view = TextureOf(KeyedCrop(rgba.data(), int(pw), x, y, w, h, key_rgb, low, high), UINT(w), UINT(h));
    }
    if (decoder) decoder->Release();
  }
  return e.view ? reinterpret_cast<ImTextureID>(e.view) : ImTextureID{};
#else
  const std::string id = PathUtf8(path) + "|" + std::to_string(x) + "," + std::to_string(y) + "," +
                         std::to_string(w) + "," + std::to_string(h) + "," + std::to_string(key_rgb);
  auto [it, added] = g_textures.try_emplace(id);
  Entry& e = it->second;
  if (!e.tex && (added || now - e.tried > std::chrono::seconds(10)) && g_gl_ready) {
    e.tried = now;
    int pw = 0, ph = 0;
    if (stbi_uc* rgba = DecodeRgba(path, pw, ph)) {
      if (x >= 0 && y >= 0 && x + w <= pw && y + h <= ph) {
        const std::vector<uint8_t> out = KeyedCrop(rgba, pw, x, y, w, h, key_rgb, low, high);
        e.tex = TextureOf(out.data(), w, h);
      }
      stbi_image_free(rgba);
    }
  }
  return e.tex ? ImTextureID(uintptr_t(e.tex)) : ImTextureID{};
#endif
}

ImTextureID LoadTextureRgba(const std::string& key, int w, int h, const std::vector<uint8_t>& rgba) {
  if (g_no_game_art || w <= 0 || h <= 0 || rgba.size() < size_t(w) * size_t(h) * 4) return ImTextureID{};
#ifdef _WIN32
  // Its own key space ("rgba:"), apart from the file paths.
  auto [it, added] = g_textures.try_emplace(L"rgba:" + std::wstring(key.begin(), key.end()));
  Entry& e = it->second;
  if (added && g_device) e.view = TextureOf(rgba, UINT(w), UINT(h));
  return e.view ? reinterpret_cast<ImTextureID>(e.view) : ImTextureID{};
#else
  auto [it, added] = g_textures.try_emplace("rgba:" + key);
  Entry& e = it->second;
  if (added && g_gl_ready) e.tex = TextureOf(rgba.data(), w, h);
  return e.tex ? ImTextureID(uintptr_t(e.tex)) : ImTextureID{};
#endif
}

ImTextureID LoadUiTexture(int resource_id) {
#ifdef _WIN32
  Entry& e = g_ui_textures[resource_id];
  // Built in: one try (once the device is there) is enough.
  if (!e.view && e.tried == std::chrono::steady_clock::time_point{} && g_device) {
    e.tried = std::chrono::steady_clock::now();
    e.view = DecodeResource(resource_id);
  }
  return e.view ? reinterpret_cast<ImTextureID>(e.view) : ImTextureID{};
#else
  (void)resource_id;
  return ImTextureID{};
#endif
}

void ReleaseTextures() {
#ifdef _WIN32
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
#else
  for (auto& [path, e] : g_textures) {
    if (e.tex) glDeleteTextures(1, &e.tex);
  }
  g_textures.clear();
#endif
}

}  // namespace coreoflight
