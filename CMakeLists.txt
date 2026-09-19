cmake_minimum_required(VERSION 3.25)

project(Vekt VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VEKT_BUILD_TESTS "Build Vekt tests" ON)
option(VEKT_BUILD_AUDIO_LAB "Build the Vekt development audio lab" OFF)
set(VEKT_AUV3_APP_GROUP_ID "" CACHE STRING
    "Optional macOS App Group identifier used by the AUv3 preset container")

add_subdirectory(external/JUCE EXCLUDE_FROM_ALL)

add_library(vekt_project_options INTERFACE)
target_compile_features(vekt_project_options INTERFACE cxx_std_20)
target_link_libraries(vekt_project_options INTERFACE
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    $<$<CONFIG:Release>:juce::juce_recommended_lto_flags>)

add_subdirectory(framework/juce)
add_subdirectory(framework/dsp)
add_subdirectory(framework/state)
add_subdirectory(framework/presets)
add_subdirectory(framework/ui)
add_subdirectory(framework/preset_ui)
add_subdirectory(plugins/vekt_rav)
add_subdirectory(plugins/vekt_glimmer)

if(VEKT_BUILD_AUDIO_LAB)
    add_subdirectory(tools/audio_lab)
endif()

if(VEKT_BUILD_TESTS)
    include(CTest)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG dfc2dff8d70d083c60c1c6986030e5389a867a93
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_subdirectory(tests)
endif()
