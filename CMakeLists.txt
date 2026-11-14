cmake_minimum_required(VERSION 3.22)

# Static MSVC runtime so the plugin has no VC++ redistributable dependency
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)  # universal 64-bit
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
endif()

project(GuitarToMidi VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(GuitarToMidi
  COMPANY_NAME "GuitarToMidi"
  PLUGIN_MANUFACTURER_CODE Gtmd
  PLUGIN_CODE Gtm1
  FORMATS VST3
  PRODUCT_NAME "GuitarToMidi"
  IS_SYNTH TRUE                 # shows up as an Instrument
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT TRUE
  IS_MIDI_EFFECT FALSE
  VST3_CATEGORIES Instrument
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(GuitarToMidi PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginProcessor.h
  Source/PitchDetector.h)

target_compile_definitions(GuitarToMidi PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(GuitarToMidi
  PRIVATE juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags
         juce::juce_recommended_lto_flags
         juce::juce_recommended_warning_flags)
