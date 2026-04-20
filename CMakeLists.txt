cmake_minimum_required(VERSION 3.28)
project(Mirage C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ─────────────────────────────────────────────────────────────

find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)

# FFmpeg (via homebrew / system pkg-config)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswresample
    libswscale
    libavfilter
)

# GLFW (via homebrew)
pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)

# GLAD2 — generates a minimal OpenGL 4.1 Core loader at configure time
include(FetchContent)
FetchContent_Declare(glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v2.0.8
    SOURCE_SUBDIR  cmake
)
FetchContent_MakeAvailable(glad)
glad_add_library(glad_gl_core_41 STATIC REPRODUCIBLE API gl:core=4.1)

# miniaudio — single header, no build step
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(miniaudio)

# stb — single-header utilities (stb_easy_font for debug HUD text rendering)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(stb)

# pugixml — lightweight XML parser for DASH MPD parsing
FetchContent_Declare(pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG        v1.14
)
FetchContent_MakeAvailable(pugixml)

# Catch2 — unit testing framework
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

# Dear ImGui — immediate-mode GUI
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9
)
FetchContent_MakeAvailable(imgui)

# nativefiledialog-extended — native OS file/folder picker
FetchContent_Declare(nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG        v1.2.1
)
FetchContent_MakeAvailable(nfd)

add_library(imgui_impl STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_impl PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
# Use GLAD2 as the OpenGL loader for the ImGui OpenGL3 backend.
target_compile_definitions(imgui_impl PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD2)
target_link_libraries(imgui_impl PUBLIC PkgConfig::GLFW OpenGL::GL glad_gl_core_41)
if(APPLE)
    target_compile_definitions(imgui_impl PRIVATE GL_SILENCE_DEPRECATION)
endif()

# ── Executable ───────────────────────────────────────────────────────────────

add_executable(mirage
    src/main.cpp
    src/Demuxer.cpp
    src/Decoder.cpp
    src/AudioDecoder.cpp
    src/AudioPlayer.cpp
    src/VideoRenderer.cpp
    src/ScrubBar.cpp
    src/DebugHUD.cpp
    src/ThumbnailStrip.cpp
    src/Sync.cpp
    src/NetworkLogger.cpp
    src/ManifestParser.cpp
    src/PlayerUI.cpp
)

target_include_directories(mirage PRIVATE
    ${miniaudio_SOURCE_DIR}
    ${stb_SOURCE_DIR}
)

target_link_libraries(mirage PRIVATE
    OpenGL::GL
    glad_gl_core_41
    PkgConfig::GLFW
    PkgConfig::FFMPEG
    pugixml
    imgui_impl
    nfd
)

if(APPLE)
    target_compile_definitions(mirage PRIVATE GL_SILENCE_DEPRECATION)
endif()

# ── Unit tests ────────────────────────────────────────────────────────────────

include(CTest)
include(Catch)

add_executable(mirage_tests
    tests/test_network_logger.cpp
    tests/test_manifest_parser.cpp
    src/NetworkLogger.cpp
    src/ManifestParser.cpp
)

target_include_directories(mirage_tests PRIVATE
    src
    ${pugixml_SOURCE_DIR}/src
)

target_link_libraries(mirage_tests PRIVATE
    Catch2::Catch2WithMain
    PkgConfig::FFMPEG
    pugixml
)

catch_discover_tests(mirage_tests)
