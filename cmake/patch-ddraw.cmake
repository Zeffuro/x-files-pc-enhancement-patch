find_package(Git REQUIRED)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE already_patched
    OUTPUT_QUIET ERROR_QUIET
)

if(NOT already_patched EQUAL 0)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply "${PATCH_FILE}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()
