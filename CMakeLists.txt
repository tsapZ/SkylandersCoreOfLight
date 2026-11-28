cmake_minimum_required(VERSION 3.25)
if(WIN32)
  project(coreoflight LANGUAGES CXX RC)
else()
  project(coreoflight LANGUAGES CXX)
endif()

# Skylanders: Core of Light, the launcher for the recompiled games (docs/roadmap.md, phase 1).
# A desktop launcher with Dear ImGui (Direct3D 11 on Windows, SDL3 + OpenGL on Linux); it starts
# each game's own executable, so no game depends on it.

# The launcher's version: the problem report names it, tools/package_installer.ps1 builds the
# installer with it, and updates from GitHub Releases (#17) will compare against it.
set(COREOFLIGHT_VERSION "0.5.0-beta.9")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "Unsupported platform: expected Windows or Linux.")
endif()

# The Linux packages are named after the machine's architecture, and so are the release assets the
# launcher looks for. Set here once so the app and the packaging scripts agree.
if(NOT WIN32)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    set(COREOFLIGHT_LINUX_ARCH "amd64")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(COREOFLIGHT_LINUX_ARCH "arm64")
  else()
    message(FATAL_ERROR "Unsupported Linux architecture: ${CMAKE_SYSTEM_PROCESSOR} (expected x86_64 or aarch64)")
  endif()
  message(STATUS "Linux architecture: ${COREOFLIGHT_LINUX_ARCH}")
else()
  # Windows builds are x64; code shared with Linux (SetupVersion's tarball names) keeps amd64 as before.
  set(COREOFLIGHT_LINUX_ARCH "amd64")
endif()

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui)
if(NOT EXISTS ${IMGUI_DIR}/imgui.cpp)
  message(FATAL_ERROR "thirdparty/imgui is missing: git submodule update --init")
endif()

