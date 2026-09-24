cmake_minimum_required(VERSION 3.20)

# ── DMFG detection property tests ──
# Standalone console executables — no Windows API dependencies.

add_executable(test_dmfg_detection test_dmfg_detection.cpp)
target_compile_features(test_dmfg_detection PRIVATE cxx_std_17)

if(MSVC)
    set_property(TARGET test_dmfg_detection PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

## ── Scheduler passthrough unit tests ──
# Tests the passthrough gating logic as pure functions.

add_executable(test_scheduler_passthrough test_scheduler_passthrough.cpp)
target_compile_features(test_scheduler_passthrough PRIVATE cxx_std_17)

if(MSVC)
    set_property(TARGET test_scheduler_passthrough PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

## ── DMFG output cap property tests ──
# Tests cadence formula, EMA convergence, config clamping, wake target.

add_executable(test_dmfg_output_cap test_dmfg_output_cap.cpp)
target_compile_features(test_dmfg_output_cap PRIVATE cxx_std_17)

if(MSVC)
    set_property(TARGET test_dmfg_output_cap PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

enable_testing()
add_test(NAME dmfg_detection_properties COMMAND test_dmfg_detection)
add_test(NAME scheduler_passthrough COMMAND test_scheduler_passthrough)
add_test(NAME dmfg_output_cap_properties COMMAND test_dmfg_output_cap)

# ── Host API tests ──
# Links against src/api.cpp only. It defines its own Config and ApplyConfig/SaveConfig, so config.cpp
# stays out of the link and the test needs no ReShade, no GPU and no game.
#
# A CMake target rather than a hand-written cl invocation in CI: MSBuild already has the toolchain
# environment, which is what three failed attempts at calling vcvars from a workflow step were about.
add_executable(api_test api_test.cpp ${CMAKE_SOURCE_DIR}/src/api.cpp)
target_compile_features(api_test PRIVATE cxx_std_17)
target_include_directories(api_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(api_test PRIVATE RELIMITER_VERSION_STRING="${RELIMITER_VERSION}")

if(MSVC)
    set_property(TARGET api_test PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
