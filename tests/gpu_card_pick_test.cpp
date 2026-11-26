// gpu_card_pick.h's tests: which of the Linux PC's graphics cards counts as the games' card (gpu_tier.cpp
// Detect). Invented machines, one per case the comment names.

#include <cstdio>

#include "gpu_card_pick.h"

using coreoflight::DrmCard;
using coreoflight::PickGameCard;

static int g_failures = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

static bool Is(const DrmCard& card, uint32_t vendor, uint64_t vram_mb) {
  return card.vendor == vendor && card.vram_mb == vram_mb;
}

int main() {
  constexpr uint32_t kAmd = 0x1002, kNvidia = 0x10DE, kIntel = 0x8086;
  // One card: that one, whatever sysfs says about its memory.
  CHECK(Is(PickGameCard({{kAmd, 16368}}, false), kAmd, 16368));
  CHECK(Is(PickGameCard({{kNvidia, 0}}, true), kNvidia, 0));
  CHECK(Is(PickGameCard({{kAmd, 1024}}, false), kAmd, 1024));  // a handheld's own graphics
  // Core of Light #163: a Ryzen's Radeon graphics (512 MB) listed first, an NVIDIA card with no size, OpenGL on it.
  CHECK(Is(PickGameCard({{kAmd, 512}, {kNvidia, 0}}, true), kNvidia, 0));
  // The same with OpenGL on the integrated graphics (a laptop's default): the NVIDIA card still.
  CHECK(Is(PickGameCard({{kAmd, 512}, {kNvidia, 0}}, false), kNvidia, 0));
  CHECK(Is(PickGameCard({{kIntel, 0}, {kNvidia, 0}}, false), kNvidia, 0));
  CHECK(Is(PickGameCard({{kNvidia, 0}, {kIntel, 0}}, false), kNvidia, 0));
  // A Radeon card next to a Ryzen's Radeon graphics: the bigger one, in either order.
  CHECK(Is(PickGameCard({{kAmd, 512}, {kAmd, 16368}}, false), kAmd, 16368));
  CHECK(Is(PickGameCard({{kAmd, 16368}, {kAmd, 512}}, false), kAmd, 16368));
  // A big Radeon card and an NVIDIA card: the Radeon while OpenGL runs on it, NVIDIA when OpenGL runs there.
  CHECK(Is(PickGameCard({{kAmd, 16368}, {kNvidia, 0}}, false), kAmd, 16368));
  CHECK(Is(PickGameCard({{kAmd, 16368}, {kNvidia, 0}}, true), kNvidia, 0));
  // No card at all (a container without /sys/class/drm): nothing.
  CHECK(Is(PickGameCard({}, false), 0, 0));
  if (g_failures == 0) std::printf("gpu_card_pick: all passed\n");
  return g_failures == 0 ? 0 : 1;
}
