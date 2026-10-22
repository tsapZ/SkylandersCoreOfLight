#include "music.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace coreoflight {
namespace {

constexpr wchar_t kAlias[] = L"coreoflight_title";
constexpr float kFadeIn = 1.2f, kFadeOut = 0.6f;  // seconds
constexpr float kLevel = 0.7f;                    // not at full loudness behind the menus

bool Mci(const std::wstring& command) { return mciSendStringW(command.c_str(), nullptr, 0, nullptr) == 0; }

}  // namespace

MusicPlayer::~MusicPlayer() { Close(); }

void MusicPlayer::Play(const std::filesystem::path& file) { wanted_ = file; }

void MusicPlayer::Stop() {
  wanted_.clear();
  Close();
}

void MusicPlayer::Open(const std::filesystem::path& file) {
  Close();
  // mpegvideo (DirectShow) plays WAV, loops with "repeat" and has a volume.
  if (!Mci(L"open \"" + file.wstring() + L"\" type mpegvideo alias " + kAlias)) return;
  open_ = true;
  playing_ = file;
  volume_ = 0.0f;
  SetVolume(0.0f);
  Mci(std::wstring(L"play ") + kAlias + L" repeat");
}

void MusicPlayer::Close() {
  if (open_) Mci(std::wstring(L"close ") + kAlias);
  open_ = false;
  playing_.clear();
  volume_ = 0.0f;
}

void MusicPlayer::SetVolume(float v) {
  // MCI's volume is 0..1000; the ear follows its square better than its line.
  const int level = int(std::lround(1000.0f * v * v * kLevel));
  Mci(std::wstring(L"setaudio ") + kAlias + L" volume to " + std::to_wstring(level));
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
}

}  // namespace coreoflight
