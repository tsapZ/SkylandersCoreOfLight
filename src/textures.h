#pragma once

// Pictures from PNG files as Dear ImGui textures (textures.cpp). UI thread only.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"

#ifdef _WIN32
struct ID3D11Device;
#endif

namespace coreoflight {

// The device / graphics context readiness the textures are made on (main.cpp), once at start.
#ifdef _WIN32
void SetTextureDevice(ID3D11Device* device);
#else
void SetTextureDevice(void* device);
#endif

// The picture in `path` (decoded once and kept), or ImTextureID{} if there is none or it can't be
// read; a missing file is looked for again only after a while.
ImTextureID LoadTexture(const std::filesystem::path& path);

// A picture made in memory (`w` x `h` RGBA rows), kept under `key` (ASCII, e.g. "ds3:1:P"): made
// on the first call, the same texture after that. Game art (the 3DS dumps' icons), so
// --no_game_art leaves it out.
ImTextureID LoadTextureRgba(const std::string& key, int w, int h, const std::vector<uint8_t>& rgba);

// The launcher's own pictures built into the exe (coreoflight.rc, RCDATA): morpheus284's frames for the
// start screen (#78, CC BY 4.0). Not the games' art, so --no_game_art keeps them. Decoded once and kept;
// ImTextureID{} if the resource is missing or can't be read (the screen then draws its own shapes).
constexpr int kUiCardBase = 101;
constexpr int kUiCardLight = 102;
constexpr int kUiOptionBar = 103;
constexpr int kUiBackground = 104;
constexpr int kUiStoneEdge = 105;  // a piece of a frame's top edge, notch to notch (repeats), 156x32
ImTextureID LoadUiTexture(int resource_id);

// A part of the picture in `path` (x, y, w, h in its pixels) with the colour `key_rgb` (0xRRGGBB) made clear:
// colours within `low` (the sum of the three channels' differences) clear, beyond `high` solid, a ramp
// between (for the soft edges of a JPEG). For a logo on a plain background in one of the games' own
// pictures (the player's disc), so it is game art: --no_game_art gives none. Kept like LoadTexture.
ImTextureID LoadTextureKeyed(const std::filesystem::path& path, int x, int y, int w, int h, uint32_t key_rgb,
                             int low, int high);

// Drops every texture (before the device goes).
void ReleaseTextures();

// --no_game_art, for screenshots to show (the owner, 2026-10-23: only for that, no setting): every
// picture is one of the games' (discs, figures, hats, achievements), so LoadTexture gives none and
// the screens draw their own placeholders (an element's colour and initial, the plain backdrop).
void SetNoGameArt(bool on);

}  // namespace coreoflight
