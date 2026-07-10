cmake_minimum_required(VERSION 3.10)

if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    execute_process(
            COMMAND /usr/bin/xcrun --find clang++
            OUTPUT_VARIABLE XCODE_CLANGXX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY
    )
    set(CMAKE_CXX_COMPILER "${XCODE_CLANGXX}" CACHE FILEPATH "" FORCE)
endif ()

project(E16Emulator LANGUAGES CXX)

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

add_executable(e16emu ${SOURCES})
target_include_directories(e16emu PUBLIC include)

find_package(SDL3 QUIET)
if (SDL3_FOUND)
    if (TARGET SDL3::SDL3)
        target_link_libraries(e16emu PRIVATE SDL3::SDL3)
    else ()
        target_include_directories(e16emu PRIVATE ${SDL3_INCLUDE_DIRS})
        target_link_libraries(e16emu PRIVATE ${SDL3_LIBRARIES})
    endif ()
else ()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
    target_link_libraries(e16emu PRIVATE PkgConfig::SDL3)
endif ()

if (MSVC)
    target_compile_options(e16emu PRIVATE /W4)
else ()
    target_compile_options(e16emu PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif ()
