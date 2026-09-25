set(library "${SOURCE_DIR}/bin/Release/ddraw.dll")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(COPY_FILE "${library}" "${OUTPUT_DIR}/ddraw.dll" ONLY_IF_DIFFERENT)

file(READ "${SOURCE_DIR}/LICENSE" CNCDDRAW_LICENSE)
file(READ "${SOURCE_DIR}/src/detours/LICENSE.md" DETOURS_LICENSE)
file(READ "${SOURCE_DIR}/src/lodepng.c" lodepng)
string(FIND "${lodepng}" "*/" notice_end)
if(notice_end LESS 0)
    message(FATAL_ERROR "LodePNG's license header is missing.")
endif()
math(EXPR notice_length "${notice_end} - 2")
string(SUBSTRING "${lodepng}" 2 ${notice_length} LODEPNG_LICENSE)
string(STRIP "${LODEPNG_LICENSE}" LODEPNG_LICENSE)

configure_file("${PROJECT_DIR}/cmake/ddraw-notices.txt.in"
               "${OUTPUT_DIR}/cnc-ddraw.LICENSE" @ONLY)

file(SHA256 "${library}" DDRAW_SHA256)
file(SHA256 "${PROJECT_DIR}/config/ddraw.ini" DDRAW_CONFIG_SHA256)
configure_file("${PROJECT_DIR}/cmake/ddraw_version.h.in" "${HEADER_FILE}" @ONLY)
