# ------------------------------------------------------------------------------
# --------------------------- CMake Common Settings ----------------------------
# ------------------------------------------------------------------------------
# Copyright (c) 2021 takiyu
#  This software is released under the MIT License.
#  http://opensource.org/licenses/mit-license.php
#
# History
#   - 2021/05/31 Using FetchContent
#   - 2021/06/01 Add MSVC compile options
#   - 2021/06/01 Replace function with macro for no-scope.
#   - 2021/06/01 Remove .git from target name
#   - 2021/06/02 Considering directory existence for FetchContent
#

# Print make commands for debug
# set(CMAKE_VERBOSE_MAKEFILE 1)

# Set default build type
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Export `compile_commands.json`
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Sanitizers
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sanitizers)
find_package(Sanitizers) # Address sanitizer (-DSANITIZE_ADDRESS=ON)

# Set output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Warning options
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set(warning_options "-Wall -Wextra -Wconversion")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    set(warning_options "-Wall -Wextra -Wcast-align -Wcast-qual \
                         -Wctor-dtor-privacy -Wdisabled-optimization \
                         -Wformat=2 -Winit-self \
                         -Wmissing-declarations -Wmissing-include-dirs \
                         -Woverloaded-virtual -Wredundant-decls -Wshadow \
                         -Wsign-conversion -Wsign-promo \
                         -Wstrict-overflow=5 -Wundef -Wno-unknown-pragmas")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set(warning_options "--pedantic -Wall -Wextra -Wcast-align -Wcast-qual \
                         -Wctor-dtor-privacy -Wdisabled-optimization \
                         -Wformat=2 -Winit-self -Wlogical-op \
                         -Wmissing-declarations -Wmissing-include-dirs \
                         -Wnoexcept -Woverloaded-virtual \
                         -Wredundant-decls -Wshadow -Wsign-conversion \
                         -Wsign-promo -Wstrict-null-sentinel \
                         -Wstrict-overflow=5 -Wundef -Wno-unknown-pragmas")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    set(warning_options "/W3")
else()
    message(WARNING "Unsupported compiler for warning options")
    message("CMAKE_CXX_COMPILER_ID is ${CMAKE_CXX_COMPILER_ID}")
endif()

# Specific compile options
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    add_definitions(-D_CRT_SECURE_NO_WARNING)
    add_definitions(-DNOMINMAX)
    add_compile_options(/MP)      # Parallel build
    add_compile_options(/bigobj)  # Increase limit of symbols
endif()

# Utility function to setup a target
function(setup_target target includes libs is_own)
    # Include and Link
    target_include_directories(${target} PUBLIC ${includes})
    target_link_libraries(${target} ${libs})
    if (${is_own})
        # Warning and sanitizer
        set_target_properties(${target}
                              PROPERTIES COMPILE_FLAGS ${warning_options})
        add_sanitizers(${target})
    endif()
endfunction()

# Utility function to setup third_party (macro for no scope)
macro(setup_third_party url tag is_subdir third_party_dir)
    get_filename_component(target ${url} NAME_WLE)  # Generate name from URL
    message(">> FetchContent: [${target}](${tag})")

    # Version check
    if ("3.10" VERSION_LESS ${CMAKE_VERSION})
        # Setup FetchContent
        include(FetchContent)
        set(FETCHCONTENT_QUIET TRUE)
        # Check directory
        if (EXISTS ${third_party_dir}/${target})
            set(FETCHCONTENT_FULLY_DISCONNECTED TRUE)
        endif()
        # Define
        FetchContent_Declare(${target}
                             GIT_REPOSITORY ${url}
                             SOURCE_DIR ${third_party_dir}/${target}
                             GIT_TAG ${tag})
        if (${is_subdir})
            # Fetch and subdirectory
            FetchContent_MakeAvailable(${target})
        else()
            # Fetch
            FetchContent_Populate(${target})
        endif()
    else()
        # No FetchContent
        message(STATUS "No FetchContent support (CMake 3.11 is required)")
        if (${is_subdir})
            # Subdirectory
            add_subdirectory(${third_party_dir}/${target})
        endif()
    endif()
endmacro()
