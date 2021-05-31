message(STATUS "--------------------------------------------------------------")
message(STATUS "----------------------- ImGui-VulkanHpp ----------------------")
message(STATUS "--------------------------------------------------------------")
cmake_minimum_required(VERSION 3.10)

# ------------------------------------------------------------------------------
# -------------------------------- Check parent --------------------------------
# ------------------------------------------------------------------------------
get_directory_property(HAS_PARENT PARENT_DIRECTORY)
if (HAS_PARENT)
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
set(IMGUIVKHPP_THIRD_PARTY_PATH "" CACHE STRING "Third party directory path.")

# Handle option dependency
if (IMGUIVKHPP_BUILD_EXAMPLES)
    set(IMGUIVKHPP_USE_INTERNAL_VKW ON)  # Use internal VKW for examples
endif()
if (ANDROID)
    set(IMGUIVKHPP_USE_INTERNAL_IMGUI_GLFW_BACKEND OFF)
endif()
if (NOT IMGUIVKHPP_THIRD_PARTY_PATH)
    set(IMGUIVKHPP_THIRD_PARTY_PATH ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
endif()

# External includes/libraries
list(APPEND IMGUIVKHPP_INCLUDE ${IMGUIVKHPP_EXTERNAL_INCLUDE})
list(APPEND IMGUIVKHPP_LIBRARY ${IMGUIVKHPP_EXTERNAL_LIBRARY})

# ------------------------------------------------------------------------------
# ----------------------------------- Common -----------------------------------
# ------------------------------------------------------------------------------
project(imgui_vkhpp CXX C)
set(CMAKE_CXX_STANDARD 14)  # C++ 14

# Include common configurations
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/common_setups.cmake)

# ------------------------------------------------------------------------------
# --------------------------- Third Party Libraries ----------------------------
# ------------------------------------------------------------------------------
# Vulkan Wrapper
if (IMGUIVKHPP_USE_INTERNAL_VKW)
    # Vulkan Wrapper
    message(STATUS "Setup Internal VulkanWrapper")
    set(VKW_BUILD_EXAMPLES OFF CACHE BOOL "")
    set(VKW_LINK_DYNAMIC_VK ON CACHE BOOL "")
    set(VKW_THIRD_PARTY_PATH ${IMGUIVKHPP_THIRD_PARTY_PATH} CACHE STRING "")
    setup_third_party("https://github.com/takiyu/VulkanWrapper" "master" TRUE
                      ${IMGUIVKHPP_THIRD_PARTY_PATH})
    list(APPEND IMGUIVKHPP_INCLUDE ${VKW_INCLUDE})
    list(APPEND IMGUIVKHPP_LIBRARY ${VKW_LIBRARY})
    list(APPEND IMGUIVKHPP_DEFINE ${VKW_DEFINE})
endif()

# ImGui
if (IMGUIVKHPP_USE_INTERNAL_IMGUI)
    message(STATUS "Use internal ImGui")
    setup_third_party("https://github.com/ocornut/imgui"
                      "fa963b9aafde7f05b1d32d42e3d2b084c9685e31" FALSE
                      ${IMGUIVKHPP_THIRD_PARTY_PATH})
    set(IMGUI_PATH ${IMGUIVKHPP_THIRD_PARTY_PATH}/imgui)
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
    setup_target(imgui "${IMGUIVKHPP_INCLUDE}" "${IMGUIVKHPP_LIBRARY}" FALSE)
    list(APPEND IMGUIVKHPP_LIBRARY imgui)
endif()

if (IMGUIVKHPP_BUILD_EXAMPLES)
    # GLM
    setup_third_party("https://github.com/g-truc/glm"
                      "66062497b104ca7c297321bd0e970869b1e6ece5" FALSE
                      ${IMGUIVKHPP_THIRD_PARTY_PATH})
    list(APPEND IMGUIVKHPP_INCLUDEVKW_INCLUDE ${VKW_THIRD_PARTY_PATH}/glm)
endif()

# ------------------------------------------------------------------------------
# ----------------------------- Internal Libraries -----------------------------
# ------------------------------------------------------------------------------
# ImGui-VulkanHpp
list(APPEND IMGUIVKHPP_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_library(imgui_vkhpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_impl_vulkanhpp.cpp
)
setup_target(imgui_vkhpp "${IMGUIVKHPP_INCLUDE}" "${IMGUIVKHPP_LIBRARY}" TRUE)
add_definitions(${IMGUIVKHPP_DEFINE})
list(APPEND IMGUIVKHPP_LIBRARY imgui_vkhpp)

# ------------------------------------------------------------------------------
# ----------------------------- Pass to the parent -----------------------------
# ------------------------------------------------------------------------------
if (HAS_PARENT)
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
    setup_target(main_glfw
                 "${IMGUIVKHPP_INCLUDE}" "${IMGUIVKHPP_LIBRARY}" TRUE)

    # BG-mode GLFW example
    add_executable(main_glfw_bg
                   ${CMAKE_CURRENT_SOURCE_DIR}/example/main_glfw_bg.cpp)
    setup_target(main_glfw_bg
                 "${IMGUIVKHPP_INCLUDE}" "${IMGUIVKHPP_LIBRARY}" TRUE)
endif()

message(STATUS "--------------------------------------------------------------")
message(STATUS "--------------------------------------------------------------")
message(STATUS "--------------------------------------------------------------")
