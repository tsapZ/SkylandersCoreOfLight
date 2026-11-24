// updates.h's AssetFitsThisPlatform: a cached release asset from another platform or architecture
// must never be installed or offered (Core of Light #134). No file or network access.

#include <cstdio>
#include <string>

#include "updates.h"

static int g_failures = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

int main() {
#ifdef _WIN32
  CHECK(coreoflight::AssetFitsThisPlatform("SSARecompSetup-0.1.0-beta.7.exe"));
  CHECK(coreoflight::AssetFitsThisPlatform("CoreOfLightPortable-0.5.0.zip"));
  CHECK(!coreoflight::AssetFitsThisPlatform("SSARecomp-linux-amd64.tar.gz"));
  CHECK(!coreoflight::AssetFitsThisPlatform("CoreOfLight-linux-arm64.tar.gz"));
#else
  CHECK(coreoflight::AssetFitsThisPlatform("SSARecomp-linux-" COREOFLIGHT_LINUX_ARCH ".tar.gz"));
  CHECK(!coreoflight::AssetFitsThisPlatform("SSARecompSetup-0.1.0-beta.7.exe"));
  CHECK(!coreoflight::AssetFitsThisPlatform("CoreOfLightPortable-0.5.0.zip"));
  // The other Linux architecture.
  CHECK(!coreoflight::AssetFitsThisPlatform(std::string("SSARecomp-linux-") +
                                            (std::string(COREOFLIGHT_LINUX_ARCH) == "amd64" ? "arm64" : "amd64") +
                                            ".tar.gz"));
#endif
  CHECK(!coreoflight::AssetFitsThisPlatform(""));
  CHECK(!coreoflight::AssetFitsThisPlatform("notes.txt"));

  if (g_failures == 0) std::printf("asset_platform: all tests passed\n");
  return g_failures == 0 ? 0 : 1;
}
