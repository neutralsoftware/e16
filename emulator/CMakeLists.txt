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

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../assembler"
                 "${CMAKE_BINARY_DIR}/assembler")

set(BIOS_BINARY "${CMAKE_CURRENT_BINARY_DIR}/generated/bios.bin")
set(BIOS_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/bios_data.h")
file(GLOB_RECURSE BIOS_DEPENDENCIES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/../bios/*.e16"
        "${CMAKE_CURRENT_SOURCE_DIR}/../bios/*.e16spr"
)

add_custom_command(
        OUTPUT "${BIOS_BINARY}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND $<TARGET_FILE:e16asm> --base 0xF00000
                -o "${BIOS_BINARY}" "${CMAKE_CURRENT_SOURCE_DIR}/../bios/main.e16"
        DEPENDS e16asm ${BIOS_DEPENDENCIES}
        VERBATIM
)

add_custom_command(
        OUTPUT "${BIOS_HEADER}"
        COMMAND "${CMAKE_COMMAND}"
                "-DINPUT=${BIOS_BINARY}"
                "-DOUTPUT=${BIOS_HEADER}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake"
        DEPENDS "${BIOS_BINARY}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake"
        VERBATIM
)

add_custom_target(e16bios DEPENDS "${BIOS_HEADER}")

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(e16emu ${SOURCES})
add_dependencies(e16emu e16bios)
target_include_directories(e16emu PUBLIC include "${CMAKE_CURRENT_BINARY_DIR}/generated")

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
