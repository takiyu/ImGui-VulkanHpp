cmake_minimum_required(VERSION 3.11)

# ------------------------------------------------------------------------------
# -------------------------------- Check parent --------------------------------
# ------------------------------------------------------------------------------
get_directory_property(HAS_PARENT PARENT_DIRECTORY)
if(HAS_PARENT)
    message(STATUS "Build ImGui-VulkanHpp as subdirectory mode")
else()
    message(STATUS "Build ImGui-VulkanHpp as independent mode")
endif()

# ------------------------------------------------------------------------------
# ----------------------------------- Options ----------------------------------
# ------------------------------------------------------------------------------
set(IMGUIVKHPP_BUILD_EXAMPLES ON CACHE BOOL "Build examples")
set(IMGUIVKHPP_USE_INTERNAL_IMGUI ON CACHE BOOL "Use Internal ImGui.")
set(IMGUIVKHPP_USE_INTERNAL_IMGUI_GLFW_BACKEND ON CACHE BOOL "Use GLFW backend")
set(IMGUIVKHPP_USE_INTERNAL_VKW ON CACHE BOOL "Use Internal Vulkan Wrapper.")
set(IMGUIVKHPP_EXTERNAL_INCLUDE "" CACHE STRING "External include paths.")
set(IMGUIVKHPP_EXTERNAL_LIBRARY "" CACHE STRING "External libraries.")

# Handle option dependency
if (IMGUIVKHPP_BUILD_EXAMPLES)
    set(IMGUIVKHPP_USE_INTERNAL_VKW ON)  # Use internal VKW for examples
endif()

# External includes/libraries
list(APPEND IMGUIVKHPP_INCLUDE ${IMGUIVKHPP_EXTERNAL_INCLUDE})
list(APPEND IMGUIVKHPP_LIBRARY ${IMGUIVKHPP_EXTERNAL_LIBRARY})

# ------------------------------------------------------------------------------
# ----------------------------------- Common -----------------------------------
# ------------------------------------------------------------------------------
project(imgui_vkhpp CXX C)

set(CMAKE_CXX_STANDARD 14)  # C++ 14

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    set(LINK_TYPE STATIC)
else()
    set(LINK_TYPE SHARED)
endif()

# Print make commands for debug
# set(CMAKE_VERBOSE_MAKEFILE 1)

# Set default build type
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Export `compile_commands.json`
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
    set(warning_options "/W4")
else()
    message(WARNING "Unsupported compiler for warning options")
    message ("CMAKE_CXX_COMPILER_ID is ${CMAKE_CXX_COMPILER_ID}")
endif()

# Utility function to setup a target (include, link, warning, sanitizer)
function(setup_target target includes libs)
    target_include_directories(${target} PUBLIC ${includes})
    target_link_libraries(${target} ${libs})
    set_target_properties(${target} PROPERTIES COMPILE_FLAGS ${warning_options})
endfunction(setup_target)

# Utility function to setup a target simply (include, link)
function(setup_target_simple target includes libs)
    target_include_directories(${target} PUBLIC ${includes})
    target_link_libraries(${target} ${libs})
endfunction(setup_target_simple)

# ------------------------------------------------------------------------------
# --------------------------- Third Party Libraries ----------------------------
# ------------------------------------------------------------------------------
# Vulkan Wrapper
if (IMGUIVKHPP_USE_INTERNAL_VKW)
    # Vulkan Wrapper
    message(STATUS "Setup Internal VulkanWrapper")
    set(VKW_PATH ${CMAKE_CURRENT_SOURCE_DIR}/third_party/VulkanWrapper)
    include(FetchContent)
    FetchContent_Declare(VulkanWrapper
                         GIT_REPOSITORY https://github.com/takiyu/VulkanWrapper
                         SOURCE_DIR ${VKW_PATH})
    FetchContent_Populate(VulkanWrapper)
    set(VKW_BUILD_EXAMPLES OFF CACHE BOOL "")
    set(VKW_LINK_DYNAMIC_VK ON CACHE BOOL "")
    add_subdirectory(${VKW_PATH})
    list(APPEND IMGUIVKHPP_INCLUDE ${VKW_INCLUDE})
    list(APPEND IMGUIVKHPP_LIBRARY ${VKW_LIBRARY})
    list(APPEND IMGUIVKHPP_DEFINE ${VKW_DEFINE})
