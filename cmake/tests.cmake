include(CTest)
if(BUILD_TESTING)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_test(NAME release-package-checker COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/package_check.py")
    add_executable(installation-test tests/installation.cpp src/launcher/configuration.cpp
        src/setup/destination.cpp)
    target_include_directories(installation-test PRIVATE src)
    add_test(NAME installation-paths-and-settings COMMAND installation-test)
    add_executable(iso-files-test tests/iso_files.cpp src/setup/iso.cpp src/setup/catalog.cpp
        "${CMAKE_BINARY_DIR}/generated/media-catalog.rc")
    target_link_libraries(iso-files-test PRIVATE launcher-common)
    add_test(NAME dvd-cd-iso-files COMMAND iso-files-test)
    add_executable(audio-edits-test tests/audio_edits.cpp)
    target_include_directories(audio-edits-test PRIVATE src)
    add_test(NAME audio-edit-rates COMMAND audio-edits-test)
    add_executable(report-test tests/report.cpp src/diagnostics/report.cpp)
    target_include_directories(report-test PRIVATE src)
    add_test(NAME troubleshooting-report COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/report_zip.py" $<TARGET_FILE:report-test>)
    add_executable(log-file-test tests/log_file.cpp src/diagnostics/log_file.cpp)
    target_include_directories(log-file-test PRIVATE src)
    add_test(NAME bounded-logs COMMAND log-file-test)
    add_executable(image-codec-test tests/image_codec.cpp src/picture/pict.cpp)
    target_include_directories(image-codec-test PRIVATE src)
    add_test(NAME photo-save-codec COMMAND image-codec-test $<TARGET_FILE:quicktime>)

    add_executable(focus-test tests/focus.cpp src/enhancements/focus.cpp)
    target_include_directories(focus-test PRIVATE src)
    target_link_libraries(focus-test PRIVATE user32)
    add_test(NAME controller-focus COMMAND focus-test)

    add_executable(navigation-test tests/navigation.cpp src/enhancements/inventory.cpp)
    target_include_directories(navigation-test PRIVATE src)
    target_link_libraries(navigation-test PRIVATE user32)
    add_test(NAME inventory-navigation COMMAND navigation-test)

    add_executable(session-test tests/session.cpp src/launcher/session.cpp
        src/launcher/preferences.cpp src/preferences/store.cpp)
    target_include_directories(session-test PRIVATE src)
    target_link_libraries(session-test PRIVATE advapi32)
    add_test(NAME launcher-session COMMAND session-test)

    add_executable(abi-test tests/abi.cpp)
    add_test(NAME dispatcher-abi COMMAND abi-test $<TARGET_FILE:quicktime>)

    add_executable(movie-test tests/movie.cpp)
    target_link_libraries(movie-test PRIVATE movie)
    add_test(NAME movie-reader COMMAND movie-test)

    add_executable(audio-test tests/audio.cpp)
    target_link_libraries(audio-test PRIVATE movie)
    add_test(NAME ima4-audio COMMAND audio-test)

    add_executable(compressed-audio-test tests/compressed_audio.cpp)
    target_link_libraries(compressed-audio-test PRIVATE movie)
    add_test(NAME compressed-audio COMMAND compressed-audio-test)

    add_executable(playback-test tests/playback.cpp src/settings.cpp)
    target_include_directories(playback-test PRIVATE src)
    add_test(NAME movie-playback COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/run_logged.py"
        "${CMAKE_BINARY_DIR}/test-logs/movie-playback.log" $<TARGET_FILE:playback-test> $<TARGET_FILE:quicktime> --no-audio)
    add_test(NAME movie-playback-audio COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/run_logged.py"
        "${CMAKE_BINARY_DIR}/test-logs/movie-playback-audio.log" $<TARGET_FILE:playback-test> $<TARGET_FILE:quicktime>)
    set_tests_properties(movie-playback-audio PROPERTIES LABELS audio-hardware)

    add_executable(video-test tests/video.cpp)
    target_link_libraries(video-test PRIVATE movie)
    add_test(NAME video-codecs COMMAND video-test)

    add_executable(media-files-test tests/media_files.cpp)
    target_include_directories(media-files-test PRIVATE src)
    add_test(NAME local-media COMMAND media-files-test $<TARGET_FILE:quicktime>)

    add_executable(world-test tests/world.cpp)
    target_include_directories(world-test PRIVATE src)
    target_link_libraries(world-test PRIVATE gdi32 user32)
    add_test(NAME quickdraw-world COMMAND world-test $<TARGET_FILE:quicktime>)

    add_executable(picture-test tests/picture.cpp tests/picture_indexed.cpp src/picture/pict.cpp)
    target_include_directories(picture-test PRIVATE src)
    add_test(NAME quickdraw-picture COMMAND picture-test $<TARGET_FILE:quicktime>)

    add_executable(region-test tests/regions.cpp)
    target_include_directories(region-test PRIVATE src)
    target_link_libraries(region-test PRIVATE gdi32 user32)
    add_test(NAME quickdraw-regions COMMAND region-test $<TARGET_FILE:quicktime>)

    add_executable(memory-test tests/memory.cpp)
    target_include_directories(memory-test PRIVATE src)
    add_test(NAME memory-handles COMMAND memory-test $<TARGET_FILE:quicktime>)

    add_executable(preferences-test tests/preferences.cpp src/preferences/store.cpp)
    target_include_directories(preferences-test PRIVATE src)
    target_link_libraries(preferences-test PRIVATE advapi32)
    add_test(NAME portable-preferences COMMAND preferences-test $<TARGET_FILE:quicktime>)

    add_executable(display-test
        src/diagnostics/log_file.cpp
        tests/display.cpp
        src/graphics.cpp
        src/log.cpp
        src/identity.cpp
        src/platform/desktop.cpp
    )
    target_include_directories(display-test PRIVATE src "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(display-test PRIVATE bcrypt dxguid user32 gdi32)
    add_dependencies(display-test display-files)
    add_test(NAME display-compatibility
        COMMAND display-test "$<TARGET_FILE_DIR:xfiles-patch>/ddraw.dll")
    set_tests_properties(display-compatibility PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
endif()

if(BUILD_TESTING)
    add_executable(setup-test tests/setup.cpp tests/setup_iso.cpp src/setup/shortcuts.cpp src/setup/media.cpp src/setup/destination.cpp src/setup/iso.cpp src/setup/catalog.cpp
        "${CMAKE_BINARY_DIR}/generated/media-catalog.rc")
    target_link_libraries(setup-test PRIVATE launcher-common ole32 uuid shell32)
    add_dependencies(setup-test xfiles-patch)
    add_test(NAME setup-validation COMMAND setup-test)
endif()
