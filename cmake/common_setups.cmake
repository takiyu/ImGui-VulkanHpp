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
#   - 2021/06/03 Population switching
#   - 2021/06/08 Check version
#   - 2021/06/09 Explicit git checkout
#   - 2021/07/05 Restore FETCHCONTENT_... vairbales
#
message(STATUS "common_setups.cmake v0.7")
set(CSC_VERSION_LOCAL 7)

# Check version
if (DEFINED CSC_VERSION)
    # Check version
    if (NOT ${CSC_VERSION} EQUAL ${CSC_VERSION_LOCAL})
        message(FATAL_ERROR "CSC_VERSION mismatch")
    endif()
else()
    # Set version globally
    set(CSC_VERSION ${CSC_VERSION_LOCAL} CACHE INTERNAL "CSC_VERSION")
endif()

# Update this script
# file(DOWNLOAD
#      "https://raw.githubusercontent.com/takiyu/common_setups.cmake/master/common_setups.cmake"
#      ${CMAKE_CURRENT_SOURCE_DIR}/cmake/common_setups.cmake SHOW_PROGRESS)

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
    string(TOLOWER ${target} target_lc)             # Lower name
    message(">> FetchContent: [${target}](${tag})")

    # Version check
    if ("3.10" VERSION_LESS ${CMAKE_VERSION})
        # Store previous values
        set(PREV_QUIET ${FETCHCONTENT_QUIET})
        set(PREV_UPDATE_DISCONNECTED ${FETCHCONTENT_UPDATE_DISCONNECTED})
        set(PREV_FULLY_DISCONNECTED ${FETCHCONTENT_FULLY_DISCONNECTED})

        # Setup FetchContent
        include(FetchContent)
        set(FETCHCONTENT_QUIET TRUE)
        set(FETCHCONTENT_UPDATE_DISCONNECTED FALSE)  # Ensure

        # Checkout explicitly
        set(checkout_result "1")  # Mark as failed
        if (EXISTS ${third_party_dir}/${target}/.git)
            find_package(Git REQUIRED)
            execute_process(COMMAND ${GIT_EXECUTABLE} checkout ${tag}
                            WORKING_DIRECTORY ${third_party_dir}/${target}
                            RESULT_VARIABLE checkout_result
                            OUTPUT_QUIET ERROR_QUIET)
        endif()

        # Check need of update
        if (${checkout_result} EQUAL "0")
            message("  >> Already fetched")
            set(FETCHCONTENT_FULLY_DISCONNECTED TRUE)
        else()
            message("  >> Initial Download")
            set(FETCHCONTENT_FULLY_DISCONNECTED FALSE)
        endif()

        # Restore overwitten values
        set(FETCHCONTENT_QUIET ${PREV_QUIET})
        set(FETCHCONTENT_UPDATE_DISCONNECTED ${PREV_UPDATE_DISCONNECTED})
        set(FETCHCONTENT_FULLY_DISCONNECTED ${PREV_FULLY_DISCONNECTED})

        # Define
        FetchContent_Declare(${target}
                             GIT_REPOSITORY ${url}
                             SOURCE_DIR ${third_party_dir}/${target}
                             GIT_TAG ${tag})
        # Get properties
        FetchContent_GetProperties(${target})
        # Populate
        if (NOT ${target_lc}_POPULATED)  # (escape double population)
            FetchContent_Populate(${target})
        endif()
        # Subdirectory
        if (${is_subdir})
            # Note: The following two subdirectories are exactly same.
            # add_subdirectory(${${target_lc}_SOURCE_DIR}
            #                  ${${target_lc}_BINARY_DIR})
            add_subdirectory(${third_party_dir}/${target}
                             ${CMAKE_BINARY_DIR}/_deps/${target_lc}-build)
        endif()

    else()
        # No FetchContent
        message(STATUS "No FetchContent support (CMake 3.11 is required)")
        # Subdirectory
        if (${is_subdir})
            add_subdirectory(${third_party_dir}/${target}
                             ${CMAKE_BINARY_DIR}/_deps/${target_lc}-build)
        endif()
    endif()
endmacro()
