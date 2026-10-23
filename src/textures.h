#pragma once

// Pictures from PNG files as Dear ImGui textures (textures.cpp): Windows' WIC decodes, Direct3D 11
// holds them. UI thread only.

#include <filesystem>

#include "imgui.h"

struct ID3D11Device;

namespace coreoflight {

// The device the textures are made on (main.cpp), once at start.
void SetTextureDevice(ID3D11Device* device);

// The picture in `path` (decoded once and kept), or ImTextureID{} if there is none or it can't be
// read; a missing file is looked for again only after a while.
ImTextureID LoadTexture(const std::filesystem::path& path);

// Drops every texture (before the device goes).
void ReleaseTextures();

// --no_game_art, for screenshots to show (the owner, 2026-10-23: only for that, no setting): every
// picture is one of the games' (discs, figures, hats, achievements), so LoadTexture gives none and
// the screens draw their own placeholders (an element's colour and initial, the plain backdrop).
void SetNoGameArt(bool on);

}  // namespace coreoflight
