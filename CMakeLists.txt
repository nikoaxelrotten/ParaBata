cmake_minimum_required(VERSION 3.22)
# CRT estatico en Windows: no hace falta instalar el runtime de Visual C++
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(ParaBata VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: usa una copia local si se pasa -DJUCE_LOCAL_PATH=..., si no la descarga.
if(DEFINED JUCE_LOCAL_PATH)
  add_subdirectory(${JUCE_LOCAL_PATH} juce EXCLUDE_FROM_ALL)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.12
    GIT_SHALLOW ON)
  FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(ParaBata
  COMPANY_NAME "ParaBata"
  PLUGIN_MANUFACTURER_CODE Pbat
  PLUGIN_CODE Pbta
  FORMATS VST3
  PRODUCT_NAME "ParaBata"
  VST3_CATEGORIES Fx
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(ParaBata PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

target_compile_definitions(ParaBata PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ParaBata
  PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
