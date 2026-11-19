#include "music.h"

#include "hub_log.h"
#include "platform.h"

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <cmath>
#include <string>

namespace coreoflight {
namespace {

#ifdef _WIN32
constexpr wchar_t kAlias[] = L"coreoflight_title";
bool Mci(const std::wstring& command) { return mciSendStringW(command.c_str(), nullptr, 0, nullptr) == 0; }
#endif
constexpr float kFadeIn = 1.2f, kFadeOut = 0.6f;  // seconds
constexpr float kLevel = 0.7f;                    // not at full loudness behind the menus

}  // namespace

MusicPlayer::~MusicPlayer() { Close(); }

void MusicPlayer::Play(const std::filesystem::path& file) { wanted_ = file; }

void MusicPlayer::Stop() {
  wanted_.clear();
  Close();
}

void MusicPlayer::Open(const std::filesystem::path& file) {
  Close();
#ifdef _WIN32
  // mpegvideo (DirectShow) plays WAV, loops with "repeat" and has a volume.
  if (!Mci(L"open \"" + file.wstring() + L"\" type mpegvideo alias " + kAlias)) return;
  open_ = true;
  playing_ = file;
  volume_ = 0.0f;
  SetVolume(0.0f);
  Mci(std::wstring(L"play ") + kAlias + L" repeat");
#else
  if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
  }
  SDL_AudioSpec spec{};
  Uint8* buf = nullptr;
  Uint32 len = 0;
  if (!SDL_LoadWAV(PathUtf8(file).c_str(), &spec, &buf, &len) || !buf || len == 0) {
    if (buf) SDL_free(buf);
    return;
  }
  pcm_.assign(buf, buf + len);
  SDL_free(buf);
  pcm_offset_ = 0;
  bytes_per_sec_ = std::max(1, spec.freq * spec.channels * int(SDL_AUDIO_BYTESIZE(spec.format)));
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (!stream_) {
    pcm_.clear();
    return;
  }
  open_ = true;
  playing_ = file;
  volume_ = 0.0f;
  SetVolume(0.0f);
  const size_t initial = std::min(pcm_.size(), size_t(bytes_per_sec_));
  SDL_PutAudioStreamData(stream_, pcm_.data(), int(initial));
  pcm_offset_ = initial % pcm_.size();
  SDL_ResumeAudioStreamDevice(stream_);
#endif
}

void MusicPlayer::Close() {
#ifdef _WIN32
  if (open_) Mci(std::wstring(L"close ") + kAlias);
#else
  if (stream_) {
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
  }
  pcm_.clear();
  pcm_offset_ = 0;
  bytes_per_sec_ = 0;
#endif
  open_ = false;
  playing_.clear();
  volume_ = 0.0f;
}

void MusicPlayer::SetVolume(float v) {
  const float loud = v * loudness_;  // the fade times the owner's setting
#ifdef _WIN32
  // MCI's volume is 0..1000; the ear follows its square better than its line.
  const int level = int(std::lround(1000.0f * loud * loud * kLevel));
  Mci(std::wstring(L"setaudio ") + kAlias + L" volume to " + std::to_wstring(level));
#else
  if (stream_) SDL_SetAudioStreamGain(stream_, std::clamp(loud * loud * kLevel, 0.0f, 1.0f));
#endif
}

void MusicPlayer::SetLoudness(float loudness) {
  loudness = std::clamp(loudness, 0.0f, 1.0f);
  if (loudness == loudness_) return;
  loudness_ = loudness;
  if (open_) SetVolume(volume_);  // a change heard at once
}

void MusicPlayer::Update(float dt) {
  if (open_ && playing_ != wanted_) {
    // Another (or no) music wanted: out first.
    volume_ = std::max(0.0f, volume_ - dt / kFadeOut);
    SetVolume(volume_);
    if (volume_ <= 0.0f) Close();
    return;
  }
  if (!open_ && !wanted_.empty()) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(wanted_, ec)) Open(wanted_);
    else wanted_.clear();
    return;
  }
  if (open_ && volume_ < 1.0f) {
    volume_ = std::min(1.0f, volume_ + dt / kFadeIn);
    SetVolume(volume_);
  }
#ifdef _WIN32
  // "repeat" doesn't always hold with mpegvideo: the owner heard the title music stop after its
  // ~40 s (2026-10-25). Twice a second: if it stopped, from the start again.
  since_check_ += dt;
  if (open_ && since_check_ >= 0.5f) {
    since_check_ = 0.0f;
    wchar_t mode[32] = L"";
    const std::wstring status = std::wstring(L"status ") + kAlias + L" mode";
    if (mciSendStringW(status.c_str(), mode, 32, nullptr) == 0 && std::wstring(mode) == L"stopped") {
      Mci(std::wstring(L"seek ") + kAlias + L" to start");
      Mci(std::wstring(L"play ") + kAlias + L" repeat");
      HubLog("music: played through, from the start again");
    }
  }
#else
  (void)since_check_;
  if (open_ && stream_ && !pcm_.empty()) {
    const int target_queued = std::max(4096, bytes_per_sec_ / 2);
    while (SDL_GetAudioStreamQueued(stream_) < target_queued) {
      const size_t avail = pcm_.size() - pcm_offset_;
      const size_t chunk = std::min(avail, size_t(bytes_per_sec_ / 4));
      if (!SDL_PutAudioStreamData(stream_, pcm_.data() + pcm_offset_, int(chunk))) break;
      pcm_offset_ = (pcm_offset_ + chunk) % pcm_.size();
    }
  }
#endif
}

}  // namespace coreoflight
