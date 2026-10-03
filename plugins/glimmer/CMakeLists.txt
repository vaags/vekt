vekt_add_factory_presets(
	TARGET GlimmerFactoryData
	NAMESPACE GlimmerFactoryData
	HEADER_NAME GlimmerBinaryData.h
	PRESET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets"
	ORDER_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets/factory-order.txt"
	LOCK_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets/factory-order.lock"
	MANIFEST_HEADER GlimmerFactoryPresetManifest.h
	MANIFEST_NAMESPACE vekt::glimmer::factory_presets)

add_library(GlimmerCore STATIC
	Source/FactoryPresets.cpp
	Source/Parameters.cpp
	Source/PluginEditor.cpp
	Source/RotaryEngine.cpp
	Source/PluginProcessor.cpp)
target_include_directories(GlimmerCore PUBLIC
	include
	Source)
target_link_libraries(GlimmerCore PUBLIC
	GlimmerFactoryData
	vekt_project_options
    vekt::juce_headers
	vekt::ui
	vekt::dsp
	vekt::state
	vekt::presets
	vekt::preset_ui
	vekt::plugin_support)

set(glimmer_formats Standalone VST3 AU)

juce_add_plugin(Glimmer
	VERSION ${PROJECT_VERSION}
	COMPANY_NAME "${VEKT_MAKER_NAME}"
	BUNDLE_ID ${VEKT_BUNDLE_ID_PREFIX}.glimmer
	PLUGIN_MANUFACTURER_CODE ${VEKT_MAKER_CODE}
	PLUGIN_CODE Glmr
	FORMATS ${glimmer_formats}
	PRODUCT_NAME "Glimmer"
	DESCRIPTION "A stereo rotary speaker effect"
	IS_SYNTH FALSE
	NEEDS_MIDI_INPUT FALSE
	NEEDS_MIDI_OUTPUT FALSE
	IS_MIDI_EFFECT FALSE
	AU_MAIN_TYPE kAudioUnitType_Effect
	NEEDS_WEB_BROWSER FALSE
	NEEDS_CURL FALSE
	HARDENED_RUNTIME_ENABLED TRUE)
target_sources(Glimmer PRIVATE Source/PluginEntryPoint.cpp)
target_link_libraries(Glimmer PRIVATE
	GlimmerCore
	juce::juce_audio_utils
	juce::juce_audio_formats
	juce::juce_dsp)

vekt_adhoc_seal_bundle(Glimmer_AU)
vekt_adhoc_seal_bundle(Glimmer_VST3)
vekt_adhoc_seal_bundle(Glimmer_Standalone)
