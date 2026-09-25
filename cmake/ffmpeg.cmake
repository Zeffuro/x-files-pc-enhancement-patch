set(FFMPEG_ROOT "" CACHE PATH "Prefix of the x86 shared FFmpeg build")
find_path(ffmpeg_include libavcodec/avcodec.h PATHS "${FFMPEG_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

foreach(component avcodec avutil swresample swscale)
    find_library(ffmpeg_${component}_library NAMES ${component}
        PATHS "${FFMPEG_ROOT}/lib" "${FFMPEG_ROOT}/bin" NO_DEFAULT_PATH REQUIRED)
    file(GLOB component_dll "${FFMPEG_ROOT}/bin/${component}-*.dll")
    list(LENGTH component_dll dll_count)
    if(NOT dll_count EQUAL 1)
        message(FATAL_ERROR "Expected one ${component} DLL in ${FFMPEG_ROOT}/bin")
    endif()
    add_library(FFmpeg::${component} SHARED IMPORTED)
    set_target_properties(FFmpeg::${component} PROPERTIES
        IMPORTED_IMPLIB "${ffmpeg_${component}_library}"
        IMPORTED_LOCATION "${component_dll}"
        INTERFACE_INCLUDE_DIRECTORIES "${ffmpeg_include}")
    list(APPEND ffmpeg_runtime "${component_dll}")
    get_filename_component(dll_name "${component_dll}" NAME)
    string(APPEND ffmpeg_names "    L\"${dll_name}\",\n")
endforeach()

file(WRITE "${CMAKE_BINARY_DIR}/generated/ffmpeg_files.h"
    "#pragma once\n\ninline constexpr const wchar_t* ffmpeg_files[] = {\n${ffmpeg_names}    L\"FFmpeg.LICENSE\",\n};\n")

add_custom_target(codec-files ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:quicktime>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        ${ffmpeg_runtime} "${FFMPEG_ROOT}/FFmpeg.LICENSE" "$<TARGET_FILE_DIR:quicktime>"
    VERBATIM)
add_dependencies(quicktime codec-files)
