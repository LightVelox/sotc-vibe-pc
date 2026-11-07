set(SOTC_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")

if(NOT EXISTS "${SOTC_GENERATED_DIR}/register_functions.cpp" OR NOT EXISTS "${SOTC_GENERATED_DIR}/sotc_layout_generated.h")
    message(STATUS
        "Recompiled game code not found in ${SOTC_GENERATED_DIR}; the sotc target will be added "
        "after Tools/generate.bat has produced it. Run Tools/generate.bat (or Tools/generate.py) "
        "with your own disc image, then configure again.")
    return()
endif()

file(GLOB SOTC_GENERATED_SOURCES CONFIGURE_DEPENDS "${SOTC_GENERATED_DIR}/*.cpp")

set(SOTC_HOST_SOURCES
    src/main.cpp
    src/sotc/config.cpp
    src/sotc/debug_script.cpp
    src/sotc/display_mode.cpp
    src/sotc/function_hooks.cpp
    src/sotc/game_disc.cpp
    src/sotc/hle/sce_cdvd.cpp
    src/sotc/hle/libgcc.cpp
    src/sotc/hle/sce_fileio.cpp
    src/sotc/idle_thread.cpp
    src/sotc/log.cpp
    src/sotc/module_guard.cpp
    src/sotc/motion_blur.cpp
    src/sotc/native_effects.cpp
    src/sotc/mouse_camera.cpp
    src/sotc/sha256.cpp
    src/sotc/window_icon.cpp
    src/sotc/watchdog.cpp
)

set(SOTC_GAME_CODE_LIBRARY "" CACHE FILEPATH "Reuse an unchanged generated EE library for host-only builds")
if(SOTC_GAME_CODE_LIBRARY)
    if(NOT EXISTS "${SOTC_GAME_CODE_LIBRARY}")
        message(FATAL_ERROR "Cached generated EE library does not exist: ${SOTC_GAME_CODE_LIBRARY}")
    endif()
    add_library(sotc_game_code STATIC IMPORTED)
    set_target_properties(sotc_game_code PROPERTIES IMPORTED_LOCATION "${SOTC_GAME_CODE_LIBRARY}")
    target_include_directories(sotc_game_code INTERFACE "${SOTC_GENERATED_DIR}")
    target_link_libraries(sotc_game_code INTERFACE ps2_runtime)
else()
add_library(sotc_game_code STATIC ${SOTC_GENERATED_SOURCES})
target_include_directories(sotc_game_code PUBLIC "${SOTC_GENERATED_DIR}")
target_link_libraries(sotc_game_code PUBLIC ps2_runtime)
set_target_properties(sotc_game_code PROPERTIES UNITY_BUILD OFF)
target_precompile_headers(sotc_game_code PRIVATE
    "${CMAKE_SOURCE_DIR}/External/PS2Recomp/ps2xRuntime/include/ps2_runtime_macros.h"
    "${CMAKE_SOURCE_DIR}/External/PS2Recomp/ps2xRuntime/include/ps2_runtime.h"
    "${SOTC_GENERATED_DIR}/ps2_recompiled_functions.h"
    "${SOTC_GENERATED_DIR}/ps2_recompiled_stubs.h"
    "${CMAKE_SOURCE_DIR}/External/PS2Recomp/ps2xRuntime/include/ps2_syscalls.h"
    "${CMAKE_SOURCE_DIR}/External/PS2Recomp/ps2xRuntime/include/ps2_stubs.h"
)
endif()

add_executable(sotc ${SOTC_HOST_SOURCES})
target_include_directories(sotc PRIVATE src "${SOTC_GENERATED_DIR}")
if(MSVC)
    target_link_libraries(sotc PRIVATE sotc_game_code ps2_runtime)
else()
    # ps2_runtime reads the function table defined in the game code, which itself calls into ps2_runtime.
    # GNU ld scans each archive once in order, so the two have to be rescanned as a group.
    target_link_libraries(sotc PRIVATE "$<LINK_GROUP:RESCAN,sotc_game_code,ps2_runtime>")
endif()
if(WIN32)
    enable_language(RC)
    target_sources(sotc PRIVATE assets/sotc.rc)
    target_include_directories(sotc PRIVATE "${CMAKE_SOURCE_DIR}")
else()
    # No resource compiler: embed the window icon PNG as a byte array instead.
    set(SOTC_ICON_PNG "${CMAKE_SOURCE_DIR}/game_icon.png")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SOTC_ICON_PNG}")
    file(READ "${SOTC_ICON_PNG}" SOTC_ICON_HEX HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," SOTC_ICON_BYTES "${SOTC_ICON_HEX}")
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/embedded/sotc_icon_png.h"
        CONTENT "#pragma once\nstatic const unsigned char kSotcIconPng[] = {${SOTC_ICON_BYTES}};\n")
    target_include_directories(sotc PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/embedded")
    target_compile_definitions(sotc PRIVATE SOTC_HAS_EMBEDDED_ICON=1)
endif()

file(GLOB SOTC_VU1_SOURCES CONFIGURE_DEPENDS "${SOTC_GENERATED_DIR}/vu1/*.cpp")
if(SOTC_VU1_SOURCES)
    add_library(sotc_vu1_code STATIC ${SOTC_VU1_SOURCES})
    target_include_directories(sotc_vu1_code PRIVATE "${CMAKE_SOURCE_DIR}/External/PS2Recomp/ps2xRuntime/src/lib/vu")
    target_link_libraries(sotc_vu1_code PUBLIC ps2_runtime)
    target_link_libraries(sotc PRIVATE sotc_vu1_code)
    target_compile_definitions(sotc PRIVATE SOTC_HAS_VU1_PROGRAMS=1)
    if(MSVC)
        target_compile_options(sotc_vu1_code PRIVATE /bigobj /fp:precise /arch:AVX2 /W0 /MP $<$<CONFIG:RelWithDebInfo>:/Ob2>)
    else()
        target_compile_options(sotc_vu1_code PRIVATE -w -g0)
    endif()
endif()

# tools/vu1_bench.cpp samples threads with the Win32 API.
if(SOTC_VU1_SOURCES AND WIN32)
    add_executable(sotc_vu1_bench tools/vu1_bench.cpp)
    target_include_directories(sotc_vu1_bench PRIVATE "${CMAKE_SOURCE_DIR}/External/PS2Recomp/ps2xRuntime/src/lib/vu")
    target_link_libraries(sotc_vu1_bench PRIVATE sotc_vu1_code ps2_runtime)
    set_target_properties(sotc_vu1_bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(MSVC)
        target_compile_options(sotc_vu1_bench PRIVATE /fp:precise /W3 /utf-8)
    endif()
    if(SOTC_VU1_ALT_DIR)
        file(GLOB SOTC_VU1_ALT_SOURCES CONFIGURE_DEPENDS "${SOTC_VU1_ALT_DIR}/*.cpp")
        add_library(sotc_vu1_alt STATIC ${SOTC_VU1_ALT_SOURCES})
        target_include_directories(sotc_vu1_alt PRIVATE "${CMAKE_SOURCE_DIR}/External/PS2Recomp/ps2xRuntime/src/lib/vu")
        target_link_libraries(sotc_vu1_alt PUBLIC ps2_runtime)
        add_executable(sotc_vu1_bench_alt tools/vu1_bench.cpp)
        target_include_directories(sotc_vu1_bench_alt PRIVATE "${CMAKE_SOURCE_DIR}/External/PS2Recomp/ps2xRuntime/src/lib/vu")
        target_link_libraries(sotc_vu1_bench_alt PRIVATE sotc_vu1_alt ps2_runtime)
        set_target_properties(sotc_vu1_bench_alt PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        if(MSVC)
            target_compile_options(sotc_vu1_alt PRIVATE /bigobj /fp:precise /arch:AVX2 /W0 /MP $<$<CONFIG:RelWithDebInfo>:/Ob2>)
            target_compile_options(sotc_vu1_bench_alt PRIVATE /fp:precise /W3 /utf-8)
        endif()
    endif()
endif()

if(MSVC)
    if(NOT SOTC_GAME_CODE_LIBRARY)
        target_compile_options(sotc_game_code PRIVATE /bigobj /fp:precise /arch:AVX2 /GS- /d2OptimizeHugeFunctions /W0 /MP)
        set_source_files_properties("${SOTC_GENERATED_DIR}/register_functions.cpp" PROPERTIES COMPILE_OPTIONS "/Z7;/Ob0;/Od" SKIP_PRECOMPILE_HEADERS ON)
    endif()
    target_compile_options(sotc PRIVATE /fp:precise /W3 /utf-8)
    target_compile_definitions(sotc PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_options(sotc PRIVATE /FORCE:MULTIPLE /STACK:16777216)
else()
    if(NOT SOTC_GAME_CODE_LIBRARY)
        # Debug info for thousands of generated files costs gigabytes, so game code is built without it.
        target_compile_options(sotc_game_code PRIVATE -w -g0 -fwrapv -fno-stack-protector)
        set_source_files_properties("${SOTC_GENERATED_DIR}/register_functions.cpp" PROPERTIES COMPILE_OPTIONS "-O0" SKIP_PRECOMPILE_HEADERS ON)
    endif()
    # Same first-definition-wins linking as /FORCE:MULTIPLE; the 16 MB thread stack is set in main().
    target_link_options(sotc PRIVATE -Wl,--allow-multiple-definition)
endif()

set_target_properties(sotc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
