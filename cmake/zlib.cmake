include(FetchContent)
FetchContent_Declare(zlib
    URL https://zlib.net/fossils/zlib-1.3.1.tar.gz
    URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(zlib)

add_library(movie-zlib STATIC
    ${zlib_SOURCE_DIR}/adler32.c
    ${zlib_SOURCE_DIR}/crc32.c
    ${zlib_SOURCE_DIR}/compress.c
    ${zlib_SOURCE_DIR}/deflate.c
    ${zlib_SOURCE_DIR}/trees.c
    ${zlib_SOURCE_DIR}/inffast.c
    ${zlib_SOURCE_DIR}/inflate.c
    ${zlib_SOURCE_DIR}/inftrees.c
    ${zlib_SOURCE_DIR}/uncompr.c
    ${zlib_SOURCE_DIR}/zutil.c)
target_include_directories(movie-zlib SYSTEM PUBLIC ${zlib_SOURCE_DIR})
set_property(TARGET movie-zlib PROPERTY COMPILE_OPTIONS /W0 /utf-8)
add_library(movie-minizip STATIC
    ${zlib_SOURCE_DIR}/contrib/minizip/unzip.c
    ${zlib_SOURCE_DIR}/contrib/minizip/zip.c
    ${zlib_SOURCE_DIR}/contrib/minizip/ioapi.c
    ${zlib_SOURCE_DIR}/contrib/minizip/iowin32.c)
target_include_directories(movie-minizip SYSTEM PUBLIC ${zlib_SOURCE_DIR}/contrib/minizip)
target_link_libraries(movie-minizip PUBLIC movie-zlib)
target_compile_definitions(movie-minizip PRIVATE NOCRYPT NOUNCRYPT)
set_property(TARGET movie-minizip PROPERTY COMPILE_OPTIONS /W0 /utf-8)
file(READ "${zlib_SOURCE_DIR}/LICENSE" zlib_notice)
file(READ "${zlib_SOURCE_DIR}/contrib/minizip/MiniZip64_info.txt" minizip_notice)
file(WRITE "${CMAKE_BINARY_DIR}/generated/zlib.LICENSE" "${zlib_notice}\n${minizip_notice}")
install(FILES "${CMAKE_BINARY_DIR}/generated/zlib.LICENSE" DESTINATION .)
