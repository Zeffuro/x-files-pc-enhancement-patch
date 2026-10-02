cmake_minimum_required(VERSION 3.24)
set(XFILES_VERSION "0.3.0" CACHE STRING "Release version (major.minor.patch)")
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

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
add_library(movie-grading STATIC src/playback/grading.cpp)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/generated/grading_catalog_generated.h"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/grading-catalog.py"
        "${CMAKE_SOURCE_DIR}/data/movie-grades.tsv" "${CMAKE_BINARY_DIR}/generated/grading_catalog_generated.h"
    DEPENDS tools/grading-catalog.py data/movie-grades.tsv
    VERBATIM)
target_sources(movie-grading PRIVATE "${CMAKE_BINARY_DIR}/generated/grading_catalog_generated.h")
target_include_directories(movie-grading PUBLIC src PRIVATE "${CMAKE_BINARY_DIR}/generated")
add_custom_target(native-profile-check
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/src/game/scripts/generate_profiles.py" --check
    DEPENDS src/game/scripts/generate_profiles.py src/game/profiles/builds.json src/game/profiles/generated.h
    VERBATIM)

add_library(quicktime SHARED
    src/diagnostics/game_context.cpp
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
    src/localization/ui.cpp
    src/devtools/inspector.cpp
    src/devtools/inspector_window.cpp
    src/devtools/inspector_preview_details.cpp
    src/devtools/database/native.cpp
    src/devtools/database/types.cpp
    src/devtools/database/trigger.cpp
    src/devtools/library_list.cpp
    src/devtools/artwork.cpp
    src/devtools/preview.cpp
    src/devtools/preview_paint.cpp
    src/devtools/caption_index.cpp
    src/devtools/subtitle_editor.cpp
    src/devtools/dialogs.rc
    src/devtools/game_state.cpp
    src/devtools/story_state.cpp
    src/devtools/state_tree.cpp
    src/devtools/variables.cpp
    src/devtools/hotspots.cpp
    src/devtools/clip_catalog.cpp
    src/devtools/clip_defaults.cpp
    src/enhancements/ui/settings_dialog.cpp
    src/enhancements/ui/settings_tabs.cpp
    src/enhancements/ui/controller_dialog.cpp
    src/enhancements/ui/controller_dialog.rc
    src/enhancements/ui/quick_menu_dialog.cpp
    src/enhancements/ui/movie_preview.cpp
    src/enhancements/ui/tools_dialog.cpp
    src/enhancements/ui/subtitle_dialog.cpp
    src/enhancements/ui/menu_link.cpp
    src/enhancements/ui/notification.cpp
    src/enhancements/controls.cpp
    src/enhancements/scene_overlay.cpp
    src/enhancements/ui/quick_menu.cpp
    src/enhancements/ui/quick_menu_surface.cpp
    src/enhancements/controller.cpp
    src/enhancements/dialogue.cpp
    src/transcript/capture.cpp
    src/transcript/page.cpp
    src/transcript/view.cpp
    src/enhancements/game_strings.cpp
    src/enhancements/inventory.cpp
    src/enhancements/menu_corner.cpp
    src/enhancements/gun.cpp
    src/enhancements/menu.cpp
    src/enhancements/script_controls.cpp
    src/enhancements/quick_save.cpp
    src/enhancements/modal_input.cpp
    src/enhancements/game_targets.cpp
    src/enhancements/focus.cpp
    src/enhancements/screens.cpp
    src/enhancements/login.cpp
    src/enhancements/ui/highlight.cpp
    src/enhancements/ui/controller_hints.cpp
    src/enhancements/text_entry.cpp
    src/enhancements/ui/keyboard_view.cpp
    src/identity.cpp
    src/enhancements/ui/settings.rc
    src/platform/imports.cpp
    src/media/files.cpp
    src/saves/paths.cpp
    src/saves/dialogs.cpp
    src/saves/catalog.cpp
    src/saves/slots.cpp
    src/saves/artwork.cpp
    src/saves/browser.cpp
    src/saves/browser_paint.cpp
    src/saves/browser_navigation.cpp
    src/saves/preview.cpp
    src/saves/browser_text.cpp
    src/game/render/native_render.cpp
    src/game/render/gdi_hooks.cpp
    src/game/render/caption_surface.cpp
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
configure_file(cmake/clip-labels.rc.in "${CMAKE_BINARY_DIR}/generated/clip-labels.rc" @ONLY)
target_sources(quicktime PRIVATE "${CMAKE_BINARY_DIR}/generated/clip-labels.rc")
set_target_properties(quicktime PROPERTIES OUTPUT_NAME QuickTime SUFFIX .qts)
add_library(controller-input STATIC
    src/enhancements/controller_state.cpp
    src/enhancements/controller_navigation.cpp
    src/enhancements/rumble_state.cpp
    src/enhancements/rumble_catalog.cpp
    src/enhancements/rumble.cpp)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/generated/rumble_catalog_generated.h"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/rumble-catalog.py"
        "${CMAKE_SOURCE_DIR}/data" "${CMAKE_BINARY_DIR}/generated/rumble_catalog_generated.h"
    DEPENDS tools/rumble-catalog.py data/rumble-effects.tsv data/rumble-clips.tsv data/rumble-events.tsv
    VERBATIM)
