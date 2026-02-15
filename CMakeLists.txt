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

# giflib
find_library(GIF_LIBRARY NAMES gif PATHS /opt/homebrew/lib)

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

# Common source files (loaders, parsers, shared infrastructure)
set(COMMON_SRC
    src/TextureLoader.cpp
    src/BMDParser.cpp
    src/BMDUtils.cpp
    src/TerrainParser.cpp
    src/Camera.cpp
    src/Terrain.cpp
    src/Screenshot.cpp
    src/FireEffect.cpp
    src/Sky.cpp
    src/GrassRenderer.cpp
)

# Main Game Executable
add_executable(MuRemaster src/main.cpp src/ObjectRenderer.cpp src/ViewerCommon.cpp src/HeroCharacter.cpp src/ClickEffect.cpp ${COMMON_SRC} ${IMGUI_SOURCES})
target_link_libraries(MuRemaster PRIVATE
    glfw
    GLEW::GLEW
    OpenGL::GL
    ${JPEG_LIBRARY}
    ${GIF_LIBRARY}
)

# Standalone Model Viewer
add_executable(ModelViewer src/model_viewer_main.cpp src/ViewerCommon.cpp ${COMMON_SRC} ${IMGUI_SOURCES})
target_link_libraries(ModelViewer PRIVATE
    glfw
    GLEW::GLEW
    OpenGL::GL
    ${JPEG_LIBRARY}
    ${GIF_LIBRARY}
)

# Standalone Character Viewer
add_executable(CharViewer src/char_viewer_main.cpp src/ViewerCommon.cpp ${COMMON_SRC} ${IMGUI_SOURCES})
target_link_libraries(CharViewer PRIVATE
    glfw
    GLEW::GLEW
    OpenGL::GL
    ${JPEG_LIBRARY}
    ${GIF_LIBRARY}
)

if(APPLE)
    target_link_libraries(MuRemaster PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo" "-framework QuartzCore")
    target_link_libraries(ModelViewer PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo" "-framework QuartzCore")
    target_link_libraries(CharViewer PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo" "-framework QuartzCore")
endif()

message(STATUS "Configured MuRemaster, ModelViewer, and CharViewer")