# The figure files (names, elements, stats) from skylanders-portal, without the games' SDK: its
# skylanders_portal_figures target (CNG on Windows, OpenSSL on Linux).
set(PORTAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/skylanders-portal)
if(NOT EXISTS ${PORTAL_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "thirdparty/skylanders-portal is missing: git submodule update --init")
endif()
# Its skylanders_alchemy (the games' archive readers, here the LZMA decoder for Imaginators'
# archives) takes stb_image's zlib decoder from thirdparty/stb.
set(SKYLANDERS_STB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb)
add_subdirectory(${PORTAL_DIR} skylanders-portal EXCLUDE_FROM_ALL)

# Cemu's .wua archives for the Wii U figures (#166, wua_unpack.h): Exzap's ZArchive (MIT-0) on zstd (BSD), pinned
# submodules built here from their sources: the launcher gets the reader and zstd's decoder; the writer, zstd's encoder
# and ZArchive's SHA-256 only go into the test, which writes its own archives. No assembly (ZSTD_DISABLE_ASM): the
# same C on every platform. On Linux zstd's functions are hidden (no ZSTDLIB_VISIBLE, -fvisibility=hidden): the
# launcher's copy must not stand in for the system libzstd that libarchive loads.
enable_language(C)
set(ZSTD_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/zstd/lib)
set(ZARCHIVE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/zarchive)
if(NOT EXISTS ${ZARCHIVE_DIR}/src/zarchivereader.cpp OR NOT EXISTS ${ZSTD_LIB_DIR}/zstd.h)
  message(FATAL_ERROR "thirdparty/zarchive or thirdparty/zstd is missing: git submodule update --init")
endif()
function(coreoflight_zstd_flags target)
  target_include_directories(${target} PRIVATE ${ZSTD_LIB_DIR})
  # ZSTD_TRACE=0: no weak trace hooks (ZSTD_trace_decompress_begin) left open for another library to fill.
  target_compile_definitions(${target} PRIVATE ZSTD_DISABLE_ASM ZSTD_TRACE=0 ZSTDLIB_VISIBLE= ZSTDERRORLIB_VISIBLE=
                                               ZDICTLIB_VISIBLE=)
  if(NOT WIN32)
    set_target_properties(${target} PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
  endif()
endfunction()
file(GLOB COREOFLIGHT_ZSTD_DECODER ${ZSTD_LIB_DIR}/common/*.c ${ZSTD_LIB_DIR}/decompress/*.c)
add_library(coreoflight_zarchive STATIC ${ZARCHIVE_DIR}/src/zarchivereader.cpp ${COREOFLIGHT_ZSTD_DECODER})
target_include_directories(coreoflight_zarchive PUBLIC ${ZARCHIVE_DIR}/include)
target_compile_features(coreoflight_zarchive PUBLIC cxx_std_20)
coreoflight_zstd_flags(coreoflight_zarchive)

set(COREOFLIGHT_SOURCES
  src/main.cpp
  src/platform.cpp
  src/games.cpp
  src/disc_id.cpp
  src/disc_report.cpp
  src/game_settings.cpp
  src/hub_settings.cpp
  src/disc_import.cpp
  src/disc_search.cpp
  src/file_browser.cpp
  src/music.cpp
  src/figure_import.cpp
  src/figures_ui.cpp
  src/textures.cpp
  src/xiso.cpp
  src/settings_ui.cpp
  src/key_names.cpp
  src/setup_ui.cpp
  src/launcher_ui.cpp
  src/hints.cpp
  src/hub_log.cpp
  src/updates.cpp
  src/nxe_art.cpp
  src/profiles.cpp
  src/profiles_ui.cpp
  src/achievements.cpp
  src/achievements_ui.cpp
  src/ds3_games.cpp
  src/ds3_ui.cpp
  src/pe_imports.cpp
  src/problem_report.cpp
  src/saves.cpp
  src/saves_ui.cpp
  src/data_move.cpp
  src/data_transfer.cpp
  src/game_files.cpp
  src/handheld.cpp
  src/gpu_tier.cpp
  src/portable.cpp
  src/language_import.cpp
  src/zip_extract.cpp
  src/wua_unpack.cpp
  src/wiiu_figures.cpp
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
)

if(WIN32)
  add_executable(coreoflight WIN32
    ${COREOFLIGHT_SOURCES}
    src/coreoflight.rc
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
  )
else()
  add_executable(coreoflight
    ${COREOFLIGHT_SOURCES}
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
  )
endif()

# thirdparty/stb: stb_image's zlib decoder for Imaginators' archives (language_import.cpp) and PNG textures.
target_include_directories(coreoflight PRIVATE src ${IMGUI_DIR} ${IMGUI_DIR}/backends thirdparty/stb)
# The suffix in every Linux release asset's name (portable launcher, game setups and updates).
target_compile_definitions(coreoflight PRIVATE COREOFLIGHT_LINUX_ARCH="${COREOFLIGHT_LINUX_ARCH}")
target_compile_definitions(coreoflight PRIVATE COREOFLIGHT_VERSION="${COREOFLIGHT_VERSION}")

if(WIN32)
  # IMGUI_IMPL_WIN32_DISABLE_GAMEPAD: the launcher reads every XInput pad itself (pad_state.h).
  target_compile_definitions(coreoflight PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
                                                 IMGUI_IMPL_WIN32_DISABLE_GAMEPAD)
  target_link_libraries(coreoflight PRIVATE skylanders_portal_figures skylanders_alchemy d3d11 dxgi d3dcompiler bcrypt shell32 ole32 windowscodecs winmm winhttp)
else()
  find_package(OpenGL REQUIRED)
  find_package(SDL3 REQUIRED CONFIG)
  find_package(CURL REQUIRED)
  find_package(OpenSSL REQUIRED)
  find_package(LibArchive REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
  find_package(Threads REQUIRED)
  target_link_libraries(coreoflight PRIVATE
    skylanders_portal_figures
    skylanders_alchemy
    OpenGL::GL
    SDL3::SDL3
    CURL::libcurl
    OpenSSL::Crypto
    LibArchive::LibArchive
    PkgConfig::LIBUSB
    Threads::Threads
    ${CMAKE_DL_LIBS}
  )
  set_target_properties(coreoflight PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()

target_link_libraries(coreoflight PRIVATE coreoflight_zarchive)

# The fallback font next to the exe (main.cpp LoadFont: where system UI fonts aren't there).
add_custom_command(TARGET coreoflight POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/fonts $<TARGET_FILE_DIR:coreoflight>/fonts)
if(MSVC)
  target_compile_options(coreoflight PRIVATE /W3 /utf-8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(coreoflight PRIVATE -Wall -Wextra -Wno-unused-parameter -ffile-prefix-map=${CMAKE_SOURCE_DIR}/=)
endif()

# The disc check's own tests (disc_id.cpp, xiso.cpp; no window and no game data): a synthetic XEX2
# header and a synthetic XDVDFS image. The game lists stay in games.cpp, which needs the launcher's
# window; CheckDiscReport covers them against real discs (tests/README.md).
include(CTest)
if(BUILD_TESTING)
  add_executable(coreoflight_disc_id_test tests/disc_id_test.cpp src/disc_id.cpp src/xiso.cpp)
  target_include_directories(coreoflight_disc_id_test PRIVATE src ${PORTAL_DIR}/src)
  target_compile_features(coreoflight_disc_id_test PRIVATE cxx_std_20)
  if(WIN32)
    target_link_libraries(coreoflight_disc_id_test PRIVATE bcrypt)
  else()
    target_link_libraries(coreoflight_disc_id_test PRIVATE OpenSSL::Crypto Threads::Threads)
  endif()
  add_test(NAME disc_id COMMAND coreoflight_disc_id_test)
  # Which DLLs a game's exe needs and which are gone (Core of Light #146): synthetic PE images.
  add_executable(coreoflight_pe_imports_test tests/pe_imports_test.cpp src/pe_imports.cpp)
  target_include_directories(coreoflight_pe_imports_test PRIVATE src)
  target_compile_features(coreoflight_pe_imports_test PRIVATE cxx_std_20)
  add_test(NAME pe_imports COMMAND coreoflight_pe_imports_test)
  # The problem report's private words (private_words.h): a name only where it stands alone.
  add_executable(coreoflight_private_words_test tests/private_words_test.cpp)
  target_include_directories(coreoflight_private_words_test PRIVATE src)
  target_compile_features(coreoflight_private_words_test PRIVATE cxx_std_20)
  add_test(NAME private_words COMMAND coreoflight_private_words_test)
  # What a game's exit code means: a Linux signal's name (#135: 0x87, a bus error).
  add_executable(coreoflight_exit_code_text_test tests/exit_code_text_test.cpp)
  target_include_directories(coreoflight_exit_code_text_test PRIVATE src)
  target_compile_features(coreoflight_exit_code_text_test PRIVATE cxx_std_20)
  add_test(NAME exit_code_text COMMAND coreoflight_exit_code_text_test)
  # Which Linux graphics card is the games' one: an NVIDIA card over a Ryzen's graphics (#163).
  add_executable(coreoflight_gpu_card_pick_test tests/gpu_card_pick_test.cpp)
  target_include_directories(coreoflight_gpu_card_pick_test PRIVATE src)
  target_compile_features(coreoflight_gpu_card_pick_test PRIVATE cxx_std_20)
  add_test(NAME gpu_card_pick COMMAND coreoflight_gpu_card_pick_test)
  # Which cached release assets belong to this build's platform and architecture (#134).
  add_executable(coreoflight_asset_platform_test tests/asset_platform_test.cpp)
  target_include_directories(coreoflight_asset_platform_test PRIVATE src)
  target_compile_definitions(coreoflight_asset_platform_test PRIVATE COREOFLIGHT_LINUX_ARCH="${COREOFLIGHT_LINUX_ARCH}")
  target_compile_features(coreoflight_asset_platform_test PRIVATE cxx_std_20)
  add_test(NAME asset_platform COMMAND coreoflight_asset_platform_test)
  # The update extractor's containment (platform.cpp ExtractArchive): synthetic tarballs only.
  if(NOT WIN32)
    add_executable(coreoflight_archive_extract_test tests/archive_extract_test.cpp src/platform.cpp)
    target_include_directories(coreoflight_archive_extract_test PRIVATE src)
    target_compile_features(coreoflight_archive_extract_test PRIVATE cxx_std_20)
    target_link_libraries(coreoflight_archive_extract_test PRIVATE LibArchive::LibArchive OpenSSL::Crypto Threads::Threads)
    add_test(NAME archive_extract COMMAND coreoflight_archive_extract_test)
  endif()
  # The portable update's ZIP reader (portable.cpp ReadZipFiles): synthetic ZIPs only.
  add_executable(coreoflight_portable_zip_test tests/portable_package_test.cpp src/portable.cpp src/platform.cpp
                 src/hub_log.cpp)
  target_include_directories(coreoflight_portable_zip_test PRIVATE src)
  target_compile_features(coreoflight_portable_zip_test PRIVATE cxx_std_20)
  target_compile_definitions(coreoflight_portable_zip_test PRIVATE COREOFLIGHT_LINUX_ARCH="${COREOFLIGHT_LINUX_ARCH}")
  if(WIN32)
    target_link_libraries(coreoflight_portable_zip_test PRIVATE bcrypt)
  else()
    target_link_libraries(coreoflight_portable_zip_test PRIVATE LibArchive::LibArchive OpenSSL::Crypto Threads::Threads)
  endif()
  add_test(NAME portable_zip COMMAND coreoflight_portable_zip_test)
  # The .wua unpacking for the Wii U figures (wua_unpack.cpp, #166): archives the test writes itself with ZArchive's
  # writer on zstd's encoder (only here).
  file(GLOB COREOFLIGHT_ZSTD_ENCODER ${ZSTD_LIB_DIR}/compress/*.c)
  add_library(coreoflight_zarchive_writer STATIC ${ZARCHIVE_DIR}/src/zarchivewriter.cpp ${ZARCHIVE_DIR}/src/sha_256.c
              ${COREOFLIGHT_ZSTD_ENCODER})
  target_link_libraries(coreoflight_zarchive_writer PUBLIC coreoflight_zarchive)
  coreoflight_zstd_flags(coreoflight_zarchive_writer)
  add_executable(coreoflight_wua_unpack_test tests/wua_unpack_test.cpp src/wua_unpack.cpp src/platform.cpp
                 src/hub_log.cpp)
  target_include_directories(coreoflight_wua_unpack_test PRIVATE src)
  target_compile_definitions(coreoflight_wua_unpack_test PRIVATE COREOFLIGHT_LINUX_ARCH="${COREOFLIGHT_LINUX_ARCH}")
  target_link_libraries(coreoflight_wua_unpack_test PRIVATE coreoflight_zarchive_writer)
  if(WIN32)
    target_link_libraries(coreoflight_wua_unpack_test PRIVATE bcrypt)
  else()
    target_link_libraries(coreoflight_wua_unpack_test PRIVATE LibArchive::LibArchive OpenSSL::Crypto Threads::Threads)
  endif()
  add_test(NAME wua_unpack COMMAND coreoflight_wua_unpack_test)
endif()
