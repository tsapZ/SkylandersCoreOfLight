#pragma once

// Weak graphics cards (gpu_tier.cpp): a game set up from the launcher gets resolution scale 1x
// instead of 2x on them, as on a handheld (handheld.h). At 2x the games are limited by the graphics
// card even on fast ones (Swap Force, Trap Team and Imaginators about 66-70 FPS in their levels on
// an RX 6800 XT, 2026-11-03), so a card of a third of that runs them at about 30 (a GTX 1070:
// "30 fps, gpu 100 %", a player's report, 2026-11-05). The owner's wish, 2026-11-05: weak cards
// get 1x by default. Only a new setup's settings file gets it (the setup never overwrites one);
// the game's settings page still offers every scale.

#include <cstdint>
#include <string>

namespace coreoflight {

// The graphics card the games use (the first hardware DXGI adapter, as the games' Direct3D 12
// pick it), read once.
struct GpuInfo {
  std::string name;        // the adapter's description, e.g. "NVIDIA GeForce GTX 1070"
  uint32_t vendor = 0;     // PCI vendor: 0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel
  uint64_t vram_mb = 0;    // its own memory; integrated graphics have little or none
  bool weak = false;
  std::string reason;      // why it counts as weak, for the log ("integrated graphics", ...)
};
const GpuInfo& DetectGpu();

// Why a card counts as weak at 2x, or empty. By name, since the cards' own numbers (memory, ids)
// don't say how fast they are; the line is about a GTX 1070, which runs the games at about 30 FPS
// at 2x. Integrated graphics by their small memory of their own.
std::string WeakGpuReason(const GpuInfo& gpu);

}  // namespace coreoflight
