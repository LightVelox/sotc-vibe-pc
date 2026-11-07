cmake_minimum_required(VERSION 3.21)

project(SotcPC LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PS2X_BUILD_RECOMP ON CACHE BOOL "" FORCE)
set(PS2X_BUILD_RUNTIME ON CACHE BOOL "" FORCE)
set(PS2X_BUILD_ANALYZER OFF CACHE BOOL "" FORCE)
if(NOT PS2X_BUILD_TEST)
    set(PS2X_BUILD_TEST OFF CACHE BOOL "" FORCE)
endif()
set(PS2X_BUILD_STUDIO OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_DEBUG_UI OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_FFMPEG OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_AGRESSIVE_LOGS OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_SCCACHE OFF CACHE BOOL "" FORCE)

if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    # GCC/Clang equivalents of the MSVC build: AVX2 like /arch:AVX2, no fused multiply-add like
    # /fp:precise, and no type-based alias analysis, which recompiled guest code would violate.
    add_compile_options(-march=x86-64-v3 -ffp-contract=off -fno-strict-aliasing)
endif()

add_subdirectory(External/PS2Recomp EXCLUDE_FROM_ALL)
if(NOT TARGET vu1_recomp)
    add_executable(vu1_recomp EXCLUDE_FROM_ALL External/PS2Recomp/ps2xTest/tools/vu1_recomp.cpp)
    target_link_libraries(vu1_recomp PRIVATE ps2_runtime)
    target_include_directories(vu1_recomp PRIVATE External/PS2Recomp/ps2xRuntime/src/lib/vu)
endif()
add_subdirectory(Port)
