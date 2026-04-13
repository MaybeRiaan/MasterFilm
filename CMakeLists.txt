cmake_minimum_required(VERSION 3.21)
project(MasterFilm VERSION 1.0.0 LANGUAGES CXX)

# ── C++ standard ─────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Options ───────────────────────────────────────────────────────────────────
option(MASTERFILM_BUILD_PRO  "Build Pro mode (full parameter UI)"  ON)
option(MASTERFILM_BUILD_LITE "Build Lite mode (simplified UI)"     ON)
option(MASTERFILM_ENABLE_METAL "Enable Metal GPU path (macOS only)" ON)
option(MASTERFILM_ENABLE_OPENGL "Enable OpenGL/GLSL GPU path"      ON)

# ── Platform detection ────────────────────────────────────────────────────────
if(APPLE)
    message(STATUS "MasterFilm: macOS build")
    set(MASTERFILM_PLATFORM_MACOS TRUE)
    set(PLUGIN_INSTALL_DIR "$ENV{HOME}/Library/OFX/Plugins")
elseif(WIN32)
    message(STATUS "MasterFilm: Windows build")
    set(MASTERFILM_PLATFORM_WINDOWS TRUE)
    set(PLUGIN_INSTALL_DIR "C:/Program Files/Common Files/OFX/Plugins")
elseif(UNIX)
    message(STATUS "MasterFilm: Linux build")
    set(MASTERFILM_PLATFORM_LINUX TRUE)
    set(PLUGIN_INSTALL_DIR "/usr/OFX/Plugins")
endif()

# ── OpenFX SDK ────────────────────────────────────────────────────────────────
set(OFX_SDK_DIR "${CMAKE_SOURCE_DIR}/openfx-sdk" CACHE PATH "Path to OpenFX SDK headers")
if(NOT EXISTS "${OFX_SDK_DIR}/ofxCore.h")
    message(WARNING
        "OpenFX SDK headers not found at ${OFX_SDK_DIR}.\n"
        "Run: git submodule update --init --recursive\n"
        "Or set OFX_SDK_DIR to your local SDK path.")
endif()

# ── OpenGL + GLEW ─────────────────────────────────────────────────────────────
# Windows gl.h only covers OpenGL 1.1. All modern GL (shaders, VAOs, FBOs)
# requires extension loading — GLEW handles this automatically.
if(MASTERFILM_ENABLE_OPENGL)
    find_package(OpenGL QUIET)
    if(OpenGL_FOUND)
        message(STATUS "MasterFilm: OpenGL found — GLSL path enabled")
    else()
        message(STATUS "MasterFilm: OpenGL not found — GLSL path disabled")
        set(MASTERFILM_ENABLE_OPENGL OFF)
    endif()

    if(WIN32 AND MASTERFILM_ENABLE_OPENGL)
        find_package(GLEW REQUIRED)
        if(GLEW_FOUND)
            message(STATUS "MasterFilm: GLEW found — ${GLEW_VERSION}")
        else()
            message(FATAL_ERROR
                "GLEW not found. Install via: vcpkg install glew:x64-windows\n"
                "Then set cmakeToolchain in CMakeSettings.json to the vcpkg toolchain file.")
        endif()
    endif()
endif()

# ── Metal (macOS only) ────────────────────────────────────────────────────────
if(MASTERFILM_ENABLE_METAL AND NOT APPLE)
    message(STATUS "MasterFilm: Metal requires macOS — disabling")
    set(MASTERFILM_ENABLE_METAL OFF)
endif()
if(MASTERFILM_ENABLE_METAL AND APPLE)
    enable_language(OBJCXX)
    message(STATUS "MasterFilm: Metal path enabled")
endif()

# ── Source files ──────────────────────────────────────────────────────────────
set(MASTERFILM_SOURCES
    src/MasterFilmPlugin.cpp
    src/MasterFilmPlugin.h
    src/processors/GrainProcessor.cpp
    src/processors/GrainProcessor.h
    src/processors/HalationProcessor.cpp
    src/processors/HalationProcessor.h
    src/processors/AcutanceProcessor.cpp
    src/processors/AcutanceProcessor.h
    src/processors/UnifiedFilmProcessor.cpp
    src/processors/UnifiedFilmProcessor.h
    src/presets/FilmPreset.h
    src/presets/FilmPreset.cpp
    src/presets/StockLibrary.h
    src/presets/StockLibrary.cpp
    src/ui/ProUI.cpp
    src/ui/ProUI.h
    src/ui/LiteUI.cpp
    src/ui/LiteUI.h
)

