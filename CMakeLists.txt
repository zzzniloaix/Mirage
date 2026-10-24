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

# libvmaf (via homebrew — brew install libvmaf)
pkg_check_modules(VMAF REQUIRED IMPORTED_TARGET libvmaf)

# libvmaf's package version, which can differ from the version the library
# reports through vmaf_version() (DECISIONS.md D-006). Read from the real path
# of the library pkg-config resolved, e.g. a Homebrew Cellar path; "unknown"
# when that path carries no version. mirage_vmaf records it in its reports.
set(MIRAGE_LIBVMAF_PACKAGE_VERSION "unknown")
foreach(_vmaf_lib IN LISTS VMAF_LINK_LIBRARIES)
    file(REAL_PATH "${_vmaf_lib}" _vmaf_real)
    if(_vmaf_real MATCHES "/Cellar/libvmaf/([^/]+)/")
        set(MIRAGE_LIBVMAF_PACKAGE_VERSION "${CMAKE_MATCH_1}")
    endif()
endforeach()
message(STATUS "libvmaf package version: ${MIRAGE_LIBVMAF_PACKAGE_VERSION}")

# Where the libvmaf package keeps its model files; mirage_tests loads one
# through mirage_vmaf's --model-file path, and skips that test without it.
pkg_get_variable(MIRAGE_LIBVMAF_PREFIX libvmaf prefix)
set(MIRAGE_LIBVMAF_MODEL_DIR "${MIRAGE_LIBVMAF_PREFIX}/share/libvmaf/model")

# GLAD2 — the OpenGL 4.1 Core loader glad 2.0.8 generated, committed under
# third_party/glad (DECISIONS.md D-031) and built as glad's own CMake function
# built it, so the build runs no Python. third_party/glad/README.md says how
# it was generated and how to regenerate it.
add_library(glad_gl_core_41 STATIC third_party/glad/src/gl.c)
target_include_directories(glad_gl_core_41 PUBLIC third_party/glad/include)
target_link_libraries(glad_gl_core_41 PUBLIC ${CMAKE_DL_LIBS})

include(FetchContent)

# miniaudio and stb used to follow their master branch. Both are pinned to the
# commit the Phase 0 baseline build fetched (eval/baseline/environment.txt);
# the other dependencies are pinned by release tag.

# miniaudio — single header, no build step
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        9634bedb5b5a2ca38c1ee7108a9358a4e233f14d   # on master, committed 2026-03-04
)
FetchContent_MakeAvailable(miniaudio)

# stb — single-header utilities (stb_easy_font for debug HUD text rendering)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d   # on master, committed 2026-10-19
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
    src/MediaSession.cpp
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
    src/NetDescribe.cpp
    src/NetInspector.cpp
    src/NetInspectorAlign.cpp
    src/NetInspectorResponse.cpp
    src/NetProbe.cpp
    src/TrackPanel.cpp
    src/TrackGroups.cpp
    src/ManifestParser.cpp
    src/PlayerUI.cpp
    src/VMAFAlign.cpp
    src/VMAFAnalyzer.cpp
    src/VMAFColor.cpp
    src/VMAFPool.cpp
    src/FileHash.cpp
    src/ScrubDecoder.cpp
    src/SubtitleDecoder.cpp
    src/SubtitleSelect.cpp
    src/SubtitleOverlay.cpp
    src/SidecarSubtitles.cpp
    src/UiFonts.cpp
    src/UiFormat.cpp
    src/UiIcons.cpp
    src/UiWidgets.cpp
    src/ThumbPlan.cpp
    src/FfOpen.cpp
    src/HlsMaster.cpp
    src/HlsVariants.cpp
    src/HlsAlign.cpp
    src/DemuxRetry.cpp
    src/PipelineState.cpp
    src/KeyframeIndex.cpp
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
    PkgConfig::VMAF
    pugixml
    imgui_impl
    nfd
)

if(APPLE)
    target_compile_definitions(mirage PRIVATE GL_SILENCE_DEPRECATION)
    # VideoToolbox zero-copy path: CVPixelBuffer + IOSurface + CGLTexImageIOSurface2D.
    target_link_libraries(mirage PRIVATE
        "-framework CoreVideo"
        "-framework IOSurface"
    )
endif()

# ── Headless VMAF scorer ─────────────────────────────────────────────────────

