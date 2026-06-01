cmake_minimum_required(VERSION 3.12)

project(SimpleCompilerInC_2 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PROJECT_SOURCE_DIR}/bin)

file(GLOB_RECURSE PROJECT_SOURCES
        CONFIGURE_DEPENDS
        "src/*.c"
        "src/*.h"
)

include_directories(src)

add_executable(main ${PROJECT_SOURCES})

# Compiler flags per build type
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(main PRIVATE
            -Wall -Wextra -Werror -Wpedantic
            -Wno-error=unused-variable
            -Wno-error=unused-parameter
            -Wno-error=unused-local-typedefs
            -Wno-error=unused-result
    )

    target_compile_options(main PRIVATE
            $<$<CONFIG:Debug>:   -O0 -g -fsanitize=address>
            $<$<CONFIG:Release>: -O2 -DNDEBUG -static>
    )
endif()


if(CMAKE_BUILD_TYPE STREQUAL "Release")
    find_library(MIMALLOC_STATIC
            NAMES libmimalloc.a
#            NAMES libmimalloc.a mimalloc
#            HINTS /usr/lib /usr/local/lib
    )
    if(MIMALLOC_STATIC)
        message(STATUS "mimalloc found: ${MIMALLOC_STATIC}")
        target_link_libraries(main PRIVATE ${MIMALLOC_STATIC})
    else()
        message(STATUS "mimalloc not found, building without it")
    endif()
endif()

target_link_options(main PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)
target_link_options(main PRIVATE $<$<CONFIG:Release>:-static>)
