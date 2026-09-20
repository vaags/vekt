set(vekt_juce_modules
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp)

add_library(vekt_juce_runtime STATIC Runtime.cpp)
add_library(vekt::juce_runtime ALIAS vekt_juce_runtime)
target_link_libraries(vekt_juce_runtime PRIVATE
    ${vekt_juce_modules}
    vekt_project_options)

add_library(vekt_juce_headers INTERFACE)
add_library(vekt::juce_headers ALIAS vekt_juce_headers)
target_include_directories(vekt_juce_headers INTERFACE
    "${JUCE_MODULES_DIR}")
target_compile_definitions(vekt_juce_headers INTERFACE
    VEKT_AUV3_APP_GROUP_ID="${VEKT_AUV3_APP_GROUP_ID}"
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    $<$<CONFIG:Debug>:DEBUG=1>
    $<$<CONFIG:Debug>:_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:_NDEBUG=1>
    JUCE_MODULE_AVAILABLE_juce_audio_basics=1
    JUCE_MODULE_AVAILABLE_juce_audio_devices=1
    JUCE_MODULE_AVAILABLE_juce_audio_formats=1
    JUCE_MODULE_AVAILABLE_juce_audio_processors=1
    JUCE_MODULE_AVAILABLE_juce_audio_processors_headless=1
    JUCE_MODULE_AVAILABLE_juce_audio_utils=1
    JUCE_MODULE_AVAILABLE_juce_core=1
    JUCE_MODULE_AVAILABLE_juce_data_structures=1
    JUCE_MODULE_AVAILABLE_juce_dsp=1
    JUCE_MODULE_AVAILABLE_juce_events=1
    JUCE_MODULE_AVAILABLE_juce_graphics=1
    JUCE_MODULE_AVAILABLE_juce_gui_basics=1
    JUCE_MODULE_AVAILABLE_juce_gui_extra=1)
