vekt_add_factory_presets(
	TARGET VektGlimmerFactoryData
	NAMESPACE VektGlimmerFactoryData
	HEADER_NAME GlimmerBinaryData.h
	PRESET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets"
	ORDER_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets/factory-order.txt"
	LOCK_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets/factory-order.lock"
	MANIFEST_HEADER VektGlimmerFactoryPresetManifest.h
	MANIFEST_NAMESPACE vekt::glimmer::factory_presets)

add_library(VektGlimmerCore STATIC
	Source/FactoryPresets.cpp
	Source/Parameters.cpp
	Source/PluginEditor.cpp
	Source/RotaryEngine.cpp
	Source/PluginProcessor.cpp)
target_include_directories(VektGlimmerCore PUBLIC
	include
	Source)
target_link_libraries(VektGlimmerCore PUBLIC
	VektGlimmerFactoryData
	vekt_project_options
    vekt::juce_headers
	vekt::ui
	vekt::dsp
	vekt::state
	vekt::presets
	vekt::preset_ui)

set(vekt_glimmer_formats Standalone VST3)
if(CMAKE_GENERATOR STREQUAL "Xcode")
	list(APPEND vekt_glimmer_formats AUv3)
endif()

juce_add_plugin(VektGlimmer
	VERSION ${PROJECT_VERSION}
	COMPANY_NAME Vekt
	BUNDLE_ID com.vekt.glimmer
	PLUGIN_MANUFACTURER_CODE Vekt
	PLUGIN_CODE Glmr
	FORMATS ${vekt_glimmer_formats}
	PRODUCT_NAME "Vekt Glimmer"
	DESCRIPTION "A stereo rotary speaker effect"
	IS_SYNTH FALSE
	NEEDS_MIDI_INPUT FALSE
	NEEDS_MIDI_OUTPUT FALSE
	IS_MIDI_EFFECT FALSE
	AU_MAIN_TYPE kAudioUnitType_Effect
	NEEDS_WEB_BROWSER FALSE
	NEEDS_CURL FALSE
	HARDENED_RUNTIME_ENABLED TRUE)
target_sources(VektGlimmer PRIVATE Source/PluginEntryPoint.cpp)
target_link_libraries(VektGlimmer PRIVATE
	VektGlimmerCore
	juce::juce_audio_utils
	juce::juce_audio_formats
	juce::juce_dsp)
