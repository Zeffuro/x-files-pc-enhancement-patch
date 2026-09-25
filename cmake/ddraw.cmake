include(ExternalProject)

set(ddraw_source "${CMAKE_BINARY_DIR}/_deps/cnc-ddraw-src")
set(ddraw_package "${CMAKE_BINARY_DIR}/third-party")
if(CMAKE_VS_MSBUILD_COMMAND)
    set(msbuild "${CMAKE_VS_MSBUILD_COMMAND}")
else()
    find_program(msbuild MSBuild REQUIRED)
endif()

ExternalProject_Add(cnc-ddraw
    GIT_REPOSITORY https://github.com/FunkyFr3sh/cnc-ddraw.git
    GIT_TAG 541b5de218ec3fbd6ea91e606ebfadc07c1786b0
    GIT_SUBMODULES ""
    SOURCE_DIR "${ddraw_source}"
    UPDATE_COMMAND ""
    PATCH_COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=<SOURCE_DIR>"
        "-DPATCH_FILE=${CMAKE_SOURCE_DIR}/patches/cnc-ddraw-desktop.patch"
        -P "${CMAKE_SOURCE_DIR}/cmake/patch-ddraw.cmake"
    CONFIGURE_COMMAND ""
    BUILD_IN_SOURCE TRUE
    BUILD_ALWAYS TRUE
    BUILD_COMMAND "${msbuild}" <SOURCE_DIR>/cnc-ddraw.vcxproj
        /m /v:minimal /p:Configuration=Release /p:Platform=Win32
        /p:SolutionDir=<SOURCE_DIR>/
    BUILD_BYPRODUCTS "${ddraw_source}/bin/Release/ddraw.dll"
    INSTALL_COMMAND ""
)

add_custom_target(display-files ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${ddraw_source}"
        "-DPROJECT_DIR=${CMAKE_SOURCE_DIR}"
        "-DOUTPUT_DIR=${ddraw_package}"
        "-DHEADER_FILE=${CMAKE_BINARY_DIR}/generated/ddraw_version.h"
        -P "${CMAKE_SOURCE_DIR}/cmake/package-ddraw.cmake"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:xfiles-patch>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${ddraw_package}/ddraw.dll"
        "${ddraw_package}/cnc-ddraw.LICENSE"
        "${CMAKE_SOURCE_DIR}/config/ddraw.ini"
        "$<TARGET_FILE_DIR:xfiles-patch>"
    DEPENDS cnc-ddraw
    BYPRODUCTS "${CMAKE_BINARY_DIR}/generated/ddraw_version.h"
    VERBATIM
)

add_dependencies(xfiles-patch display-files quicktime)

add_dependencies(launcher-common display-files)
add_dependencies(xfiles-setup xfiles-patch)
