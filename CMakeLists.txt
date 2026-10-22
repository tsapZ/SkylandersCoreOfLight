cmake_minimum_required(VERSION 3.25)
project(coreoflight LANGUAGES CXX RC)

# Skylanders: Core of Light, the launcher for the recompiled games (docs/roadmap.md, phase 1).
# A small Win32 / Direct3D 11 program with Dear ImGui; it starts each game's own exe, so no game
# depends on it.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
  message(FATAL_ERROR "The launcher is a Windows program, as the games are.")
endif()

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui)
if(NOT EXISTS ${IMGUI_DIR}/imgui.cpp)
  message(FATAL_ERROR "thirdparty/imgui is missing: git submodule update --init")
endif()

# The figure files (names, elements, stats) from skylanders-portal, without the games' SDK: its
# skylanders_portal_figures target, with Windows' own AES and MD5.
set(PORTAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/skylanders-portal)
if(NOT EXISTS ${PORTAL_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "thirdparty/skylanders-portal is missing: git submodule update --init")
endif()
add_subdirectory(${PORTAL_DIR} skylanders-portal EXCLUDE_FROM_ALL)

add_executable(coreoflight WIN32
  src/main.cpp
  src/games.cpp
  src/game_settings.cpp
  src/hub_settings.cpp
  src/disc_import.cpp
  src/file_browser.cpp
  src/music.cpp
  src/figures_ui.cpp
  src/textures.cpp
  src/xiso.cpp
  src/settings_ui.cpp
  src/launcher_ui.cpp
  src/nxe_art.cpp
  src/profiles.cpp
  src/profiles_ui.cpp
  src/achievements.cpp
  src/achievements_ui.cpp
  src/coreoflight.rc
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
  ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(coreoflight PRIVATE src ${IMGUI_DIR} ${IMGUI_DIR}/backends)
# IMGUI_IMPL_WIN32_DISABLE_GAMEPAD: the launcher reads every XInput pad itself (pad_state.h).
target_compile_definitions(coreoflight PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
                                               IMGUI_IMPL_WIN32_DISABLE_GAMEPAD)
target_link_libraries(coreoflight PRIVATE skylanders_portal_figures d3d11 dxgi d3dcompiler bcrypt shell32 ole32 windowscodecs winmm)
if(MSVC)
  target_compile_options(coreoflight PRIVATE /W3 /utf-8)
endif()
