find_package(Git REQUIRED)

foreach(current_patch IN ITEMS "${PATCH_FILE}" "${LOG_PATCH_FILE}")
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
