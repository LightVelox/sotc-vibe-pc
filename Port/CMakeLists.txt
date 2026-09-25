set(SOTC_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")

if(NOT EXISTS "${SOTC_GENERATED_DIR}/register_functions.cpp" OR NOT EXISTS "${SOTC_GENERATED_DIR}/sotc_layout_generated.h")
    message(FATAL_ERROR
        "Recompiled game code not found in ${SOTC_GENERATED_DIR}.\n"
        "Run Tools/generate.bat (or Tools/generate.py) with your own disc image first.")
endif()

file(GLOB SOTC_GENERATED_SOURCES CONFIGURE_DEPENDS "${SOTC_GENERATED_DIR}/*.cpp")

set(SOTC_HOST_SOURCES
    src/main.cpp
    src/sotc/function_hooks.cpp
    src/sotc/game_disc.cpp
    src/sotc/hle/sce_cdvd.cpp
    src/sotc/hle/sce_fileio.cpp
    src/sotc/idle_thread.cpp
    src/sotc/log.cpp
    src/sotc/module_guard.cpp
    src/sotc/sha256.cpp
    src/sotc/watchdog.cpp
)

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

add_executable(sotc ${SOTC_HOST_SOURCES})
target_include_directories(sotc PRIVATE src "${SOTC_GENERATED_DIR}")
target_link_libraries(sotc PRIVATE sotc_game_code ps2_runtime)

file(GLOB SOTC_VU1_SOURCES CONFIGURE_DEPENDS "${SOTC_GENERATED_DIR}/vu1/*.cpp")
if(SOTC_VU1_SOURCES)
    add_library(sotc_vu1_code STATIC ${SOTC_VU1_SOURCES})
    target_include_directories(sotc_vu1_code PRIVATE "${CMAKE_SOURCE_DIR}/External/PS2Recomp/ps2xRuntime/src/lib/vu")
    target_link_libraries(sotc_vu1_code PUBLIC ps2_runtime)
    target_link_libraries(sotc PRIVATE sotc_vu1_code)
    target_compile_definitions(sotc PRIVATE SOTC_HAS_VU1_PROGRAMS=1)
    if(MSVC)
        target_compile_options(sotc_vu1_code PRIVATE /bigobj /fp:precise /arch:AVX2 /W0 /MP $<$<CONFIG:RelWithDebInfo>:/Ob2>)
    endif()
endif()

if(MSVC)
    target_compile_options(sotc_game_code PRIVATE /bigobj /fp:precise /W0 /MP $<$<CONFIG:RelWithDebInfo>:/Ob2>)
    target_compile_options(sotc PRIVATE /fp:precise /W3 /utf-8)
    target_compile_definitions(sotc PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_options(sotc PRIVATE /FORCE:MULTIPLE /STACK:16777216 $<$<CONFIG:RelWithDebInfo>:/LTCG>)
endif()

set_target_properties(sotc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
