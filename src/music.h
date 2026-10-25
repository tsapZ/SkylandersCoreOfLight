#pragma once

// The chosen game's title music (music.cpp): a WAV the game recorded for the launcher
// (skylanders-portal title_music.h), looped, faded in when its card is chosen and out when another
// one is, through Windows' MCI (no audio library needed).

#include <filesystem>
#include <string>

namespace coreoflight {

class MusicPlayer {
 public:
  ~MusicPlayer();
  // The music to play now: another file fades the current one out first; empty fades out.
  void Play(const std::filesystem::path& file);
  // Each frame: the fades. `dt` in seconds.
  void Update(float dt);
  // At once, no fade (a game starts).
  void Stop();
  // The hub's "Musik-Lautstärke", 0..1: the loudest the music gets.
  void SetLoudness(float loudness);

 private:
  void Open(const std::filesystem::path& file);
  void Close();
  void SetVolume(float v);

  std::filesystem::path playing_, wanted_;
  bool open_ = false;
  float volume_ = 0.0f;  // 0..1, the fade
  float loudness_ = 0.6f;  // 0..1, the setting
  float since_check_ = 0.0f;  // seconds since Update last asked whether it still plays
};

}  // namespace coreoflight
