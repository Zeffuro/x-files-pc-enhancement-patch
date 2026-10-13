find_package(Git REQUIRED)

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
