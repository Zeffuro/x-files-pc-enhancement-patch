cmake_minimum_required(VERSION 3.24)
set(XFILES_VERSION "0.1.0" CACHE STRING "Release version (major.minor.patch)")
if(NOT XFILES_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "XFILES_VERSION must be major.minor.patch")
endif()
project(XFilesEnhancement VERSION ${XFILES_VERSION} LANGUAGES C CXX RC)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Build with MSVC and -A Win32; the game uses the x86 ABI.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
add_compile_definitions(XFILES_BUILD_VERSION="${PROJECT_VERSION}")
add_compile_options(/W4 /WX /utf-8)

add_library(quicktime SHARED
    src/diagnostics/report_dialog.cpp
    src/diagnostics/report.cpp
    src/diagnostics/log_file.cpp
    src/dllmain.cpp
    src/preferences/hooks.cpp
    src/preferences/registry.cpp
    src/preferences/store.cpp
    src/dispatch.cpp
    src/runtime.cpp
    src/settings.cpp
    src/enhancements/ui/settings_dialog.cpp
    src/enhancements/ui/tools_dialog.cpp
    src/enhancements/ui/menu_link.cpp
    src/enhancements/ui/notification.cpp
    src/enhancements/controls.cpp
    src/enhancements/controller.cpp
    src/enhancements/dialogue.cpp
    src/enhancements/inventory.cpp
    src/enhancements/menu.cpp
    src/enhancements/quick_save.cpp
    src/enhancements/modal_input.cpp
    src/enhancements/game_targets.cpp
    src/enhancements/focus.cpp
    src/enhancements/screens.cpp
    src/enhancements/login.cpp
    src/enhancements/ui/highlight.cpp
    src/enhancements/text_entry.cpp
    src/enhancements/ui/keyboard_view.cpp
    src/identity.cpp
    src/enhancements/ui/settings.rc
    src/platform/imports.cpp
    src/media/files.cpp
    src/memory.cpp
    src/graphics.cpp
    src/quickdraw/world.cpp
    src/quickdraw/drawing.cpp
    src/quickdraw/regions.cpp
    src/quickdraw/procedures.cpp
    src/quickdraw/compressed.cpp
    src/quickdraw/image_codec.cpp
    src/quickdraw/recording.cpp
    src/quickdraw/picture.cpp
    src/quickdraw/blit.cpp
    src/picture/pict.cpp
    src/playback/files.cpp
    src/playback/movie.cpp
    src/playback/timeline.cpp
    src/playback/audio.cpp
    src/playback/output.cpp
    src/playback/callbacks.cpp
    src/playback/components.cpp
    src/playback/tracks.cpp
    src/playback/video.cpp
    src/playback/captions.cpp
    src/log.cpp
    src/quicktime.def
)
set_target_properties(quicktime PROPERTIES OUTPUT_NAME QuickTime SUFFIX .qts)
target_include_directories(quicktime PRIVATE src src/enhancements/ui)
target_link_libraries(quicktime PRIVATE advapi32 bcrypt comctl32 comdlg32 dxguid gdi32 msimg32 shell32 user32 winmm xaudio2 xinput)

add_library(launcher-common STATIC
    src/diagnostics/report.cpp
    src/diagnostics/report_dialog.cpp
    src/diagnostics/log_file.cpp
    src/launcher/application.cpp
    src/launcher/configuration.cpp
    src/identity.cpp
    src/launcher/staging.cpp
    src/launcher/process.cpp
    src/launcher/preferences.cpp
    src/launcher/session.cpp
    src/preferences/store.cpp
    src/platform/desktop.cpp
)
target_include_directories(launcher-common PUBLIC src "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(launcher-common PUBLIC advapi32 bcrypt user32 comdlg32)

add_executable(xfiles-patch WIN32 src/launcher/main.cpp)
set_target_properties(xfiles-patch PROPERTIES OUTPUT_NAME XFilesPlay)
target_link_libraries(xfiles-patch PRIVATE launcher-common)
configure_file(cmake/media-catalog.rc.in "${CMAKE_BINARY_DIR}/generated/media-catalog.rc" @ONLY)
add_executable(xfiles-setup WIN32 src/setup/main.cpp src/setup/dialog.cpp
    src/setup/dialog.rc src/setup/shortcuts.cpp src/setup/media.cpp src/setup/destination.cpp src/setup/iso.cpp src/setup/catalog.cpp
    "${CMAKE_BINARY_DIR}/generated/media-catalog.rc")
set_target_properties(xfiles-setup PROPERTIES OUTPUT_NAME XFilesSetup)
target_link_libraries(xfiles-setup PRIVATE launcher-common ole32 uuid comctl32 shell32)
add_custom_command(TARGET xfiles-patch POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/patch.ini" "$<TARGET_FILE_DIR:xfiles-patch>/patch.ini")
configure_file(cmake/version.rc.in "${CMAKE_BINARY_DIR}/generated/version.rc" @ONLY)
foreach(target quicktime xfiles-patch xfiles-setup)
    target_sources(${target} PRIVATE "${CMAKE_BINARY_DIR}/generated/version.rc")
endforeach()
include(cmake/ddraw.cmake)

include(cmake/ffmpeg.cmake)
include(cmake/zlib.cmake)
include(cmake/package.cmake)
add_library(movie STATIC
    src/media/movie.cpp
    src/media/ima4.cpp
    src/media/compressed_audio.cpp
    src/media/video.cpp)
target_link_libraries(movie PUBLIC ole32 windowscodecs movie-zlib
    FFmpeg::avcodec FFmpeg::avutil FFmpeg::swresample FFmpeg::swscale)
add_dependencies(movie codec-files)
target_link_libraries(quicktime PRIVATE movie)
target_include_directories(movie PUBLIC src/media)
option(XFILES_BUILD_TOOLS "Build developer diagnostics (not included in releases)" OFF)
if(XFILES_BUILD_TOOLS)
    add_executable(xfiles-checkpoint tools/checkpoint/main.cpp src/preferences/store.cpp)
    target_include_directories(xfiles-checkpoint PRIVATE src)
    add_executable(xfiles-media src/media/inspect.cpp)
    target_link_libraries(xfiles-media PRIVATE movie)
    add_executable(xfiles-probe tools/probe/main.cpp)
    target_link_libraries(xfiles-probe PRIVATE launcher-common)
    add_dependencies(xfiles-probe xfiles-patch)
endif()

include(cmake/tests.cmake)

include(cmake/quality.cmake)
