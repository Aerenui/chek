cmake_minimum_required(VERSION 3.12)

project(SimpleCompilerInC_2 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PROJECT_BINARY_DIR}/bin/release)
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PROJECT_BINARY_DIR}/bin/debug)

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
    )

    target_compile_options(main PRIVATE
            $<$<CONFIG:Debug>:   -O0 -g -fsanitize=address>
            $<$<CONFIG:Release>: -O2 -DNDEBUG>
    )
endif()

# mimalloc
find_package(mimalloc REQUIRED)
#target_link_libraries(main PRIVATE mimalloc)
target_link_libraries(main PRIVATE
        $<$<CONFIG:Release>:mimalloc>
)
target_link_options(main PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)

#message(STATUS "Binary dir: ${PROJECT_BINARY_DIR}")
#message(STATUS "Output dir: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")



#cmake_minimum_required(VERSION 3.12)
#
## Project Name and Language
#project(SimpleCompilerInC_2 C)
#
## Set the C Standard (e.g., C11 or C99)
#set(CMAKE_C_STANDARD 11)
#set(CMAKE_C_STANDARD_REQUIRED ON)
#
## Add compiler flags
#if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
#    add_compile_options(-g -lc -O0 -Wall -Wextra -Werror -Wpedantic -Wno-error=unused-variable -Wno-error=unused-parameter -Wno-error=unused-local-typedefs)
#endif()
#
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
#
## Automatically find all .c and .h files in the src/ directory
#file(GLOB_RECURSE PROJECT_SOURCES
#        CONFIGURE_DEPENDS
#        "src/*.c"
#        "src/*.h"
#)
#
## Tell CMake to include the src directory so it can find the .h files
#include_directories(src)
#
## Create the executable using the automatically gathered sources list
#add_executable(main ${PROJECT_SOURCES})
#
