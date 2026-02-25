cmake_minimum_required(VERSION 3.20)
project(avframe_editor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Path to Dear ImGui")

if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
    message(FATAL_ERROR "Dear ImGui not found. Please place it at ${IMGUI_DIR} (expected file: imgui.cpp).")
endif()

add_executable(avframe_editor
    main.cpp
    frame_editor.cpp
    frame_editor.hpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(avframe_editor PRIVATE
    include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(avframe_editor PRIVATE
    PkgConfig::FFMPEG
    SDL2::SDL2
    OpenGL::GL
)

if(APPLE)
    target_link_libraries(avframe_editor PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()
