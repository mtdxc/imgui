cmake_minimum_required(VERSION 3.16)

project(sdl3_audio_test LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(sdl3_audio_test
    main.cpp
    howling.cpp
    feedback_suppression.cpp
    avp_afe_howling.c
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)

target_include_directories(sdl3_audio_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)

if(WIN32 AND MSVC)
    set(SDL3_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../libs/sdl3" CACHE PATH "Path to the SDL3 SDK")
    if(DEFINED ENV{SDL3_DIR})
        set(SDL3_ROOT "$ENV{SDL3_DIR}")
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(SDL3_LIBRARY_SUBDIR x64)
    else()
        set(SDL3_LIBRARY_SUBDIR x86)
    endif()

    find_path(SDL3_INCLUDE_DIR SDL3/SDL.h PATHS "${SDL3_ROOT}/include" NO_DEFAULT_PATH)
    find_library(SDL3_LIBRARY NAMES SDL3 PATHS "${SDL3_ROOT}/lib/${SDL3_LIBRARY_SUBDIR}" NO_DEFAULT_PATH)
    if(NOT SDL3_INCLUDE_DIR OR NOT SDL3_LIBRARY)
        message(FATAL_ERROR "SDL3 SDK not found under '${SDL3_ROOT}'. Set SDL3_DIR to the SDK root.")
    endif()

    target_include_directories(sdl3_audio_test PRIVATE "${SDL3_INCLUDE_DIR}")
    target_link_libraries(sdl3_audio_test PRIVATE "${SDL3_LIBRARY}")

    find_file(SDL3_RUNTIME SDL3.dll PATHS "${SDL3_ROOT}/lib/${SDL3_LIBRARY_SUBDIR}" NO_DEFAULT_PATH)
    if(SDL3_RUNTIME)
        add_custom_command(TARGET sdl3_audio_test POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDL3_RUNTIME}" "$<TARGET_FILE_DIR:sdl3_audio_test>"
        )
    endif()
    add_custom_command(TARGET sdl3_audio_test POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/jaec_x86.dll" "$<TARGET_FILE_DIR:sdl3_audio_test>")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
    target_link_libraries(sdl3_audio_test PRIVATE PkgConfig::SDL3)
    add_custom_command(TARGET sdl3_audio_test POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/jaec_x86.so" "$<TARGET_FILE_DIR:sdl3_audio_test>")
    add_custom_command(TARGET sdl3_audio_test POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/jaec_arm.so" "$<TARGET_FILE_DIR:sdl3_audio_test>")
endif()

if(WIN32)
    target_link_libraries(sdl3_audio_test PRIVATE gdi32 opengl32 imm32)
elseif(APPLE)
    find_library(OPENGL_FRAMEWORK OpenGL)
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(COREVIDEO_FRAMEWORK CoreVideo)
    target_link_libraries(sdl3_audio_test PRIVATE
        "${OPENGL_FRAMEWORK}"
        "${COCOA_FRAMEWORK}"
        "${IOKIT_FRAMEWORK}"
        "${COREVIDEO_FRAMEWORK}"
    )
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(sdl3_audio_test PRIVATE OpenGL::GL ${CMAKE_DL_LIBS})
endif()

if(MSVC)
    target_compile_options(sdl3_audio_test PRIVATE /W4)
else()
    target_compile_options(sdl3_audio_test PRIVATE -g -Wall -Wformat)
endif()

add_custom_command(TARGET sdl3_audio_test POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/tde_lp.bin" "$<TARGET_FILE_DIR:sdl3_audio_test>")

