cmake_minimum_required(VERSION 3.22)
project(Laaa VERSION 0.1.0 LANGUAGES C CXX)

set(JUCE_SOURCE_DIR "" CACHE PATH "Path to JUCE 8.0.9 sources")
set(COPY_PLUGIN_AFTER_BUILD OFF CACHE BOOL
    "Copy built AU and VST3 plug-ins to the system plug-in folders")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

add_library(laaa_dsp STATIC Source/dsp/La2aEngine.cpp)
target_include_directories(laaa_dsp PUBLIC Source)
if(MSVC)
    target_compile_options(laaa_dsp PRIVATE /W4 /permissive-)
else()
    target_compile_options(laaa_dsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(laaa_test tests/La2aTest.cpp)
target_link_libraries(laaa_test PRIVATE laaa_dsp)
add_test(NAME laaa_test COMMAND laaa_test)
add_executable(laaa_benchmark tests/La2aBenchmark.cpp)
target_link_libraries(laaa_benchmark PRIVATE laaa_dsp)

set(_juce_source "")
if(JUCE_SOURCE_DIR)
    if(NOT EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "JUCE_SOURCE_DIR does not contain JUCE's CMakeLists.txt: ${JUCE_SOURCE_DIR}")
    endif()
    set(_juce_source "${JUCE_SOURCE_DIR}")
else()
    foreach(_candidate IN ITEMS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE")
        if(EXISTS "${_candidate}/CMakeLists.txt")
            file(STRINGS "${_candidate}/CMakeLists.txt" _juce_project_line
                REGEX "^project\\(JUCE VERSION [0-9.]+")
            if(_juce_project_line MATCHES "VERSION 8\\.0\\.9")
                set(_juce_source "${_candidate}")
                break()
            else()
                message(STATUS "Skipping JUCE checkout without a declared 8.0.9 version: ${_candidate}")
            endif()
        endif()
    endforeach()
endif()

if(_juce_source)
    file(STRINGS "${_juce_source}/CMakeLists.txt" _juce_project_line
        REGEX "^project\\(JUCE VERSION [0-9.]+")
    if(NOT _juce_project_line MATCHES "VERSION 8\\.0\\.9")
        message(FATAL_ERROR "LAAA requires JUCE 8.0.9; ${_juce_source} declares: ${_juce_project_line}")
    endif()
    message(STATUS "Using JUCE 8.0.9 from ${_juce_source}")
    add_subdirectory("${_juce_source}" juce-build)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(Laaa
    PRODUCT_NAME "LAAA"
    COMPANY_NAME "codycodie"
    BUNDLE_ID "com.c0dypeng.laaa"
    PLUGIN_MANUFACTURER_CODE Cdpg
    PLUGIN_CODE Laaa
    FORMATS AU VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${COPY_PLUGIN_AFTER_BUILD})

juce_add_binary_data(LaaaAssets SOURCES
    Resources/fonts/Inter-SemiBold.ttf
    Resources/fonts/JetBrainsMono-Medium.ttf)

target_sources(Laaa PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(Laaa PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Laaa PRIVATE laaa_dsp LaaaAssets juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

juce_add_console_app(laaa_snapshot)
target_sources(laaa_snapshot PRIVATE tests/snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(laaa_snapshot PRIVATE JucePlugin_Name="LAAA" JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(laaa_snapshot PRIVATE laaa_dsp LaaaAssets juce::juce_audio_utils
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
