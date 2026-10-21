cmake_minimum_required(VERSION 3.25)
project(coreoflight LANGUAGES CXX)

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

add_executable(coreoflight WIN32
  src/main.cpp
  src/games.cpp
  src/launcher_ui.cpp
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
  ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(coreoflight PRIVATE src ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(coreoflight PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(coreoflight PRIVATE d3d11 dxgi d3dcompiler bcrypt shell32 ole32)
if(MSVC)
  target_compile_options(coreoflight PRIVATE /W3 /utf-8)
endif()
