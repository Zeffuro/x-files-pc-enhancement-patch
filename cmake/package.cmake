install(FILES "$<TARGET_FILE:xfiles-patch>" "$<TARGET_FILE:xfiles-setup>"
    "$<TARGET_FILE:quicktime>" DESTINATION .)
install(FILES
    "${ddraw_package}/ddraw.dll"
    "${ddraw_package}/cnc-ddraw.LICENSE"
    "${CMAKE_SOURCE_DIR}/README.md"
    "${CMAKE_SOURCE_DIR}/LICENSE"
    "${CMAKE_SOURCE_DIR}/THIRD_PARTY.md"
    ${ffmpeg_runtime}
    "${FFMPEG_ROOT}/FFmpeg.LICENSE"
    DESTINATION .)

install(FILES "${CMAKE_SOURCE_DIR}/config/ddraw.ini" "${CMAKE_SOURCE_DIR}/config/patch.ini"
    DESTINATION defaults)

install(FILES "${CMAKE_SOURCE_DIR}/docs/controls.md" "${CMAKE_SOURCE_DIR}/docs/building.md"
    DESTINATION docs)

file(GLOB ffmpeg_source_archives "${FFMPEG_ROOT}/source/ffmpeg-*.tar.xz")
list(LENGTH ffmpeg_source_archives archive_count)
if(NOT archive_count EQUAL 1)
    message(FATAL_ERROR "Expected the matching FFmpeg source archive in ${FFMPEG_ROOT}/source")
endif()
install(FILES ${ffmpeg_source_archives} "${FFMPEG_ROOT}/source/build-ffmpeg.sh"
    DESTINATION source/ffmpeg)

add_custom_target(notice-files ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:quicktime>/defaults"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/ddraw.ini" "${CMAKE_SOURCE_DIR}/config/patch.ini"
        "$<TARGET_FILE_DIR:quicktime>/defaults"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:quicktime>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/THIRD_PARTY.md"
        "${CMAKE_SOURCE_DIR}/README.md"
        "$<TARGET_FILE_DIR:quicktime>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:quicktime>/docs"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/docs/controls.md" "${CMAKE_SOURCE_DIR}/docs/building.md"
        "$<TARGET_FILE_DIR:quicktime>/docs"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${zlib_SOURCE_DIR}/LICENSE"
        "$<TARGET_FILE_DIR:quicktime>/zlib.LICENSE"
    VERBATIM)
add_dependencies(launcher-common notice-files)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "xfiles-enhancement-${PROJECT_VERSION}-windows-x86")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)
