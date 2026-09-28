cmake_minimum_required(VERSION 3.20)
project(xfiles_parser_fuzz LANGUAGES CXX)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Parser fuzzing requires Clang and libFuzzer")
endif()
find_package(ZLIB REQUIRED)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../src")

function(parser_fuzzer name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE "${SOURCE_ROOT}")
    target_compile_options(${name} PRIVATE -Wall -Wextra
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
endfunction()

parser_fuzzer(movie movie.cpp "${SOURCE_ROOT}/media/movie.cpp"
    "${SOURCE_ROOT}/media/ima4.cpp")
target_link_libraries(movie PRIVATE ZLIB::ZLIB)
parser_fuzzer(picture picture.cpp "${SOURCE_ROOT}/picture/pict.cpp")
parser_fuzzer(save save.cpp)
