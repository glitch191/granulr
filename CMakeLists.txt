# granulr - granular effect plugin
# Copyright (C) 2026 granulr contributors
# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.22)

# Version shown in the UI and reported to the host. The release workflow passes the tag.
set(GRANULR_VERSION "0.1.0" CACHE STRING "Plugin version (major.minor.patch)")
string(REGEX REPLACE "^v" "" GRANULR_VERSION_CLEAN "${GRANULR_VERSION}")
if(NOT GRANULR_VERSION_CLEAN MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(WARNING "GRANULR_VERSION '${GRANULR_VERSION}' is not major.minor.patch, using 0.1.0")
    set(GRANULR_VERSION_CLEAN "0.1.0")
endif()

project(granulr VERSION ${GRANULR_VERSION_CLEAN} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(VST3_INSTALL_DIR "C:/Program Files/Common Files/VST3" CACHE PATH
    "Folder the VST3 bundle is copied to after each build")
option(GRANULR_COPY_PLUGIN "Copy the VST3 bundle to VST3_INSTALL_DIR after each build" ON)
option(GRANULR_FAST_MATH "Compile with /fp:fast" OFF)

# JUCE, pinned to an exact tag (this is the dependency lock of the project).
include(FetchContent)
set(JUCE_TAG "9.0.5" CACHE STRING "JUCE git tag")
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG ${JUCE_TAG}
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(granulr
    VERSION ${GRANULR_VERSION_CLEAN}
    COMPANY_NAME "granulr"
    PLUGIN_MANUFACTURER_CODE Grnr
    PLUGIN_CODE Gr01
    FORMATS VST3 Standalone
    PRODUCT_NAME "granulr"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_CATEGORIES Fx Delay
    COPY_PLUGIN_AFTER_BUILD ${GRANULR_COPY_PLUGIN}
    VST3_COPY_DIR "${VST3_INSTALL_DIR}")

file(GLOB_RECURSE GRANULR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
target_sources(granulr PRIVATE ${GRANULR_SOURCES})
target_include_directories(granulr PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

file(GLOB GRANULR_FACTORY_PRESETS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/presets/factory/*.xml")
file(GLOB GRANULR_FONTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.ttf")
if(GRANULR_FACTORY_PRESETS OR GRANULR_FONTS)
    juce_add_binary_data(granulr_data
        HEADER_NAME "BinaryData.h"
        NAMESPACE BinaryData
        SOURCES ${GRANULR_FACTORY_PRESETS} ${GRANULR_FONTS})
    set_target_properties(granulr_data PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(granulr PRIVATE granulr_data)
    target_compile_definitions(granulr PUBLIC GRANULR_HAS_BINARY_DATA=1)
endif()

target_compile_definitions(granulr PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    GRANULR_VERSION_STRING="${GRANULR_VERSION_CLEAN}")

target_link_libraries(granulr
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags)

# Compiler flags for the project code.
function(granulr_apply_flags target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /arch:AVX2 /MP /permissive- /utf-8)
        target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:/O2 /GL /Gw /Gy>)
        target_link_options(${target} PRIVATE $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
        if(GRANULR_FAST_MATH)
            target_compile_options(${target} PRIVATE /fp:fast)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -mavx2 -mfma)
    endif()
endfunction()

granulr_apply_flags(granulr)
foreach(format_target granulr_VST3 granulr_Standalone)
    if(TARGET ${format_target} AND MSVC)
        target_link_options(${format_target} PRIVATE $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
    endif()
endforeach()

# Console tools reuse the plugin shared code (a static library) with its JUCE settings.
function(granulr_add_console_tool name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE granulr)
    target_include_directories(${name} PRIVATE $<TARGET_PROPERTY:granulr,INCLUDE_DIRECTORIES>)
    target_compile_definitions(${name} PRIVATE $<TARGET_PROPERTY:granulr,COMPILE_DEFINITIONS>)
    granulr_apply_flags(${name})
endfunction()

file(GLOB GRANULR_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
                                                 "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h")
granulr_add_console_tool(granulr_tests ${GRANULR_TEST_SOURCES})

file(GLOB GRANULR_BENCH_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/bench/*.cpp"
                                                  "${CMAKE_CURRENT_SOURCE_DIR}/bench/*.h")
granulr_add_console_tool(granulr_bench ${GRANULR_BENCH_SOURCES})

enable_testing()
add_test(NAME granulr_unit_tests COMMAND granulr_tests)

# Off-screen screenshot tool (layout checks and README screenshots).
granulr_add_console_tool(granulr_shot "${CMAKE_CURRENT_SOURCE_DIR}/scripts/screenshot/ShotMain.cpp")
