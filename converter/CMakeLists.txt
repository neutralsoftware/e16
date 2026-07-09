cmake_minimum_required(VERSION 3.10)

project(E16ImageConverter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

add_executable(e16img src/main.cpp)
add_executable(e16spritepack src/e16spritepack.cpp)
add_executable(e16tilemap src/e16tilemap.cpp)

find_library(ZLIB_LIBRARY z REQUIRED)
target_link_libraries(e16img PRIVATE "${ZLIB_LIBRARY}")
target_link_libraries(e16spritepack PRIVATE "${ZLIB_LIBRARY}")
target_link_libraries(e16tilemap PRIVATE "${ZLIB_LIBRARY}")

set(CONVERTER_TARGETS e16img e16spritepack e16tilemap)

if(MSVC)
    foreach(target ${CONVERTER_TARGETS})
        target_compile_options(${target} PRIVATE /W4)
    endforeach()
else()
    foreach(target ${CONVERTER_TARGETS})
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endforeach()
endif()
