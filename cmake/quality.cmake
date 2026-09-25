option(XFILES_CODE_ANALYSIS "Run MSVC static analysis on patch sources" OFF)
if(XFILES_CODE_ANALYSIS)
    foreach(target quicktime launcher-common movie xfiles-patch xfiles-setup)
        target_compile_options(${target} PRIVATE /analyze)
    endforeach()
    if(TARGET xfiles-checkpoint)
        target_compile_options(xfiles-checkpoint PRIVATE /analyze)
    endif()
endif()

find_program(CLANG_FORMAT NAMES clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE format_sources CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.h"
        "${PROJECT_SOURCE_DIR}/tests/*.cpp" "${PROJECT_SOURCE_DIR}/tests/*.h"
        "${PROJECT_SOURCE_DIR}/tools/*.cpp" "${PROJECT_SOURCE_DIR}/tools/*.h")
    add_custom_target(format
        COMMAND "${CLANG_FORMAT}" -i ${format_sources}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        VERBATIM)
    add_custom_target(format-check
        COMMAND "${CLANG_FORMAT}" --dry-run --Werror ${format_sources}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        VERBATIM)
endif()
