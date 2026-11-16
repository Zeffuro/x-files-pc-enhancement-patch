find_package(Git REQUIRED)

function(patch_state output)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE revision
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" diff --binary HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE changes
        COMMAND_ERROR_IS_FATAL ANY)
    set(state "${revision}${changes}")
    foreach(patch IN ITEMS "${PATCH_FILE}" "${LOG_PATCH_FILE}" "${DVD_PATCH_FILE}" "${MONITOR_PATCH_FILE}" "${MOUSE_PATCH_FILE}")
        file(SHA256 "${patch}" hash)
        string(APPEND state "${hash}")
    endforeach()
    string(SHA256 hash "${state}")
    set(${output} "${hash}" PARENT_SCOPE)
endfunction()

function(write_git_header)
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --match=NeVeRmAtCh --always --dirty
        WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE commit
        OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE branch
        OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    string(REPLACE "\\" "\\\\" branch "${branch}")
    string(REPLACE "\"" "\\\"" branch "${branch}")
    file(CONFIGURE OUTPUT "${SOURCE_DIR}/inc/git.h"
        CONTENT "#define GIT_COMMIT \"${commit}\"\n#define GIT_BRANCH \"${branch}\"\n" @ONLY)
endfunction()

execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --git-path xfiles-patches-state
    WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE state_file
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
get_filename_component(state_file "${state_file}" ABSOLUTE BASE_DIR "${SOURCE_DIR}")
patch_state(current_state)
if(EXISTS "${state_file}")
    file(READ "${state_file}" previous_state)
    if(previous_state STREQUAL current_state)
        write_git_header()
        return()
    endif()
endif()

# Remove the dependent patch before checking the base patches it extends.
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${MOUSE_PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE mouse_patched
    OUTPUT_QUIET ERROR_QUIET
)
if(mouse_patched EQUAL 0)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse "${MOUSE_PATCH_FILE}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()

foreach(current_patch IN ITEMS "${PATCH_FILE}" "${LOG_PATCH_FILE}" "${DVD_PATCH_FILE}" "${MONITOR_PATCH_FILE}" "${MOUSE_PATCH_FILE}")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${current_patch}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE already_patched
        OUTPUT_QUIET ERROR_QUIET
    )

    if(NOT already_patched EQUAL 0)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply "${current_patch}"
            WORKING_DIRECTORY "${SOURCE_DIR}"
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()
endforeach()

patch_state(current_state)
file(WRITE "${state_file}" "${current_state}")
write_git_header()
