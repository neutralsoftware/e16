cmake_minimum_required(VERSION 3.10)

project(E16ImageConverter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)

add_executable(e16img ${SOURCES})

find_library(ZLIB_LIBRARY z REQUIRED)
target_link_libraries(e16img PRIVATE "${ZLIB_LIBRARY}")

if(MSVC)
    target_compile_options(e16img PRIVATE /W4)
else()
    target_compile_options(e16img PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
