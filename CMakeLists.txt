cmake_minimum_required(VERSION 3.25)

project(Vekt VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VEKT_BUILD_TESTS "Build Vekt tests" ON)
option(VEKT_BUILD_AUDIO_LAB "Build the Vekt development audio lab" OFF)
set(VEKT_LTO "thin" CACHE STRING "Link-time optimisation in Release builds: thin (default; parallel and cached), full or off")
set_property(CACHE VEKT_LTO PROPERTY STRINGS full thin off)

add_subdirectory(external/JUCE EXCLUDE_FROM_ALL)

add_library(vekt_project_options INTERFACE)
target_compile_features(vekt_project_options INTERFACE cxx_std_20)
target_link_libraries(vekt_project_options INTERFACE
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if(VEKT_LTO STREQUAL "full")
    target_link_libraries(vekt_project_options INTERFACE $<$<CONFIG:Release>:juce::juce_recommended_lto_flags>)
elseif(VEKT_LTO STREQUAL "thin")
    # Parallel, cached LTO: each link reuses unchanged modules from the cache.
    target_compile_options(vekt_project_options INTERFACE $<$<CONFIG:Release>:-flto=thin>)
    target_link_options(vekt_project_options INTERFACE
        $<$<CONFIG:Release>:-flto=thin>
        $<$<CONFIG:Release>:-Wl,-cache_path_lto,${CMAKE_BINARY_DIR}/lto-cache>)
elseif(NOT VEKT_LTO STREQUAL "off")
    message(FATAL_ERROR "VEKT_LTO must be full, thin or off")
endif()

include(cmake/VektFactoryPresets.cmake)
include(cmake/VektPluginSigning.cmake)

add_subdirectory(framework/juce)
add_subdirectory(framework/dsp)
add_subdirectory(framework/state)
add_subdirectory(framework/presets)
add_subdirectory(framework/ui)
add_subdirectory(framework/preset_ui)

if(VEKT_BUILD_TESTS OR VEKT_BUILD_AUDIO_LAB)
    add_subdirectory(framework/audio_analysis)
endif()

add_subdirectory(plugins/vekt_rav)
add_subdirectory(plugins/vekt_glimmer)
add_subdirectory(plugins/vekt_mono)

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
