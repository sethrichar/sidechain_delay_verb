# Clear Space — ducking delay + reverb (Richardson Media Solutions)
# Build:  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
# Tests:  ctest --test-dir build --output-on-failure
# See README.md and CLAUDE.md.

cmake_minimum_required(VERSION 3.25)

# macOS: universal Release binaries, deployment target 11.0 (must precede project()).
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures" FORCE)
    endif()
endif()

project(ClearSpace
    VERSION 0.4.0
    DESCRIPTION "Ducking delay + reverb"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(CLEARSPACE_BUILD_TESTS  "Build Catch2 tests"                       ON)
option(CLEARSPACE_BUILD_TOOLS  "Build tools/render CLI"                   ON)
option(CLEARSPACE_COPY_PLUGIN  "Copy plugins to the user plug-in folders" OFF)
option(CLEARSPACE_WERROR       "Treat warnings as errors in our own code" ON)

# ---------------------------------------------------------------------------
# Dependencies (pinned; see docs/decisions/ADR-0001-juce-fetch.md)
# ---------------------------------------------------------------------------
set(CLEARSPACE_JUCE_TAG   "8.0.15"  CACHE STRING "JUCE git tag")
set(CLEARSPACE_CATCH2_TAG "v3.16.0" CACHE STRING "Catch2 git tag")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${CLEARSPACE_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   FALSE
    SYSTEM)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Shared core: processor + parameters, compiled into the plugin, the render
# tool, and the tests. INTERFACE so each consumer compiles the sources with
# its own JUCE configuration (this is the pattern JUCE's own examples use).
# ---------------------------------------------------------------------------
set(CLEARSPACE_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Parameters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Ducker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Routing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/delay/DigitalDelay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/delay/DelayEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/reverb/PlateReverb.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/reverb/ReverbEngine.cpp)

add_library(ClearSpaceCore INTERFACE)
target_sources(ClearSpaceCore INTERFACE ${CLEARSPACE_CORE_SOURCES})
target_include_directories(ClearSpaceCore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(ClearSpaceCore INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    CLEARSPACE_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(ClearSpaceCore INTERFACE
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Plugin targets: VST3, AU (macOS only — JUCE ignores it elsewhere), Standalone
# ---------------------------------------------------------------------------
juce_add_plugin(ClearSpace
    PRODUCT_NAME               "Clear Space"
    COMPANY_NAME               "Richardson Media Solutions"
    COMPANY_WEBSITE            "https://richardsonmediasolutions.com"
    BUNDLE_ID                  "com.richardsonmediasolutions.clearspace"
    PLUGIN_MANUFACTURER_CODE   Rmsl
    PLUGIN_CODE                Clsp
    FORMATS                    VST3 AU Standalone
    VST3_CATEGORIES            Fx Delay Reverb
    AU_MAIN_TYPE               kAudioUnitType_Effect
    IS_SYNTH                   FALSE
    NEEDS_MIDI_INPUT           FALSE
    NEEDS_MIDI_OUTPUT          FALSE
    IS_MIDI_EFFECT             FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD    ${CLEARSPACE_COPY_PLUGIN})

# The Standalone wrapper needs the audio-device layer; the plugin formats don't, and
# neither do the console consumers, so it is linked here rather than in ClearSpaceCore.
target_link_libraries(ClearSpace PRIVATE
    ClearSpaceCore
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_recommended_lto_flags)

# ---------------------------------------------------------------------------
# Tools and tests
# ---------------------------------------------------------------------------
if(CLEARSPACE_BUILD_TOOLS)
    add_subdirectory(tools/render)
endif()

if(CLEARSPACE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# -Werror on OUR sources only. JUCE module translation units are compiled into
# the same targets, so a target-wide -Werror would also police JUCE; instead
# the flag is attached to each of our source files, in every directory that
# builds them (TARGET_DIRECTORY). Lists are appended by the subdirectories.
# ---------------------------------------------------------------------------
if(CLEARSPACE_WERROR)
    set(_werror_targets ClearSpace)
    if(TARGET render)
        list(APPEND _werror_targets render)
    endif()
    if(TARGET ClearSpaceTests)
        list(APPEND _werror_targets ClearSpaceTests)
    endif()
    set_source_files_properties(${CLEARSPACE_CORE_SOURCES} ${CLEARSPACE_OWN_SOURCES}
        TARGET_DIRECTORY ${_werror_targets}
        PROPERTIES COMPILE_OPTIONS
            "$<IF:$<CXX_COMPILER_ID:MSVC>,/WX,-Werror>")
endif()