if(MASTERFILM_ENABLE_OPENGL)
    list(APPEND MASTERFILM_SOURCES
        src/platform/GLSLDispatch.cpp
        src/platform/GLSLDispatch.h
    )
endif()

if(MASTERFILM_ENABLE_METAL AND APPLE)
    list(APPEND MASTERFILM_SOURCES
        src/platform/MetalDispatch.mm
        src/platform/MetalDispatch.h
    )
endif()

# ── Shared library (.ofx is a renamed .so / .dylib / .dll) ───────────────────
add_library(MasterFilm SHARED ${MASTERFILM_SOURCES})

set_target_properties(MasterFilm PROPERTIES
    PREFIX ""
    OUTPUT_NAME "MasterFilm"
    SUFFIX ".ofx"
)

# ── Include paths ─────────────────────────────────────────────────────────────
target_include_directories(MasterFilm PRIVATE
    ${OFX_SDK_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# ── Compile definitions ───────────────────────────────────────────────────────
target_compile_definitions(MasterFilm PRIVATE
    MASTERFILM_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    MASTERFILM_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    MASTERFILM_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    $<$<BOOL:${MASTERFILM_BUILD_PRO}>:MASTERFILM_BUILD_PRO>
    $<$<BOOL:${MASTERFILM_BUILD_LITE}>:MASTERFILM_BUILD_LITE>
    $<$<BOOL:${MASTERFILM_ENABLE_OPENGL}>:MASTERFILM_ENABLE_OPENGL>
    $<$<BOOL:${MASTERFILM_ENABLE_METAL}>:MASTERFILM_ENABLE_METAL>
    $<$<BOOL:${MASTERFILM_PLATFORM_MACOS}>:MASTERFILM_PLATFORM_MACOS>
    $<$<BOOL:${MASTERFILM_PLATFORM_WINDOWS}>:MASTERFILM_PLATFORM_WINDOWS>
    $<$<BOOL:${MASTERFILM_PLATFORM_LINUX}>:MASTERFILM_PLATFORM_LINUX>
)

# ── Link libraries ────────────────────────────────────────────────────────────
if(MASTERFILM_ENABLE_OPENGL)
    target_link_libraries(MasterFilm PRIVATE OpenGL::GL)
    if(WIN32 AND GLEW_FOUND)
        target_link_libraries(MasterFilm PRIVATE GLEW::GLEW)
    endif()
endif()

if(MASTERFILM_ENABLE_METAL AND APPLE)
    target_link_libraries(MasterFilm PRIVATE
        "-framework Metal"
        "-framework Foundation"
        "-framework QuartzCore"
    )
endif()

if(APPLE)
    target_link_options(MasterFilm PRIVATE
        -bundle
        -Wl,-exported_symbol,_OfxGetNumberOfPlugins
        -Wl,-exported_symbol,_OfxGetPlugin
    )
elseif(WIN32)
    # Export OFX entry points via .def or __declspec(dllexport) in source
endif()

# ── Compiler warnings ─────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(MasterFilm PRIVATE
        $<$<CONFIG:Release>:/Zi>   # Generate PDB in Release
    )
    target_link_options(MasterFilm PRIVATE
        $<$<CONFIG:Release>:/DEBUG> # Include debug info in binary
    )
endif()

# ── Bundle layout & install ───────────────────────────────────────────────────
include(cmake/BundleLayout.cmake)

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "──────────────────────────────────────────")
message(STATUS "  MasterFilm ${PROJECT_VERSION}")
message(STATUS "  Pro mode   : ${MASTERFILM_BUILD_PRO}")
message(STATUS "  Lite mode  : ${MASTERFILM_BUILD_LITE}")
message(STATUS "  OpenGL     : ${MASTERFILM_ENABLE_OPENGL}")
message(STATUS "  Metal      : ${MASTERFILM_ENABLE_METAL}")
message(STATUS "  Install dir: ${PLUGIN_INSTALL_DIR}")
message(STATUS "──────────────────────────────────────────")