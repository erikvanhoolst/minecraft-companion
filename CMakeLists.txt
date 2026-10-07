# SPDX-License-Identifier: GPL-3.0-or-later
#
# Minecraft inventory companion for Eden Duo: the native module 0100D71004694000.so.
# Self-contained: the Eden Duo module ABI headers (native/sdk), nlohmann/json and stb_image are
# vendored, so no Eden Duo checkout is needed. Use scripts/build.sh for the usual builds.

cmake_minimum_required(VERSION 3.22)
project(minecraft-inventory-companion LANGUAGES CXX)

if(ANDROID AND NOT ANDROID_STL STREQUAL "c++_static")
    message(FATAL_ERROR "Build the Android module with -DANDROID_STL=c++_static")
endif()

set(TITLE_ID 0100D71004694000)
add_library(mc_companion SHARED
    native/0100D71004694000.cpp
    native/mc_assets.cpp
    native/mc_assets.h
    native/mc_clock.cpp
    native/mc_clock.h
    native/mc_debug.cpp
    native/mc_debug.h
    native/mc_exploration.cpp
    native/mc_exploration.h
    native/mc_map.cpp
    native/mc_map.h
    native/mc_waypoints.cpp
    native/mc_waypoints.h
    native/mc_names.cpp
    native/mc_names.h
    native/mc_projects.cpp
    native/mc_projects.h
    native/mc_reader.cpp
    native/mc_reader.h
    native/mc_zip.cpp
    native/mc_zip.h)
find_package(Threads REQUIRED)
target_link_libraries(mc_companion PRIVATE Threads::Threads)
target_compile_features(mc_companion PRIVATE cxx_std_20)
target_include_directories(mc_companion PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/native"
    "${CMAKE_CURRENT_LIST_DIR}/native/sdk"
    "${CMAKE_CURRENT_LIST_DIR}/native/third_party")
set_target_properties(mc_companion PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${TITLE_ID}"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# Export exactly the getters the Eden Duo host looks up (module, extensions, font); hide
# everything else.
if(UNIX AND NOT APPLE)
    set(script_file "${CMAKE_CURRENT_BINARY_DIR}/exports.map")
    file(WRITE "${script_file}"
        "{\n    global:\n        eden_dsmod_get_module;\n        eden_dsmod_get_extensions;\n        eden_dsmod_get_font_extensions;\n    local:\n        *;\n};\n")
    target_link_options(mc_companion PRIVATE
        "-Wl,--no-undefined"
        "-Wl,--version-script=${script_file}")
    set_property(TARGET mc_companion APPEND PROPERTY LINK_DEPENDS "${script_file}")
endif()
if(ANDROID)
    target_link_options(mc_companion PRIVATE "-Wl,--exclude-libs,ALL")
endif()

include(CTest)
if(BUILD_TESTING AND NOT ANDROID)
    add_executable(mc_reader_tests tests/test_reader.cpp
        native/mc_reader.cpp native/mc_assets.cpp native/mc_names.cpp native/mc_zip.cpp)
    target_compile_features(mc_reader_tests PRIVATE cxx_std_20)
    target_include_directories(mc_reader_tests PRIVATE native native/sdk native/third_party)
    add_test(NAME mc_reader COMMAND mc_reader_tests)
    add_executable(mc_clock_tests tests/test_clock.cpp native/mc_clock.cpp
        native/mc_reader.cpp native/mc_assets.cpp native/mc_names.cpp native/mc_zip.cpp)
    target_compile_features(mc_clock_tests PRIVATE cxx_std_20)
    target_include_directories(mc_clock_tests PRIVATE native native/sdk native/third_party)
    add_test(NAME mc_clock COMMAND mc_clock_tests)
    add_executable(mc_exploration_tests tests/test_exploration.cpp
        native/mc_exploration.cpp native/mc_map.cpp native/mc_reader.cpp
        native/mc_assets.cpp native/mc_names.cpp native/mc_zip.cpp)
    target_compile_features(mc_exploration_tests PRIVATE cxx_std_20)
    target_include_directories(mc_exploration_tests PRIVATE native native/sdk native/third_party)
    target_link_libraries(mc_exploration_tests PRIVATE Threads::Threads)
    add_test(NAME mc_exploration COMMAND mc_exploration_tests)
    add_executable(mc_waypoint_tests tests/test_waypoints.cpp native/mc_waypoints.cpp)
    target_compile_features(mc_waypoint_tests PRIVATE cxx_std_20)
    target_include_directories(mc_waypoint_tests PRIVATE native native/sdk native/third_party)
    add_test(NAME mc_waypoints COMMAND mc_waypoint_tests)
    add_executable(mc_death_tests tests/test_death.cpp native/mc_waypoints.cpp)
    target_compile_features(mc_death_tests PRIVATE cxx_std_20)
    target_include_directories(mc_death_tests PRIVATE native native/sdk native/third_party)
    add_test(NAME mc_death COMMAND mc_death_tests)
    add_executable(mc_projects_tests tests/test_projects.cpp native/mc_projects.cpp)
    target_compile_features(mc_projects_tests PRIVATE cxx_std_20)
    target_include_directories(mc_projects_tests PRIVATE native native/sdk native/third_party)
    add_test(NAME mc_projects COMMAND mc_projects_tests)
endif()