endif()

# ImGui
if (IMGUIVKHPP_USE_INTERNAL_IMGUI)
    message(STATUS "Use internal ImGui")
    set(IMGUI_PATH ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
    set(IMGUI_SOURCES ${IMGUI_PATH}/imgui.cpp
                      ${IMGUI_PATH}/imgui_draw.cpp
                      ${IMGUI_PATH}/imgui_tables.cpp
                      ${IMGUI_PATH}/imgui_widgets.cpp
                      ${IMGUI_PATH}/imgui_demo.cpp)
    if (IMGUIVKHPP_USE_INTERNAL_IMGUI_GLFW_BACKEND)
        message(STATUS "  With GLFW backend")
        list(APPEND IMGUI_SOURCES ${IMGUI_PATH}/backends/imgui_impl_glfw.cpp)
    endif()
    add_library(imgui ${IMGUI_SOURCES})
    list(APPEND IMGUIVKHPP_INCLUDE ${IMGUI_PATH}
                                   ${IMGUI_PATH}/backends)
    setup_target_simple(imgui "${IMGUIVKHPP_INCLUDE}" "${IMGUIVKHPP_LIBRARY}")
    list(APPEND IMGUIVKHPP_LIBRARY imgui)
endif()

if (IMGUIVKHPP_BUILD_EXAMPLES)
    # GLM
    list(APPEND IMGUIVKHPP_INCLUDE ${VKW_PATH}/third_party/glm)
endif()

# ------------------------------------------------------------------------------
# ----------------------------- Internal Libraries -----------------------------
# ------------------------------------------------------------------------------
# ImGui-VulkanHpp
list(APPEND IMGUIVKHPP_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_library(imgui_vkhpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_impl_vulkanhpp.cpp
)
setup_target(imgui_vkhpp "${IMGUIVKHPP_INCLUDE}" "${IMGUIVKHPP_LIBRARY}")
add_definitions(${IMGUIVKHPP_DEFINE})
list(APPEND IMGUIVKHPP_LIBRARY imgui_vkhpp)

# ------------------------------------------------------------------------------
# ----------------------------- Pass to the parent -----------------------------
# ------------------------------------------------------------------------------
if(HAS_PARENT)
    set(IMGUIVKHPP_INCLUDE ${IMGUIVKHPP_INCLUDE} PARENT_SCOPE)
    set(IMGUIVKHPP_LIBRARY ${IMGUIVKHPP_LIBRARY} PARENT_SCOPE)
    set(IMGUIVKHPP_DEFINE ${IMGUIVKHPP_DEFINE} PARENT_SCOPE)
endif()

# ------------------------------------------------------------------------------
# ---------------------------------- Examples ----------------------------------
# ------------------------------------------------------------------------------
if (IMGUIVKHPP_BUILD_EXAMPLES)
    # Standard GLFW example
    add_executable(main_glfw
                   ${CMAKE_CURRENT_SOURCE_DIR}/example/main_glfw.cpp)
    setup_target(main_glfw "${IMGUIVKHPP_INCLUDE}" "${IMGUIVKHPP_LIBRARY}")

    # BG-mode GLFW example
    add_executable(main_glfw_bg
                   ${CMAKE_CURRENT_SOURCE_DIR}/example/main_glfw_bg.cpp)
    setup_target(main_glfw_bg "${IMGUIVKHPP_INCLUDE}" "${IMGUIVKHPP_LIBRARY}")
endif()
