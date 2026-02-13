cmake_minimum_required(VERSION 3.20)
project(MuRemaster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS specific configuration
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0")
    include_directories(/opt/homebrew/include)
    link_directories(/opt/homebrew/lib)
endif()

# Find packages
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)

# libjpeg-turbo
find_path(JPEG_INCLUDE_DIR NAMES turbojpeg.h PATHS /opt/homebrew/include)
find_library(JPEG_LIBRARY NAMES turbojpeg PATHS /opt/homebrew/lib)

# ImGui setup
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# Source and include directories
include_directories(include)
include_directories(/opt/homebrew/include) # Ensure GLM and others are found
include_directories(${IMGUI_DIR})
include_directories(${IMGUI_DIR}/backends)

file(GLOB_RECURSE SRC_FILES src/*.cpp)

add_executable(${PROJECT_NAME} ${SRC_FILES} ${IMGUI_SOURCES})

# Linking
target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    GLEW::GLEW
    OpenGL::GL
    ${JPEG_LIBRARY}
)

if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo" "-framework QuartzCore")
endif()

message(STATUS "Configured MuRemaster with GLFW, GLEW, ImGui, and libjpeg-turbo")
