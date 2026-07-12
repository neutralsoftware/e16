cmake_minimum_required(VERSION 3.10)

project(E16MusicMaker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)

add_executable(e16musicmaker ${SOURCES})
target_include_directories(e16musicmaker PRIVATE "${CMAKE_SOURCE_DIR}/include")

find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
target_link_libraries(e16musicmaker PRIVATE PkgConfig::SNDFILE)

find_package(SDL3 QUIET)
if(SDL3_FOUND)
    if(TARGET SDL3::SDL3)
        target_link_libraries(e16musicmaker PRIVATE SDL3::SDL3)
    else()
        target_include_directories(e16musicmaker PRIVATE ${SDL3_INCLUDE_DIRS})
        target_link_libraries(e16musicmaker PRIVATE ${SDL3_LIBRARIES})
    endif()
else()
    pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
    target_link_libraries(e16musicmaker PRIVATE PkgConfig::SDL3)
endif()

if(MSVC)
    target_compile_options(e16musicmaker PRIVATE /W4)
else()
    target_compile_options(e16musicmaker PRIVATE -Wall -Wextra -Wpedantic)
endif()