# The pooled values are computed as eval/pooling/pooling.py computes them,
# each multiplication and addition rounded on its own: no fused multiply-add
# (src/VMAFPool.cpp). The player, mirage_vmaf and mirage_tests all compile it.
set_source_files_properties(src/VMAFPool.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")

# mirage_vmaf: the player's VMAF analyzer without a window, GL or ImGui
# (DECISIONS.md D-017). Links FFmpeg and libvmaf only. Its ladder scoring
# (D-029) reads master playlists itself, with HlsMaster's URI resolution,
# not through the player's ManifestParser and its network logger.
add_executable(mirage_vmaf
    tools/mirage_vmaf.cpp
    src/VMAFAlign.cpp
    src/VMAFAnalyzer.cpp
    src/VMAFColor.cpp
    src/VMAFLadder.cpp
    src/VMAFPool.cpp
    src/FileHash.cpp
    src/HlsMaster.cpp
    src/VMAFCli.cpp
    src/VMAFReport.cpp
    src/JsonText.cpp
)

target_include_directories(mirage_vmaf PRIVATE src)

target_link_libraries(mirage_vmaf PRIVATE
    PkgConfig::FFMPEG
    PkgConfig::VMAF
)

target_compile_definitions(mirage_vmaf PRIVATE
    MIRAGE_LIBVMAF_PACKAGE_VERSION="${MIRAGE_LIBVMAF_PACKAGE_VERSION}"
)

# ── Unit tests ────────────────────────────────────────────────────────────────

include(CTest)
include(Catch)

add_executable(mirage_tests
    tests/test_network_logger.cpp
    tests/test_manifest_parser.cpp
    tests/test_subtitle_decoder.cpp
    tests/test_yuv_matrix.cpp
    tests/test_sync.cpp
    tests/test_audio_player.cpp
    tests/test_audio_decoder.cpp
    tests/test_queue.cpp
    tests/test_ui_format.cpp
    tests/test_thumb_plan.cpp
    tests/test_subtitle_select.cpp
    tests/test_demux_retry.cpp
    tests/test_demuxer_switch.cpp
    tests/test_track_groups.cpp
    tests/test_pipeline_state.cpp
    tests/test_hls_variants.cpp
    tests/test_hls_master.cpp
    tests/test_keyframe_index.cpp
    tests/test_scrub_follow.cpp
    tests/test_hls_align.cpp
    tests/test_json_text.cpp
    tests/test_vmaf_cli.cpp
    tests/test_vmaf_report.cpp
    tests/test_vmaf_align.cpp
    tests/test_vmaf_analyzer.cpp
    tests/test_vmaf_color.cpp
    tests/test_vmaf_ladder.cpp
    tests/test_vmaf_pool.cpp
    tests/test_file_hash.cpp
    src/Demuxer.cpp
    src/NetworkLogger.cpp
    src/NetDescribe.cpp
    src/NetProbe.cpp
    src/ManifestParser.cpp
    src/SubtitleDecoder.cpp
    src/Sync.cpp
    src/AudioPlayer.cpp
    src/AudioDecoder.cpp
    src/Decoder.cpp
    src/UiFormat.cpp
    src/SubtitleSelect.cpp
    src/SidecarSubtitles.cpp
    src/ThumbPlan.cpp
    src/FfOpen.cpp
    src/HlsMaster.cpp
    src/HlsVariants.cpp
    src/HlsAlign.cpp
    src/DemuxRetry.cpp
    src/TrackGroups.cpp
    src/PipelineState.cpp
    src/KeyframeIndex.cpp
    src/ScrubDecoder.cpp
    src/JsonText.cpp
    src/VMAFCli.cpp
    src/VMAFReport.cpp
    src/VMAFAlign.cpp
    src/VMAFAnalyzer.cpp
    src/VMAFColor.cpp
    src/VMAFLadder.cpp
    src/VMAFPool.cpp
    src/FileHash.cpp
)

target_include_directories(mirage_tests PRIVATE
    src
    ${pugixml_SOURCE_DIR}/src
    ${miniaudio_SOURCE_DIR}
)

target_link_libraries(mirage_tests PRIVATE
    Catch2::Catch2WithMain
    PkgConfig::FFMPEG
    PkgConfig::VMAF
    pugixml
)

target_compile_definitions(mirage_tests PRIVATE
    MIRAGE_LIBVMAF_MODEL_DIR="${MIRAGE_LIBVMAF_MODEL_DIR}"
)

# Small standalone: pixel-diff two PNGs via stb_image, used by
# tests/pathb_snapshot_regression.sh to catch HW-vs-SW render drift.
add_executable(snapshot_diff tests/snapshot_diff.cpp)
target_include_directories(snapshot_diff PRIVATE ${stb_SOURCE_DIR})

# Convenience target: run the HW-vs-SW visual regression check. Requires a
# display (mirage opens a GLFW window) — not wired into ctest for this reason.
#   cmake --build --preset macos-debug --target pathb_regression
add_custom_target(pathb_regression
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/pathb_snapshot_regression.sh
            ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS mirage snapshot_diff
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running HW vs SW snapshot regression check"
    USES_TERMINAL
)

catch_discover_tests(mirage_tests)
