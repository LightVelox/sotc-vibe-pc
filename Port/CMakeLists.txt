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
    src/sotc/log.cpp
    src/sotc/module_guard.cpp
    src/sotc/sha256.cpp
)

add_library(sotc_game_code STATIC ${SOTC_GENERATED_SOURCES})
target_include_directories(sotc_game_code PUBLIC "${SOTC_GENERATED_DIR}")
target_link_libraries(sotc_game_code PUBLIC ps2_runtime)
set_target_properties(sotc_game_code PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 48)
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

if(MSVC)
    target_compile_options(sotc_game_code PRIVATE /bigobj /fp:precise /W0 /MP)
    target_compile_options(sotc PRIVATE /fp:precise /W3 /utf-8)
    target_link_options(sotc PRIVATE /FORCE:MULTIPLE /STACK:16777216)
endif()

set_target_properties(sotc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
