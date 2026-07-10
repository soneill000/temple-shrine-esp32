# ESP-IDF component definition. Uses non-recursive glob so the SDL host
# harness under src/harness/ does not get pulled into the badge build.

FILE(GLOB app_sources ${CMAKE_SOURCE_DIR}/src/*.c)

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS ".")
