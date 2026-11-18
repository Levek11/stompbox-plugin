cmake_minimum_required(VERSION 3.22)

project(StompBox VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    # Universal binary (Apple silicon + Intel) unless the caller chose otherwise.
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    endif()
endif()

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if given (-DSTOMPBOX_JUCE_DIR=...), otherwise fetch.
set(STOMPBOX_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(STOMPBOX_JUCE_DIR)
    add_subdirectory(${STOMPBOX_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
if(APPLE)
    set(STOMPBOX_FORMATS AU VST3)
else()
    set(STOMPBOX_FORMATS VST3)   # Linux/Windows builds are for compile checks only
endif()

juce_add_plugin(StompBox
    PRODUCT_NAME "StompBox"
    COMPANY_NAME "Levek11"
    BUNDLE_ID "com.levek11.stompbox"
    PLUGIN_MANUFACTURER_CODE Levk
    PLUGIN_CODE Stbx
    FORMATS ${STOMPBOX_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Distortion
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(StompBoxAssets
    HEADER_NAME StompBoxAssets.h
    NAMESPACE StompBoxAssets
    SOURCES assets/fonts/Goldman-Bold.ttf assets/fonts/ArchivoBlack-Regular.ttf)

set(STOMPBOX_DSP_SOURCES
    Source/board/PedalCatalog.cpp
    Source/board/BoardModel.cpp
    Source/dsp/Royal30.cpp
    Source/dsp/CabIR.cpp
    Source/dsp/Engine.cpp)

target_sources(StompBox PRIVATE
    ${STOMPBOX_DSP_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/StompLookAndFeel.cpp
    Source/ui/AmpCabTab.cpp
    Source/ui/PedalPainter.cpp
    Source/ui/PedalboardTab.cpp
    Source/ui/PedalCloseUp.cpp)

target_include_directories(StompBox PRIVATE Source)

target_compile_definitions(StompBox PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(StompBox
    PRIVATE
        StompBoxAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Headless DSP tests (run in CI on every build).
juce_add_console_app(StompBoxDspTest PRODUCT_NAME "StompBoxDspTest")
target_sources(StompBoxDspTest PRIVATE Tests/DspTest.cpp ${STOMPBOX_DSP_SOURCES})
target_include_directories(StompBoxDspTest PRIVATE Source Tests)
target_compile_definitions(StompBoxDspTest PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(StompBoxDspTest
    PRIVATE juce::juce_dsp juce::juce_audio_formats juce::juce_data_structures juce::juce_events
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