target_sources(controller-input PRIVATE "${CMAKE_BINARY_DIR}/generated/rumble_catalog_generated.h")
target_include_directories(controller-input PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_include_directories(controller-input PUBLIC src)
target_link_libraries(controller-input PUBLIC user32 xinput)
add_library(clip-index STATIC src/game/assets/clip_index.cpp)
target_include_directories(clip-index PUBLIC src)
add_library(database-browser-ui STATIC
    src/devtools/database/browser.cpp src/devtools/database/browser_controls.cpp
    src/devtools/database/browser_details.cpp src/devtools/database/browser_rows.cpp
    src/devtools/database/browser_navigation.cpp src/devtools/database/assets.cpp
    src/devtools/database/browser_asset_content.cpp src/devtools/database/source.cpp
    src/devtools/database/browser_tabs.cpp src/devtools/database/font_preview.cpp
    src/devtools/database/browser_strings.cpp src/game/assets/resource_strings.cpp
    src/devtools/database/browser_text.cpp
    src/devtools/database/browser_tables.cpp src/devtools/database/browser_database.cpp
    src/devtools/database/field_guide.cpp
    src/devtools/database/browser_accessibility.cpp
    src/devtools/database/browser_accessibility_list.cpp
    src/devtools/database/browser_stored_list.cpp src/game/database/stored_list.cpp
    src/devtools/database/browser_stored_action.cpp src/game/database/stored_action.cpp
    src/devtools/database/browser_stored_asset_list.cpp src/game/database/stored_asset_list.cpp
    src/devtools/database/browser_stored_asset_ref.cpp src/game/database/stored_asset_ref.cpp
    src/devtools/database/browser_stored_fields.cpp src/game/database/stored_fields.cpp
    src/devtools/database/payload.cpp
    src/game/assets/xt_text.cpp
    src/devtools/database/model.cpp src/devtools/clip_key.cpp
    src/game/database/database.cpp src/game/database/io.cpp src/game/database/offline_index.cpp
    src/game/database/stored_variable.cpp
    src/game/database/stored_trigger.cpp
    src/game/assets/hotspots.cpp src/game/assets/pff.cpp
    src/devtools/database/pff_browser.cpp src/devtools/database/pff_browser_files.cpp
    src/devtools/database/pff_image.cpp src/devtools/database/asset_io.cpp src/picture/pict.cpp)
target_include_directories(database-browser-ui PUBLIC src)
target_link_libraries(database-browser-ui PUBLIC clip-index comctl32 comdlg32 user32 gdi32
    uiautomationcore oleaut32 uuid)
add_library(clip-notes STATIC src/devtools/clip_notes.cpp)
target_include_directories(clip-notes PUBLIC src)
target_link_libraries(quicktime PRIVATE database-browser-ui clip-notes controller-input movie-grading uxtheme)
target_include_directories(quicktime PRIVATE src src/enhancements/ui)
target_link_libraries(quicktime PRIVATE advapi32 bcrypt comctl32 comdlg32 dxguid gdi32 msimg32 shell32 user32 uuid winmm xaudio2 xinput)

add_library(launcher-common STATIC
    src/localization/ui.cpp
    src/diagnostics/report.cpp
    src/diagnostics/report_dialog.cpp
    src/diagnostics/log_file.cpp
    src/launcher/application.cpp
    src/launcher/welcome.cpp
    src/saves/storage.cpp
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
add_dependencies(quicktime native-profile-check)
add_dependencies(launcher-common native-profile-check)

add_executable(xfiles-patch WIN32 src/launcher/main.cpp src/launcher/welcome.rc)
set_target_properties(xfiles-patch PROPERTIES OUTPUT_NAME XFilesPlay)
target_link_libraries(xfiles-patch PRIVATE launcher-common)
configure_file(cmake/media-catalog.rc.in "${CMAKE_BINARY_DIR}/generated/media-catalog.rc" @ONLY)
add_executable(xfiles-setup WIN32 src/setup/main.cpp src/setup/dialog.cpp
    src/setup/dialog.rc src/setup/shortcuts.cpp src/setup/media.cpp src/setup/destination.cpp src/setup/iso.cpp src/setup/disc_image.cpp src/setup/catalog.cpp
    "${CMAKE_BINARY_DIR}/generated/media-catalog.rc")
set_target_properties(xfiles-setup PROPERTIES OUTPUT_NAME XFilesSetup)
target_link_libraries(xfiles-setup PRIVATE launcher-common ole32 uuid comctl32 shell32)
add_custom_command(TARGET xfiles-patch POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/patch.ini" "$<TARGET_FILE_DIR:xfiles-patch>/patch.ini")
configure_file(cmake/version.rc.in "${CMAKE_BINARY_DIR}/generated/version.rc" @ONLY)
configure_file(cmake/icon.rc.in "${CMAKE_BINARY_DIR}/generated/icon.rc" @ONLY)
foreach(target quicktime xfiles-patch xfiles-setup)
    target_sources(${target} PRIVATE "${CMAKE_BINARY_DIR}/generated/icon.rc")
endforeach()
foreach(target quicktime xfiles-patch xfiles-setup)
    target_sources(${target} PRIVATE "${CMAKE_BINARY_DIR}/generated/version.rc")
endforeach()
include(cmake/ddraw.cmake)

include(cmake/ffmpeg.cmake)
include(cmake/zlib.cmake)
include(cmake/package.cmake)
add_library(movie STATIC
    src/media/mpeg.cpp
    src/media/mpeg_filter.cpp
    src/media/movie.cpp
    src/media/subtitles.cpp
    src/media/subtitle_archive.cpp
    src/media/ima4.cpp
    src/media/compressed_audio.cpp
    src/media/video.cpp)
target_link_libraries(movie PUBLIC bcrypt ole32 windowscodecs movie-zlib movie-minizip
    FFmpeg::avformat FFmpeg::avcodec FFmpeg::avfilter FFmpeg::avutil FFmpeg::swresample FFmpeg::swscale)
add_dependencies(movie codec-files)
target_link_libraries(quicktime PRIVATE movie)
target_link_libraries(database-browser-ui PUBLIC movie ole32 windowscodecs)
target_include_directories(movie PUBLIC src/media)
add_executable(xfiles-devtools WIN32 src/devtools/database/standalone.cpp
    src/devtools/database/standalone_preview.cpp
    src/devtools/preview.cpp src/playback/audio.cpp src/playback/output.cpp
    "${CMAKE_BINARY_DIR}/generated/version.rc" "${CMAKE_BINARY_DIR}/generated/icon.rc")
target_link_libraries(xfiles-devtools PRIVATE database-browser-ui movie
    comdlg32 shell32 xaudio2 uuid ole32)
add_library(dvd-player STATIC src/dvd/player.cpp src/dvd/files.cpp src/dvd/captions.cpp
    src/identity.cpp src/platform/imports.cpp src/diagnostics/log_file.cpp)
target_include_directories(dvd-player PUBLIC src)
target_link_libraries(dvd-player PUBLIC movie)
add_library(dvd-mpeg SHARED src/dvd/adapter.cpp src/dvd/output.cpp src/dvd/input.cpp src/settings.cpp src/dvd/adapter.def)
target_link_libraries(dvd-mpeg PRIVATE dvd-player user32 comctl32 gdi32 msimg32 ole32 xaudio2 xinput)
set_target_properties(dvd-mpeg PROPERTIES OUTPUT_NAME XFilesMpeg)
add_dependencies(xfiles-patch dvd-mpeg)
option(XFILES_BUILD_TOOLS "Build developer diagnostics (not included in releases)" OFF)
if(XFILES_BUILD_TOOLS)
    add_executable(xfiles-checkpoint tools/checkpoint/main.cpp src/preferences/store.cpp src/saves/storage.cpp src/saves/catalog.cpp)
    target_include_directories(xfiles-checkpoint PRIVATE src)
    add_executable(xfiles-media src/media/inspect.cpp)
    target_link_libraries(xfiles-media PRIVATE movie)
    add_executable(xfiles-probe tools/probe/main.cpp)
    target_link_libraries(xfiles-probe PRIVATE launcher-common)
    add_dependencies(xfiles-probe xfiles-patch)
endif()

include(cmake/tests.cmake)

include(cmake/quality.cmake)
