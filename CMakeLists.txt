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

# ── Executable ───────────────────────────────────────────────────────────────

add_executable(mirage
    src/main.cpp
    src/Demuxer.cpp
    src/Decoder.cpp
    src/AudioDecoder.cpp
    src/AudioPlayer.cpp
    src/VideoRenderer.cpp
    src/ScrubBar.cpp
    src/Sync.cpp
)

target_include_directories(mirage PRIVATE
    ${miniaudio_SOURCE_DIR}
)

target_link_libraries(mirage PRIVATE
    OpenGL::GL
    glad_gl_core_41
    PkgConfig::GLFW
    PkgConfig::FFMPEG
)

if(APPLE)
    target_compile_definitions(mirage PRIVATE GL_SILENCE_DEPRECATION)
endif()
