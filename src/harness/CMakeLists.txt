# Desktop / SDL2 host build of TempleShrine.
#
# Builds a native executable that runs all the shared game code against
# an SDL2 backend for display / input / audio. Same shim, same games as
# the badge — only the platform files under src/harness/ differ.
#
# Usage:
#   cd firmware/templeshrine/src/harness
#   cmake -B build
#   cmake --build build
#   ./build/templeshrine        (Linux/macOS)
#   build\Debug\templeshrine.exe   (Windows / MSVC default config)

cmake_minimum_required(VERSION 3.16)
project(templeshrine_sdl C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)

# Shared game code from src/*.c — excluding the ESP-only backend files.
file(GLOB SHARED_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../*.c)
list(FILTER SHARED_SOURCES EXCLUDE REGEX "/main\\.c$")
list(FILTER SHARED_SOURCES EXCLUDE REGEX "/display\\.c$")
list(FILTER SHARED_SOURCES EXCLUDE REGEX "/input\\.c$")
list(FILTER SHARED_SOURCES EXCLUDE REGEX "/audio\\.c$")

# SDL harness (this directory).
file(GLOB SDL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

add_executable(templeshrine ${SHARED_SOURCES} ${SDL_SOURCES})

target_include_directories(templeshrine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# SDL2 linking — works with both the modern CMake config (SDL2::SDL2)
# and the older module-style variables.
if(TARGET SDL2::SDL2)
    target_link_libraries(templeshrine PRIVATE SDL2::SDL2)
    if(TARGET SDL2::SDL2main)
        target_link_libraries(templeshrine PRIVATE SDL2::SDL2main)
    endif()
else()
    target_include_directories(templeshrine PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(templeshrine PRIVATE ${SDL2_LIBRARIES})
endif()

# libm for sinf/cosf on non-Windows toolchains.
if(NOT WIN32)
    target_link_libraries(templeshrine PRIVATE m)
endif()

# Silence a few warnings the shared code doesn't care about.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(templeshrine PRIVATE
        -Wno-unused-function
        -Wno-unused-variable
        -Wno-unused-but-set-variable)
endif()
