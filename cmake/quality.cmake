option(XFILES_CODE_ANALYSIS "Run MSVC static analysis on patch sources" OFF)
if(XFILES_CODE_ANALYSIS)
    foreach(target quicktime controller-input launcher-common movie dvd-player dvd-mpeg xfiles-patch xfiles-setup database-browser-ui xfiles-devtools save-recipes checkpoint-tools)
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
    list(LENGTH format_sources format_count)
    math(EXPR format_last "${format_count} - 1")
    set(format_commands)
    set(format_check_commands)
    foreach(offset RANGE 0 ${format_last} 75)
        list(SUBLIST format_sources ${offset} 75 format_batch)
        list(APPEND format_commands COMMAND "${CLANG_FORMAT}" -i ${format_batch})
        list(APPEND format_check_commands COMMAND "${CLANG_FORMAT}" --dry-run --Werror ${format_batch})
    endforeach()
    add_custom_target(format ${format_commands}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        VERBATIM)
    add_custom_target(format-check ${format_check_commands}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        VERBATIM)
endif()
