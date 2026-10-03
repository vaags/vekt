vekt_add_factory_presets(
	TARGET VektRavFactoryData
	NAMESPACE VektRavFactoryData
	HEADER_NAME BinaryData.h
	PRESET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets"
	ORDER_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets/factory-order.txt"
	LOCK_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets/factory-order.lock"
	MANIFEST_HEADER VektRavFactoryPresetManifest.h
	MANIFEST_NAMESPACE vekt::rav::factory_presets)

add_library(VektRavCore STATIC
	Source/FactoryPresets.cpp
	Source/Parameters.cpp
	Source/PluginEditor.cpp
	Source/PluginProcessor.cpp)

target_include_directories(VektRavCore PUBLIC
	include
	Source)

target_link_libraries(VektRavCore
	PUBLIC
        vekt::juce_headers
		vekt::ui
		VektRavFactoryData
		vekt::dsp
		vekt::presets
		vekt::preset_ui
		vekt::state
		vekt::plugin_support
		vekt_project_options)

set(vekt_rav_formats Standalone VST3 AU)

juce_add_plugin(VektRav
	VERSION ${PROJECT_VERSION}
	COMPANY_NAME "${VEKT_MAKER_NAME}"
	BUNDLE_ID ${VEKT_BUNDLE_ID_PREFIX}.rav
	PLUGIN_MANUFACTURER_CODE ${VEKT_MAKER_CODE}
	PLUGIN_CODE Ravv
	FORMATS ${vekt_rav_formats}
	PRODUCT_NAME "Rav"
	DESCRIPTION "A six-mode multiband saturation effect"
	IS_SYNTH FALSE
	NEEDS_MIDI_INPUT FALSE
	NEEDS_MIDI_OUTPUT FALSE
	IS_MIDI_EFFECT FALSE
	AU_MAIN_TYPE kAudioUnitType_Effect
	NEEDS_WEB_BROWSER FALSE
	NEEDS_CURL FALSE
	HARDENED_RUNTIME_ENABLED TRUE)

target_sources(VektRav PRIVATE Source/PluginEntryPoint.cpp)
target_link_libraries(VektRav PRIVATE
	VektRavCore
	juce::juce_audio_utils
	juce::juce_audio_formats
	juce::juce_dsp)

vekt_adhoc_seal_bundle(VektRav_AU)
vekt_adhoc_seal_bundle(VektRav_VST3)
vekt_adhoc_seal_bundle(VektRav_Standalone)
