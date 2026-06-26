# CMakeLists.txt — NOISFERATU plugin (VST3 / AU / CLAP / Standalone).
#
# Configure + build:
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release      # or: -G "Unix Makefiles"
#   cmake --build build --target Noisferatu_Standalone -j
#
# JUCE and clap-juce-extensions are fetched automatically (network required on first
# configure). Set NOISFERATU_FETCH_JUCE=OFF and provide your own JUCE via add_subdirectory
# if you'd rather pin a local copy.
cmake_minimum_required(VERSION 3.22)
project(Noisferatu VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

include(FetchContent)

option(NOISFERATU_FETCH_JUCE "Fetch JUCE via FetchContent" ON)
option(NOISFERATU_BUILD_CLAP "Also build a CLAP via clap-juce-extensions" ON)

if(NOISFERATU_FETCH_JUCE)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(NOISFERATU_BUILD_CLAP)
    FetchContent_Declare(clap-juce-extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(clap-juce-extensions)
endif()

juce_add_plugin(Noisferatu
    PRODUCT_NAME              "Noisferatu"
    COMPANY_NAME             "robscape"
    BUNDLE_ID                "com.robscape.noisferatu"
    PLUGIN_MANUFACTURER_CODE  Rbsc
    PLUGIN_CODE               Nsf1
    FORMATS                   AU VST3 Standalone
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE
    VST3_CATEGORIES           "Instrument" "Synth")

target_sources(Noisferatu PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Noisferatu PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Noisferatu PRIVATE
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if(NOISFERATU_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET Noisferatu
        CLAP_ID        "com.robscape.noisferatu"
        CLAP_FEATURES  instrument synthesizer)
endif()
