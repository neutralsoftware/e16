cmake_minimum_required(VERSION 3.10)

project(E16Assembler LANGUAGES CXX)

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

add_executable(e16asm ${SOURCES})

target_include_directories(e16asm
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
)

if(MSVC)
    target_compile_options(e16asm PRIVATE /W4)
else()
    target_compile_options(e16asm PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
