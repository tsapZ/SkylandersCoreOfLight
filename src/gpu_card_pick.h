// Which of the PC's graphics cards the games use, on Linux (gpu_tier.cpp Detect): the launcher sees the cards in
// /sys/class/drm with their PCI vendor and, from AMD's driver, their own memory (mem_info_vram_total); NVIDIA's
// driver gives no memory size there. The card with the most memory wins, as a Radeon next to a Ryzen's Radeon
// graphics should. That took a Ryzen's integrated Radeon (512 MB) over an RTX 5070 Ti, whose 0 MB never beat it
// (Core of Light #163): the launcher then called the card weak (new setups 1x) and the problem report named AMD.
// The games' Vulkan takes the discrete card there (the loader sorts discrete cards first, the SDK's automatic
// pick takes the first one: the same report's game log), and x86 PCs have no NVIDIA integrated graphics, so an
// NVIDIA card wins over integrated graphics (less than 2 GB of its own), and over any card while OpenGL itself
// runs on NVIDIA. Two big cards of different vendors stay with the bigger one, as before.
#pragma once

#include <cstdint>
#include <vector>

namespace coreoflight {

struct DrmCard {
  uint32_t vendor = 0;   // PCI vendor: 0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel
  uint64_t vram_mb = 0;  // its own memory as sysfs says; 0 when sysfs doesn't say (NVIDIA, Intel)
};

inline constexpr uint32_t kVendorNvidia = 0x10DE;
// Integrated graphics have a small part of the main memory of their own (512 MB on a Ryzen 7000, 1 GB on a Steam Deck).
inline constexpr uint64_t kIntegratedMaxMb = 2048;

// cards: the order /sys/class/drm lists them in. gl_on_nvidia: OpenGL's vendor or renderer names NVIDIA.
inline DrmCard PickGameCard(const std::vector<DrmCard>& cards, bool gl_on_nvidia) {
  DrmCard best, nvidia;
  for (const DrmCard& card : cards) {
    if (best.vendor == 0 || card.vram_mb > best.vram_mb) best = card;
    if (card.vendor == kVendorNvidia && (nvidia.vendor == 0 || card.vram_mb > nvidia.vram_mb)) nvidia = card;
  }
  if (nvidia.vendor != 0 && best.vendor != kVendorNvidia && (gl_on_nvidia || best.vram_mb < kIntegratedMaxMb)) {
    return nvidia;
  }
  return best;
}

}  // namespace coreoflight
